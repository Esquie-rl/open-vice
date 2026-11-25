//! Open Vice diagnostics: the trace buffer of a traced shader.
//!
//! `VICTORY_TRACE_SHADER=<hash>` translates that shader so that thread 0 of
//! group `VICTORY_TRACE_GROUP` (x, default 0) writes every register value
//! it computes into this buffer (see `victory_shader::emit::trace_group`).
//! The buffer is cleared before frame `VICTORY_TRACE_FRAME` (default 1600)
//! and saved two presents after it as `trace-<hash>.txt` in
//! `VICTORY_CAPTURE` (or the working folder): one line per slot written,
//! `<code offset> <register> <value>`. The values are the title's: the file
//! stays outside the repository.

use std::sync::Mutex;
use std::sync::atomic::{AtomicU64, AtomicU8, Ordering};

use windows::Win32::Graphics::Direct3D12::ID3D12Resource;

/// Bytes: the slots, then a scratch slot per thread of a group.
const BYTES: u64 = (1 + victory_shader::emit::TRACE_SLOTS as u64 + 1024) * 16;

struct Buffer {
    _resource: ID3D12Resource,
    data: usize,
    gpu: u64,
}

// SAFETY: a persistently mapped buffer only read and cleared under the lock.
unsafe impl Send for Buffer {}

static BUFFER: Mutex<Option<Buffer>> = Mutex::new(None);
/// 0 waiting, 1 cleared, 2 saved.
static STATE: AtomicU8 = AtomicU8::new(0);
/// The present the buffer was cleared at, and the traced shader's binds since.
static CLEARED: AtomicU64 = AtomicU64::new(0);
static BINDS: AtomicU64 = AtomicU64::new(0);

/// A draw or dispatch bound the trace buffer.
pub(crate) fn bound() {
    BINDS.fetch_add(1, Ordering::Relaxed);
}

/// The traced shader's hash, if `VICTORY_TRACE_SHADER` names one.
pub(crate) fn traced() -> Option<String> {
    std::env::var("VICTORY_TRACE_SHADER").ok().filter(|h| !h.is_empty())
}

/// The traced group.
pub(crate) fn group() -> u32 {
    std::env::var("VICTORY_TRACE_GROUP")
        .ok()
        .and_then(|g| g.parse().ok())
        .unwrap_or(0)
}

fn frame() -> u64 {
    std::env::var("VICTORY_TRACE_FRAME")
        .ok()
        .and_then(|g| g.parse().ok())
        .unwrap_or(1600)
}

/// The buffer's host GPU address, made on first use.
pub(crate) fn gpu() -> Option<u64> {
    let mut b = BUFFER.lock().unwrap_or_else(|e| e.into_inner());
    if b.is_none() {
        let (resource, data) = crate::host::host()?.shared_buffer(BYTES)?;
        // SAFETY: a fresh mapping of BYTES; a live buffer.
        let gpu = unsafe {
            std::ptr::write_bytes(data, 0, BYTES as usize);
            resource.GetGPUVirtualAddress()
        };
        *b = Some(Buffer {
            _resource: resource,
            data: data as usize,
            gpu,
        });
    }
    b.as_ref().map(|b| b.gpu)
}

/// Called as frames are presented: clears the buffer before the traced
/// frame and saves it two presents after.
pub(crate) fn tick(presented: u64) {
    let Some(hash) = traced() else { return };
    let f = frame();
    let b = BUFFER.lock().unwrap_or_else(|e| e.into_inner());
    let Some(b) = b.as_ref() else { return };
    if presented + 1 >= f && STATE.compare_exchange(0, 1, Ordering::Relaxed, Ordering::Relaxed).is_ok() {
        // SAFETY: the mapping; submissions are synchronous, none runs now.
        unsafe { std::ptr::write_bytes(b.data as *mut u8, 0, BYTES as usize) };
        CLEARED.store(presented, Ordering::Relaxed);
        BINDS.store(0, Ordering::Relaxed);
        tracing::info!(frame = presented + 1, "trace buffer cleared");
    }
    // Two presents after the clear, once the traced shader ran since.
    if presented >= CLEARED.load(Ordering::Relaxed) + 2
        && BINDS.load(Ordering::Relaxed) > 0
        && STATE.compare_exchange(1, 2, Ordering::Relaxed, Ordering::Relaxed).is_ok()
    {
        let n = victory_shader::emit::TRACE_SLOTS as usize;
        // SAFETY: the mapping, (1 + n) slots of four dwords.
        let words = unsafe { std::slice::from_raw_parts(b.data as *const u32, (1 + n) * 4) };
        let mut text = String::new();
        let mut lines = 0;
        for slot in words.as_chunks::<4>().0.iter().skip(1) {
            if slot[3] >> 16 != 0x7ACE {
                continue;
            }
            text.push_str(&format!("{:06x} {:03x} {:08x} {}\n", slot[0], slot[1], slot[2], slot[3] & 0xFFFF));
            lines += 1;
        }
        let dir = std::env::var_os("VICTORY_CAPTURE").map_or_else(|| ".".into(), std::path::PathBuf::from);
        let path = dir.join(format!("trace-{hash}.txt"));
        match std::fs::write(&path, text) {
            Ok(()) => tracing::info!(path = %path.display(), lines, binds = BINDS.load(Ordering::Relaxed), "trace saved"),
            Err(e) => tracing::warn!(error = %e, "trace not saved"),
        }
    }
}
