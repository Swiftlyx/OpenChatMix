//! OpenChatMix plugin ABI 1 over DeepFilterNet3 (libDF with the tract backend and the embedded model).
//!
//! Fixed 48 kHz mono. Each call to `ocm_df_process` takes one frame of `ocm_df_frame_length` samples
//! in -1..1. Every entry point catches panics, so none reaches the caller; a state that failed once
//! (panic, libDF error or non-finite output) reports failure on every later `ocm_df_process`. A state
//! may move between threads but must not be used by two threads at once.

use std::ffi::{c_char, c_void};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

// The deep_filter package names its library `df`.
use df::tract::{DfParams, DfTract, RuntimeParams};
use ndarray::{ArrayView2, ArrayViewMut2};

const ABI_VERSION: u32 = 1;
const DESCRIPTION: &[u8] = b"DeepFilterNet3 (libDF d375b2d)\0";
const SAMPLE_RATE: usize = 48000;
const FRAME_LENGTH: usize = 480;

// ocm_df_process results; any non-zero value means the caller passes the input through.
const PROCESS_OK: i32 = 0;
const PROCESS_INVALID_ARGUMENT: i32 = 1;
const PROCESS_FAILED_BEFORE: i32 = 2;
const PROCESS_ERROR: i32 = 3;
const PROCESS_PANIC: i32 = 4;

struct State {
    df: DfTract,
    // Copies of the caller's frame, so input and output may be the same buffer.
    input: Vec<f32>,
    output: Vec<f32>,
    failed: bool,
}

impl State {
    fn new() -> Option<Self> {
        // libDF runtime defaults: no attenuation limit, post filter off, thresholds -10 / 30 / 20 dB.
        let params = RuntimeParams::default_with_ch(1)
            .with_atten_lim(100.0)
            .with_post_filter(0.0)
            .with_thresholds(-10.0, 30.0, 20.0);
        let df = DfTract::new(DfParams::default(), &params).ok()?;
        if df.sr != SAMPLE_RATE || df.hop_size != FRAME_LENGTH || df.ch != 1 {
            return None;
        }
        Some(Self {
            df,
            input: vec![0.0; FRAME_LENGTH],
            output: vec![0.0; FRAME_LENGTH],
            failed: false,
        })
    }

    fn process(&mut self) -> i32 {
        #[cfg(feature = "panic-probe")]
        if self.input[0] == 12345.0 {
            panic!("panic probe");
        }
        // NaN or infinity would stay in the recurrent state for good.
        for sample in self.input.iter_mut() {
            if !sample.is_finite() {
                *sample = 0.0;
            }
        }
        let noisy = ArrayView2::from_shape((1, FRAME_LENGTH), &self.input[..]);
        let enhanced = ArrayViewMut2::from_shape((1, FRAME_LENGTH), &mut self.output[..]);
        let (Ok(noisy), Ok(enhanced)) = (noisy, enhanced) else {
            return PROCESS_ERROR;
        };
        if self.df.process(noisy, enhanced).is_err() || !self.output.iter().all(|s| s.is_finite()) {
            return PROCESS_ERROR;
        }
        PROCESS_OK
    }
}

/// Runs `f` and returns `fallback` if it panics.
fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(fallback)
}

/// Runs `f` on a state that has not failed; a panic marks the state failed.
///
/// # Safety
/// `st` is null or a live pointer from `ocm_df_create` that no other thread uses.
unsafe fn with_state(st: *mut c_void, f: impl FnOnce(&mut State)) {
    let Some(state) = st.cast::<State>().as_mut() else {
        return;
    };
    if state.failed {
        return;
    }
    if catch_unwind(AssertUnwindSafe(|| f(state))).is_err() {
        state.failed = true;
    }
}

#[no_mangle]
pub extern "C" fn ocm_df_abi_version() -> u32 {
    guard(0, || ABI_VERSION)
}

/// Static NUL-terminated UTF-8 text.
#[no_mangle]
pub extern "C" fn ocm_df_description() -> *const c_char {
    guard(ptr::null(), || DESCRIPTION.as_ptr().cast())
}

/// Loads the embedded model; takes about 0.25 s. Returns null on any failure.
#[no_mangle]
pub extern "C" fn ocm_df_create() -> *mut c_void {
    guard(ptr::null_mut(), || {
        State::new().map_or(ptr::null_mut(), |state| Box::into_raw(Box::new(state)).cast())
    })
}

/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_sample_rate(st: *mut c_void) -> u32 {
    guard(0, || st.cast::<State>().as_ref().map_or(0, |state| state.df.sr as u32))
}

/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_frame_length(st: *mut c_void) -> usize {
    guard(0, || st.cast::<State>().as_ref().map_or(0, |state| state.df.hop_size))
}

/// Processes one frame. Returns 0 on success; non-zero leaves `output` undefined.
///
/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`; `input` and `output` are null or point to
/// `ocm_df_frame_length` floats and may be the same buffer.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_process(st: *mut c_void, input: *const f32, output: *mut f32) -> i32 {
    let Some(state) = st.cast::<State>().as_mut() else {
        return PROCESS_INVALID_ARGUMENT;
    };
    if input.is_null() || output.is_null() {
        return PROCESS_INVALID_ARGUMENT;
    }
    if state.failed {
        return PROCESS_FAILED_BEFORE;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        ptr::copy_nonoverlapping(input, state.input.as_mut_ptr(), FRAME_LENGTH);
        let result = state.process();
        if result == PROCESS_OK {
            ptr::copy_nonoverlapping(state.output.as_ptr(), output, FRAME_LENGTH);
        }
        result
    }))
    .unwrap_or(PROCESS_PANIC);
    if result != PROCESS_OK {
        state.failed = true;
    }
    result
}

/// Maximum noise reduction in dB; 100 or more is unlimited, below 0.01 passes the input through.
///
/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_set_atten_lim(st: *mut c_void, db: f32) {
    with_state(st, |state| {
        if db.is_finite() {
            state.df.set_atten_lim(db);
        }
    });
}

/// Post filter strength; 0 turns it off, libDF suggests up to 0.05.
///
/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_set_post_filter_beta(st: *mut c_void, beta: f32) {
    with_state(st, |state| {
        if beta.is_finite() {
            state.df.set_pf_beta(beta.max(0.0));
        }
    });
}

/// Local SNR thresholds in dB: below `min_db` the frame is treated as noise only, above `max_erb_db`
/// it is left as is, above `max_df_db` the deep filter stage is skipped.
///
/// # Safety
/// `st` is null or a live pointer from `ocm_df_create`.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_set_thresholds(st: *mut c_void, min_db: f32, max_erb_db: f32, max_df_db: f32) {
    with_state(st, |state| {
        if min_db.is_finite() && max_erb_db.is_finite() && max_df_db.is_finite() {
            state.df.min_db_thresh = min_db;
            state.df.max_db_erb_thresh = max_erb_db;
            state.df.max_db_df_thresh = max_df_db;
        }
    });
}

/// # Safety
/// `st` is null or a pointer from `ocm_df_create` that is not used afterwards.
#[no_mangle]
pub unsafe extern "C" fn ocm_df_free(st: *mut c_void) {
    if !st.is_null() {
        // A panic while dropping leaks the state instead of crossing into the caller.
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(st.cast::<State>()))));
    }
}
