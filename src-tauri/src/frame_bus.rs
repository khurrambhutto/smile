use std::{
    sync::{Arc, Condvar, Mutex},
    time::{Duration, Instant},
};

/// A single-slot "latest frame" broadcast primitive.
///
/// The capture thread publishes the most recent encoded JPEG; any number of
/// HTTP streaming clients wait on `next` for the next frame whose sequence
/// differs from the one they last observed. Slow clients naturally drop
/// frames — we never queue.
pub struct FrameBus {
    inner: Mutex<Inner>,
    cond: Condvar,
}

struct Inner {
    frame: Option<Arc<Vec<u8>>>,
    sequence: u64,
}

impl FrameBus {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            inner: Mutex::new(Inner {
                frame: None,
                sequence: 0,
            }),
            cond: Condvar::new(),
        })
    }

    /// Return the most recently published frame, or `None` if the camera has
    /// not yet produced any output.
    pub fn latest(&self) -> Option<Arc<Vec<u8>>> {
        let guard = self.inner.lock().expect("frame bus poisoned");
        guard.frame.clone()
    }

    pub fn publish(&self, bytes: Arc<Vec<u8>>) {
        let mut guard = self.inner.lock().expect("frame bus poisoned");
        guard.sequence = guard.sequence.wrapping_add(1);
        guard.frame = Some(bytes);
        drop(guard);
        self.cond.notify_all();
    }

    /// Block until a frame with `sequence != last_seen` is available, or the
    /// timeout elapses. Returns `None` on timeout so callers can decide
    /// whether to keep waiting or close the connection.
    pub fn next(
        &self,
        last_seen: u64,
        timeout: Duration,
    ) -> Option<(Arc<Vec<u8>>, u64)> {
        let mut guard = self.inner.lock().expect("frame bus poisoned");
        let deadline = Instant::now() + timeout;

        loop {
            if guard.sequence != last_seen {
                if let Some(frame) = &guard.frame {
                    return Some((Arc::clone(frame), guard.sequence));
                }
            }

            let now = Instant::now();
            if now >= deadline {
                return None;
            }

            let (next_guard, wait_res) = self
                .cond
                .wait_timeout(guard, deadline - now)
                .expect("frame bus poisoned");
            guard = next_guard;

            if wait_res.timed_out() && guard.sequence == last_seen {
                return None;
            }
        }
    }
}
