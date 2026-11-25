//! Copy-back costs, enabled in either build with `VICTORY_BACK_TIMING=1`.
//! Merge contains its read/compare/write stages. Flight waiting is outside
//! the frame line's copy-back time. GPU copy time overlaps the fetch fence
//! wait; it is not added to it. Threads may overlap one another.

use std::sync::OnceLock;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Instant;

use windows::Win32::Graphics::Direct3D12::*;

#[derive(Clone, Copy)]
pub(crate) enum C {
    Plan,
    Submit,
    ReadyWait,
    Wait,
    Gpu,
    GpuSamples,
    Scan,
    Merge,
    Lock,
    Compare,
    Read,
    Blend,
    Guard,
    Write,
    ReadBytes,
    WriteBytes,
    ReadCalls,
    WriteRuns,
    FetchBytes,
    FetchRanges,
    Count,
}

static COUNTERS: [AtomicU64; C::Count as usize] = [const { AtomicU64::new(0) }; C::Count as usize];

pub(crate) fn enabled() -> bool {
    static ON: OnceLock<bool> = OnceLock::new();
    *ON.get_or_init(|| std::env::var_os("VICTORY_BACK_TIMING").is_some_and(|v| v == "1"))
}

pub(crate) fn add(counter: C, value: u64) {
    if enabled() {
        COUNTERS[counter as usize].fetch_add(value, Ordering::Relaxed);
    }
}

pub(crate) struct Timed(C, Option<Instant>);

impl Timed {
    pub(crate) fn new(counter: C) -> Self {
        Self(counter, enabled().then(Instant::now))
    }
}

impl Drop for Timed {
    fn drop(&mut self) {
        if let Some(start) = self.1 {
            add(self.0, start.elapsed().as_micros() as u64);
        }
    }
}

pub(crate) fn read(src: *const u8, dst: &mut [u8]) -> bool {
    let _time = Timed::new(C::Read);
    add(C::ReadBytes, dst.len() as u64);
    add(C::ReadCalls, 1);
    crate::host::read_into(src, dst)
}

pub(crate) fn write(at: usize, bytes: &[u8]) {
    add(C::WriteRuns, 1);
    {
        let _time = Timed::new(C::Guard);
        crate::watch::writing(at, bytes.len());
    }
    let _time = Timed::new(C::Write);
    crate::host::write_mapped(at as *mut u8, bytes.as_ptr(), bytes.len());
    // As in COPY_BACK: requested bytes, including unchanged bytes inside
    // a merged stretch, not a claim that every destination was mapped.
    add(C::WriteBytes, bytes.len() as u64);
}

pub(crate) fn line(frames: f64) {
    if !enabled() {
        return;
    }
    let c = COUNTERS
        .each_ref()
        .map(|c| c.swap(0, Ordering::Relaxed) as f64 / frames);
    let ms = |i: C| c[i as usize] / 1000.0;
    tracing::info!(
        "copy-back stages per frame: plan {:.2} ms, submit {:.2} ms, flight fence {:.2} ms, fetch fence {:.2} ms \
         (GPU copies {:.2} ms in {:.1} timestamp samples, overlaps fence); CPU scan {:.2} ms, \
         merge {:.2} ms (lock {:.2}, compare {:.2}, title read {:.2}, blend {:.2}, guards {:.2}, title write {:.2}); \
         fetched {:.2} MB in {:.0} ranges, title read {:.2} MB in {:.0} calls, title write {:.2} MB in {:.0} runs; \
         card pieces {} KiB, CPU batches {} KiB, fetch queue {}",
        ms(C::Plan),
        ms(C::Submit),
        ms(C::ReadyWait),
        ms(C::Wait),
        ms(C::Gpu),
        c[C::GpuSamples as usize],
        ms(C::Scan),
        ms(C::Merge),
        ms(C::Lock),
        ms(C::Compare),
        ms(C::Read),
        ms(C::Blend),
        ms(C::Guard),
        ms(C::Write),
        c[C::FetchBytes as usize] / 1e6,
        c[C::FetchRanges as usize],
        c[C::ReadBytes as usize] / 1e6,
        c[C::ReadCalls as usize],
        c[C::WriteBytes as usize] / 1e6,
        c[C::WriteRuns as usize],
        crate::shadow::card_piece_bytes() / 1024,
        crate::shadow::back_batch_bytes() / 1024,
        if crate::host::relay() && crate::host::relay_copy_queue() {
            "copy (ordered)"
        } else {
            "direct"
        },
    );
}

/// A timestamp pair owned by one fetch list. It is resolved by that list
/// and read only after the fetch's existing fence; it adds no CPU wait.
pub(crate) struct Gpu {
    heap: ID3D12QueryHeap,
    back: ID3D12Resource,
    frequency: u64,
}

impl Gpu {
    pub(crate) fn new(host: &crate::host::Host, queue: &ID3D12CommandQueue) -> Option<Self> {
        if !enabled() {
            return None;
        }
        let mut heap = None;
        // SAFETY: plain capability queries and query heap creation.
        let frequency = unsafe {
            let kind = if queue.GetDesc().Type == D3D12_COMMAND_LIST_TYPE_COPY {
                let mut options = D3D12_FEATURE_DATA_D3D12_OPTIONS3::default();
                host.device
                    .CheckFeatureSupport(
                        D3D12_FEATURE_D3D12_OPTIONS3,
                        (&mut options as *mut D3D12_FEATURE_DATA_D3D12_OPTIONS3).cast(),
                        std::mem::size_of_val(&options) as u32,
                    )
                    .ok()?;
                if !options.CopyQueueTimestampQueriesSupported.as_bool() {
                    return None;
                }
                D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP
            } else {
                D3D12_QUERY_HEAP_TYPE_TIMESTAMP
            };
            host.device
                .CreateQueryHeap(
                    &D3D12_QUERY_HEAP_DESC {
                        Type: kind,
                        Count: 2,
                        NodeMask: 0,
                    },
                    &mut heap,
                )
                .ok()?;
            queue.GetTimestampFrequency().ok()?
        };
        (frequency != 0).then_some(Self {
            heap: heap?,
            back: host.read_back_buffer(16)?,
            frequency,
        })
    }

    pub(crate) fn begin(&self, list: &ID3D12GraphicsCommandList) {
        // SAFETY: an open list of the heap's queue type and its first timestamp.
        unsafe { list.EndQuery(&self.heap, D3D12_QUERY_TYPE_TIMESTAMP, 0) };
    }

    pub(crate) fn end(&self, list: &ID3D12GraphicsCommandList) {
        // SAFETY: our heap and a read-back buffer of two u64 values.
        unsafe {
            list.EndQuery(&self.heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            list.ResolveQueryData(&self.heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, &self.back, 0);
        }
    }

    /// Called only after the fetch fence has completed.
    pub(crate) fn finished(&self) {
        let mut data = std::ptr::null_mut();
        let read = D3D12_RANGE { Begin: 0, End: 16 };
        // SAFETY: the caller waited for the resolve, the buffer holds two
        // timestamps, and nothing reuses its list until this returns.
        unsafe {
            if self.back.Map(0, Some(&read), Some(&mut data)).is_ok() && !data.is_null() {
                let data = data.cast::<u64>();
                let ticks = data.add(1).read().saturating_sub(data.read());
                add(
                    C::Gpu,
                    (u128::from(ticks) * 1_000_000 / u128::from(self.frequency)) as u64,
                );
                add(C::GpuSamples, 1);
                self.back.Unmap(0, Some(&D3D12_RANGE { Begin: 0, End: 0 }));
            }
        }
    }
}
