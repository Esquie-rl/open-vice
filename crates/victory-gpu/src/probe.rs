//! Diagnostics (Open Vice): the first record of each indirect execution of a
//! frame `VICTORY_CAPTURE_FRAMES` lists, as the GPU executed it.
//!
//! The frame report reads arguments from the host copy when the command is
//! recorded, before the title's passes write them on the GPU in the same
//! submission. Here each execution copies its first record (after the
//! clamp) into a read-back buffer right after it runs; once the frame has
//! been presented twice more, the records are logged at target
//! `victory_gpu::frame_report` as `probe <slot> [..]`, the slot the
//! report's line names.

use std::sync::Mutex;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};

use windows::Win32::Graphics::Direct3D12::*;
use windows::Win32::Graphics::Dxgi::Common::DXGI_SAMPLE_DESC;

use crate::host::HostList;

const SLOTS: u32 = 1 << 15;
const RECORD: u64 = 16;

struct Back(ID3D12Resource);
// SAFETY: a read-back buffer only mapped under the lock.
unsafe impl Send for Back {}

static BACK: Mutex<Option<Back>> = Mutex::new(None);
static FRAME: AtomicU64 = AtomicU64::new(0);
static NEXT: AtomicU32 = AtomicU32::new(0);
static DUMPED: AtomicBool = AtomicBool::new(false);

fn buffer() -> Option<ID3D12Resource> {
    let mut back = BACK.lock().unwrap_or_else(|e| e.into_inner());
    if back.is_none() {
        let device = &crate::host::host()?.device;
        let heap = D3D12_HEAP_PROPERTIES { Type: D3D12_HEAP_TYPE_READBACK, ..Default::default() };
        let desc = D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_BUFFER,
            Width: u64::from(SLOTS) * RECORD,
            Height: 1,
            DepthOrArraySize: 1,
            MipLevels: 1,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Layout: D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            ..Default::default()
        };
        let mut made: Option<ID3D12Resource> = None;
        // SAFETY: plain creation of a read-back buffer.
        unsafe {
            device
                .CreateCommittedResource(
                    &heap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    None,
                    &mut made,
                )
                .ok()?;
        }
        *back = Some(Back(made?));
    }
    back.as_ref().map(|b| b.0.clone())
}

/// After an indirect execution in `list` that read `args` (in a read
/// state) at `offset`: copies up to `len` bytes of its first record for the
/// listed frame, and returns the slot the report names.
pub(crate) fn record(list: &HostList, args: &ID3D12Resource, offset: u64, len: u64) -> Option<u32> {
    let frame = crate::queue::presented() + 1;
    if !crate::capture::listed(frame) {
        return None;
    }
    if FRAME.swap(frame, Ordering::Relaxed) != frame {
        NEXT.store(0, Ordering::Relaxed);
        DUMPED.store(false, Ordering::Relaxed);
    }
    let slot = NEXT.fetch_add(1, Ordering::Relaxed);
    if slot >= SLOTS {
        return None;
    }
    // SAFETY: reading a live resource's description.
    let width = unsafe { args.GetDesc() }.Width;
    let n = len.min(RECORD).min(width.saturating_sub(offset)) & !3;
    if n == 0 {
        return None;
    }
    let back = buffer()?;
    // SAFETY: an open list; both ranges inside their buffers, the source in a
    // read state (copy source among them), the read-back buffer in copy-dest.
    unsafe { list.list.CopyBufferRegion(&back, u64::from(slot) * RECORD, args, offset, n) };
    Some(slot)
}

/// Logs the records of the last listed frame once it is two presents old.
pub(crate) fn flush() {
    let frame = FRAME.load(Ordering::Relaxed);
    if frame == 0 || crate::queue::presented() < frame + 2 || DUMPED.swap(true, Ordering::Relaxed) {
        return;
    }
    let count = NEXT.load(Ordering::Relaxed).min(SLOTS) as usize;
    let Some(back) = buffer() else { return };
    let mut data = std::ptr::null_mut();
    let range = D3D12_RANGE { Begin: 0, End: count * RECORD as usize };
    // SAFETY: a read-back buffer the frame's finished lists copied into.
    let words: Vec<u32> = unsafe {
        if back.Map(0, Some(&range), Some(&mut data)).is_err() {
            return;
        }
        let w = std::slice::from_raw_parts(data.cast::<u32>(), count * 4).to_vec();
        back.Unmap(0, Some(&D3D12_RANGE::default()));
        w
    };
    for (slot, r) in words.as_chunks::<4>().0.iter().enumerate() {
        tracing::info!(target: "victory_gpu::frame_report", "probe {slot} {r:?}");
    }
    tracing::info!(target: "victory_gpu::frame_report", "probe done {count} records of frame {frame}");
}
