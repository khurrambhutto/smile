use std::{
    io::Cursor,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    thread::{self, JoinHandle},
    time::Duration,
};

use image::{codecs::jpeg::JpegEncoder, ColorType, ImageEncoder};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};
use v4l::{
    buffer::Type,
    capability::Flags as CapabilityFlags,
    context,
    format::FourCC,
    frameinterval::FrameIntervalEnum,
    io::traits::CaptureStream,
    prelude::*,
    video::{capture::Parameters as CaptureParameters, Capture},
};

use crate::frame_bus::FrameBus;
use crate::recording::RecordingState;

const CAMERA_STATUS_EVENT: &str = "camera-status";
pub const DEFAULT_WIDTH: u32 = 1280;
pub const DEFAULT_HEIGHT: u32 = 720;
pub const DEFAULT_FPS: u32 = 30;

const STREAM_BUFFER_COUNT: u32 = 4;
const FIRST_FRAME_TIMEOUT_MS: u64 = 3_000;
const STREAM_TIMEOUT_MS: u64 = 1_000;
const MIN_ACCEPTABLE_FPS: u32 = 15;
const YUYV_JPEG_QUALITY: u8 = 95;

const FOURCC_MJPG: [u8; 4] = *b"MJPG";
const FOURCC_YUYV: [u8; 4] = *b"YUYV";

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CameraInfo {
    pub id: String,
    pub name: String,
    pub path: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CameraStatusPayload {
    state: String,
    message: String,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct StartCameraRequest {
    pub camera_id: Option<String>,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub fps: Option<u32>,
}

#[derive(Debug, Clone, Copy)]
struct SelectedMode {
    width: u32,
    height: u32,
    fourcc: FourCC,
    fps: u32,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveStreamInfo {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
    pub pixel_format: String,
}

pub struct CameraRuntime {
    pub stop_flag: Arc<AtomicBool>,
    pub handle: JoinHandle<()>,
}

pub fn list_cameras() -> Result<Vec<CameraInfo>, String> {
    let mut cameras = Vec::new();

    for node in context::enum_devices() {
        let path = node.path().to_string_lossy().to_string();
        let name = node
            .name()
            .filter(|value| !value.trim().is_empty())
            .unwrap_or_else(|| format!("Camera {}", node.index()));

        let Ok(device) = Device::with_path(&path) else {
            continue;
        };

        let Ok(caps) = device.query_caps() else {
            continue;
        };

        if !caps.capabilities.contains(CapabilityFlags::VIDEO_CAPTURE)
            || !caps.capabilities.contains(CapabilityFlags::STREAMING)
        {
            continue;
        }

        cameras.push(CameraInfo {
            id: path.clone(),
            name,
            path,
        });
    }

    cameras.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(cameras)
}

pub fn select_camera(
    cameras: &[CameraInfo],
    request: Option<&StartCameraRequest>,
) -> Result<CameraInfo, String> {
    if cameras.is_empty() {
        return Err("No camera found".to_string());
    }

    if let Some(request) = request {
        if let Some(camera_id) = &request.camera_id {
            if let Some(camera) = cameras.iter().find(|camera| &camera.id == camera_id) {
                return Ok(camera.clone());
            }

            return Err(format!("Camera not found: {camera_id}"));
        }
    }

    Ok(cameras[0].clone())
}

pub fn spawn_worker(
    app: AppHandle,
    bus: Arc<FrameBus>,
    recording: Arc<RecordingState>,
    active_stream: Arc<Mutex<Option<ActiveStreamInfo>>>,
    path: String,
    request: Option<&StartCameraRequest>,
) -> Result<CameraRuntime, String> {
    let want_width = request
        .and_then(|r| r.width)
        .unwrap_or(DEFAULT_WIDTH);
    let want_height = request
        .and_then(|r| r.height)
        .unwrap_or(DEFAULT_HEIGHT);
    let want_fps = request
        .and_then(|r| r.fps)
        .unwrap_or(DEFAULT_FPS)
        .max(1);

    emit_status(
        &app,
        "starting",
        format!("Opening {path} (requested {want_width}x{want_height} {want_fps}fps)"),
    );

    let stop_flag = Arc::new(AtomicBool::new(false));
    let worker_flag = Arc::clone(&stop_flag);
    let worker_app = app.clone();
    let worker_bus = Arc::clone(&bus);
    let worker_recording = Arc::clone(&recording);
    let worker_active_stream = Arc::clone(&active_stream);

    let handle = thread::Builder::new()
        .name("camera-capture".into())
        .spawn(move || {
            let result = run_capture_loop(
                &worker_app,
                &worker_bus,
                &worker_recording,
                &worker_active_stream,
                &path,
                want_width,
                want_height,
                want_fps,
                &worker_flag,
            );
            clear_active_stream(&worker_active_stream);
            worker_recording.stop_for_camera_shutdown(&worker_app);

            match result {
                Ok(()) => emit_status(&worker_app, "stopped", "Camera stopped".to_string()),
                Err(error) => emit_status(&worker_app, "error", error),
            }
        })
        .map_err(|error| format!("Failed to spawn capture thread: {error}"))?;

    Ok(CameraRuntime { stop_flag, handle })
}

fn run_capture_loop(
    app: &AppHandle,
    bus: &Arc<FrameBus>,
    recording: &Arc<RecordingState>,
    active_stream: &Arc<Mutex<Option<ActiveStreamInfo>>>,
    path: &str,
    want_width: u32,
    want_height: u32,
    want_fps: u32,
    stop_flag: &AtomicBool,
) -> Result<(), String> {
    let device =
        Device::with_path(path).map_err(|error| format!("Failed to open {path}: {error}"))?;

    let mode = select_mode(&device, want_width, want_height, want_fps)?;

    let mut fmt = device
        .format()
        .map_err(|error| format!("Failed to read camera format: {error}"))?;
    fmt.width = mode.width;
    fmt.height = mode.height;
    fmt.fourcc = mode.fourcc;

    let actual_fmt = device
        .set_format(&fmt)
        .map_err(|error| format!("Failed to configure camera: {error}"))?;

    let params = CaptureParameters::with_fps(mode.fps);
    let actual_params = device.set_params(&params);
    let actual_fps = actual_params
        .as_ref()
        .ok()
        .and_then(|p| {
            let tf = p.interval;
            if tf.numerator == 0 {
                None
            } else {
                Some(tf.denominator / tf.numerator.max(1))
            }
        })
        .unwrap_or(mode.fps);

    let pixel_format = actual_fmt
        .fourcc
        .str()
        .map(|value| value.to_string())
        .unwrap_or_else(|_| "UNKNOWN".to_string());

    emit_status(
        app,
        "running",
        format!(
            "Streaming {}x{} {} @ {}fps",
            actual_fmt.width, actual_fmt.height, pixel_format, actual_fps
        ),
    );
    update_active_stream(
        active_stream,
        ActiveStreamInfo {
            width: actual_fmt.width,
            height: actual_fmt.height,
            fps: actual_fps.max(1),
            pixel_format: pixel_format.clone(),
        },
    );

    let mut stream = MmapStream::with_buffers(&device, Type::VideoCapture, STREAM_BUFFER_COUNT)
        .map_err(|error| format!("Failed to create capture stream: {error}"))?;
    stream.set_timeout(Duration::from_millis(FIRST_FRAME_TIMEOUT_MS));

    // Preallocate RGB scratch so the YUYV fallback doesn't reallocate on
    // every frame. MJPG passes through and never uses this buffer.
    let mut rgb_scratch: Vec<u8> = Vec::new();

    let mut first_frame = true;

    loop {
        if stop_flag.load(Ordering::Relaxed) {
            break;
        }

        let (buf, meta) = match stream.next() {
            Ok(frame) => frame,
            Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {
                if first_frame {
                    return Err(format!(
                        "Camera did not produce a first frame within {FIRST_FRAME_TIMEOUT_MS}ms"
                    ));
                }
                continue;
            }
            Err(error) => {
                return Err(format!("Failed to capture frame: {error}"));
            }
        };

        let used = usize::try_from(meta.bytesused)
            .map_err(|_| "Failed to read frame size".to_string())?;

        if used == 0 || used > buf.len() {
            continue;
        }

        let frame = &buf[..used];
        let jpeg = encode_frame_as_jpeg(
            frame,
            actual_fmt.width,
            actual_fmt.height,
            actual_fmt.fourcc,
            &mut rgb_scratch,
        )?;

        recording.write_frame(app, &jpeg);
        bus.publish(Arc::new(jpeg));

        if first_frame {
            // Relax the timeout once the stream is confirmed live.
            stream.set_timeout(Duration::from_millis(STREAM_TIMEOUT_MS));
            first_frame = false;
        }
    }

    Ok(())
}

fn update_active_stream(
    active_stream: &Arc<Mutex<Option<ActiveStreamInfo>>>,
    info: ActiveStreamInfo,
) {
    if let Ok(mut guard) = active_stream.lock() {
        *guard = Some(info);
    }
}

fn clear_active_stream(active_stream: &Arc<Mutex<Option<ActiveStreamInfo>>>) {
    if let Ok(mut guard) = active_stream.lock() {
        *guard = None;
    }
}

fn encode_frame_as_jpeg(
    frame: &[u8],
    width: u32,
    height: u32,
    fourcc: FourCC,
    rgb_scratch: &mut Vec<u8>,
) -> Result<Vec<u8>, String> {
    if fourcc == FourCC::new(b"MJPG") || fourcc == FourCC::new(b"JPEG") {
        // MJPG is already a JPEG-compatible stream; ship it as-is.
        return Ok(frame.to_vec());
    }

    if fourcc == FourCC::new(b"YUYV") {
        yuyv_to_rgb(frame, width, height, rgb_scratch)?;
        let mut jpeg = Vec::with_capacity(rgb_scratch.len() / 4);
        encode_rgb_as_jpeg(rgb_scratch, width, height, &mut jpeg)?;
        return Ok(jpeg);
    }

    Err(format!(
        "Unsupported pixel format: {}",
        fourcc
            .str()
            .map(|value| value.to_string())
            .unwrap_or_else(|_| "UNKNOWN".to_string())
    ))
}

// -----------------------------------------------------------------------------
// Mode selection
// -----------------------------------------------------------------------------

fn select_mode(
    device: &Device,
    want_width: u32,
    want_height: u32,
    want_fps: u32,
) -> Result<SelectedMode, String> {
    let formats = device
        .enum_formats()
        .map_err(|error| format!("Failed to enumerate camera formats: {error}"))?;

    let target_aspect = want_width as f32 / want_height.max(1) as f32;
    let preferred: [(FourCC, f32); 2] = [
        (FourCC::new(&FOURCC_MJPG), 0.0),
        (FourCC::new(&FOURCC_YUYV), 1500.0),
    ];

    let mut candidates: Vec<(SelectedMode, f32, bool)> = Vec::new();

    for (fourcc, fourcc_penalty) in preferred {
        if !formats.iter().any(|f| f.fourcc == fourcc) {
            continue;
        }

        let sizes = match device.enum_framesizes(fourcc) {
            Ok(sizes) => sizes,
            Err(_) => continue,
        };

        for frame_size in sizes {
            let discretes: Vec<_> = frame_size.size.to_discrete().into_iter().collect();
            for d in discretes {
                let (w, h) = (d.width, d.height);
                if w == 0 || h == 0 {
                    continue;
                }

                let intervals = match device.enum_frameintervals(fourcc, w, h) {
                    Ok(v) => v,
                    Err(_) => continue,
                };

                let fps_options = collect_fps_options(&intervals);
                if fps_options.is_empty() {
                    continue;
                }

                let max_fps = *fps_options.iter().max().unwrap_or(&0);
                let meets_target = fps_options.iter().any(|f| *f >= want_fps);
                let chosen_fps = if meets_target {
                    // Smallest fps that is >= the target, then fall back to
                    // max if none are listed above the target.
                    fps_options
                        .iter()
                        .copied()
                        .filter(|f| *f >= want_fps)
                        .min()
                        .unwrap_or(max_fps)
                } else {
                    max_fps
                };

                if chosen_fps < MIN_ACCEPTABLE_FPS {
                    continue;
                }

                let score =
                    score_candidate(w, h, want_width, want_height, target_aspect) + fourcc_penalty;

                candidates.push((
                    SelectedMode {
                        width: w,
                        height: h,
                        fourcc,
                        fps: chosen_fps,
                    },
                    score,
                    meets_target,
                ));
            }
        }
    }

    if candidates.is_empty() {
        return Err(
            "No supported camera mode found (need MJPG or YUYV with >=15fps)".to_string(),
        );
    }

    // Prefer modes that actually meet the requested fps; within that group,
    // pick the lowest score. Fall back to any acceptable mode otherwise.
    candidates.sort_by(|a, b| {
        let group_a = if a.2 { 0 } else { 1 };
        let group_b = if b.2 { 0 } else { 1 };
        group_a.cmp(&group_b).then(
            a.1.partial_cmp(&b.1)
                .unwrap_or(std::cmp::Ordering::Equal),
        )
    });

    Ok(candidates.remove(0).0)
}

fn score_candidate(
    w: u32,
    h: u32,
    want_w: u32,
    want_h: u32,
    target_aspect: f32,
) -> f32 {
    let aspect = w as f32 / h.max(1) as f32;
    let aspect_penalty = (aspect - target_aspect).abs() * 2000.0;

    let dw = w as f32 - want_w as f32;
    let dh = h as f32 - want_h as f32;

    // Penalize going over the requested size slightly more than going under,
    // so we don't pick 1920x1080 when the user asked for 1280x720 and both
    // are supported at the same fps.
    let size_penalty = if w > want_w || h > want_h {
        (dw.abs() + dh.abs()) * 1.5
    } else {
        dw.abs() + dh.abs()
    };

    aspect_penalty + size_penalty
}

fn collect_fps_options(intervals: &[v4l::frameinterval::FrameInterval]) -> Vec<u32> {
    let mut out = Vec::new();

    for iv in intervals {
        match &iv.interval {
            FrameIntervalEnum::Discrete(frac) => {
                if let Some(fps) = fraction_to_fps(frac.numerator, frac.denominator) {
                    out.push(fps);
                }
            }
            FrameIntervalEnum::Stepwise(stepwise) => {
                // A stepwise range in V4L2 reports *time per frame*, so the
                // `min` fraction yields the *highest* fps and `max` the
                // lowest. We approximate the range with its extremes plus a
                // mid-point; the common target fps values usually fall on
                // those endpoints in practice.
                if let Some(max_fps) = fraction_to_fps(stepwise.min.numerator, stepwise.min.denominator) {
                    out.push(max_fps);
                }
                if let Some(min_fps) = fraction_to_fps(stepwise.max.numerator, stepwise.max.denominator) {
                    out.push(min_fps);
                }
            }
        }
    }

    out.sort_unstable();
    out.dedup();
    out
}

fn fraction_to_fps(numerator: u32, denominator: u32) -> Option<u32> {
    if numerator == 0 {
        return None;
    }
    Some(denominator / numerator)
}

// -----------------------------------------------------------------------------
// YUYV -> RGB -> JPEG fallback
// -----------------------------------------------------------------------------

fn yuyv_to_rgb(
    buffer: &[u8],
    width: u32,
    height: u32,
    rgb_out: &mut Vec<u8>,
) -> Result<(), String> {
    let expected_len = (width as usize) * (height as usize) * 2;
    if buffer.len() < expected_len {
        return Err(format!(
            "Frame buffer too small for YUYV: expected at least {expected_len}, got {}",
            buffer.len()
        ));
    }

    let rgb_len = (width as usize) * (height as usize) * 3;
    rgb_out.clear();
    rgb_out.reserve(rgb_len);

    // Integer approximation of BT.601 YUV->RGB. Avoids f32 work + per-pixel
    // heap growth that the original implementation had.
    for chunk in buffer[..expected_len].chunks_exact(4) {
        let y0 = chunk[0] as i32;
        let u = chunk[1] as i32 - 128;
        let y1 = chunk[2] as i32;
        let v = chunk[3] as i32 - 128;

        push_rgb_pixel(rgb_out, y0, u, v);
        push_rgb_pixel(rgb_out, y1, u, v);
    }

    Ok(())
}

fn push_rgb_pixel(out: &mut Vec<u8>, y: i32, u: i32, v: i32) {
    // Coefficients scaled by 256 for cheap fixed-point math.
    let r = y + ((359 * v) >> 8);
    let g = y - ((88 * u + 183 * v) >> 8);
    let b = y + ((454 * u) >> 8);

    out.push(clamp_u8(r));
    out.push(clamp_u8(g));
    out.push(clamp_u8(b));
}

fn clamp_u8(v: i32) -> u8 {
    v.clamp(0, 255) as u8
}

fn encode_rgb_as_jpeg(
    rgb: &[u8],
    width: u32,
    height: u32,
    jpeg_out: &mut Vec<u8>,
) -> Result<(), String> {
    jpeg_out.clear();
    let mut cursor = Cursor::new(jpeg_out);
    let encoder = JpegEncoder::new_with_quality(&mut cursor, YUYV_JPEG_QUALITY);
    encoder
        .write_image(rgb, width, height, ColorType::Rgb8.into())
        .map_err(|error| error.to_string())?;
    Ok(())
}

fn emit_status(app: &AppHandle, state: &str, message: String) {
    let payload = CameraStatusPayload {
        state: state.to_string(),
        message,
    };
    let _ = app.emit(CAMERA_STATUS_EVENT, payload);
}
