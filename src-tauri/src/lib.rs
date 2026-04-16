use std::{
    io::Cursor,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc, Mutex,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use image::{codecs::jpeg::JpegEncoder, ColorType, ImageEncoder};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};
use v4l::{
    buffer::Type,
    capability::Flags as CapabilityFlags,
    context,
    format::FourCC,
    io::traits::CaptureStream,
    prelude::*,
    video::{capture::Parameters as CaptureParameters, Capture},
};

const CAMERA_FRAME_EVENT: &str = "camera-frame";
const CAMERA_STATUS_EVENT: &str = "camera-status";
const DEFAULT_WIDTH: u32 = 1280;
const DEFAULT_HEIGHT: u32 = 720;
const DEFAULT_FPS: u32 = 30;
const STREAM_BUFFER_COUNT: u32 = 4;
const FIRST_FRAME_TIMEOUT_MS: u64 = 3_000;
const STREAM_TIMEOUT_MS: u64 = 1_000;
const PREVIEW_FRAME_INTERVAL_MS: u64 = 33;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CameraInfo {
    id: String,
    name: String,
    path: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CameraFramePayload {
    data_url: String,
    width: u32,
    height: u32,
    pixel_format: String,
    sequence: u32,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct CameraStatusPayload {
    state: String,
    message: String,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
struct StartCameraRequest {
    camera_id: Option<String>,
    width: Option<u32>,
    height: Option<u32>,
    fps: Option<u32>,
}

#[derive(Debug)]
struct CameraRuntime {
    stop_flag: Arc<AtomicBool>,
    handle: JoinHandle<()>,
}

#[derive(Default)]
struct CameraState {
    runtime: Mutex<Option<CameraRuntime>>,
}

#[tauri::command]
fn list_cameras() -> Result<Vec<CameraInfo>, String> {
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

#[tauri::command]
fn start_camera(
    app: AppHandle,
    state: tauri::State<'_, CameraState>,
    request: Option<StartCameraRequest>,
) -> Result<Vec<CameraInfo>, String> {
    stop_active_runtime(&state)?;

    let cameras = list_cameras()?;
    let selected = select_camera(&cameras, request.as_ref())?;

    let width = request
        .as_ref()
        .and_then(|value| value.width)
        .unwrap_or(DEFAULT_WIDTH);
    let height = request
        .as_ref()
        .and_then(|value| value.height)
        .unwrap_or(DEFAULT_HEIGHT);
    let fps = request
        .as_ref()
        .and_then(|value| value.fps)
        .unwrap_or(DEFAULT_FPS)
        .max(1);

    emit_status(
        &app,
        "starting",
        format!(
            "Opening {} at {}x{} {}fps",
            selected.name, width, height, fps
        ),
    );

    let stop_flag = Arc::new(AtomicBool::new(false));
    let worker_flag = Arc::clone(&stop_flag);
    let worker_app = app.clone();
    let worker_path = selected.path.clone();

    let handle = thread::spawn(move || {
        if let Err(error) = camera_worker(
            worker_app.clone(),
            worker_path,
            width,
            height,
            fps,
            worker_flag,
        ) {
            emit_status(&worker_app, "error", error);
        } else {
            emit_status(&worker_app, "stopped", "Camera stopped".to_string());
        }
    });

    let mut runtime = state
        .runtime
        .lock()
        .map_err(|_| "Failed to acquire camera state".to_string())?;
    *runtime = Some(CameraRuntime { stop_flag, handle });

    Ok(cameras)
}

#[tauri::command]
fn stop_camera(state: tauri::State<'_, CameraState>) -> Result<(), String> {
    stop_active_runtime(&state)
}

fn stop_active_runtime(state: &CameraState) -> Result<(), String> {
    let runtime = {
        let mut guard = state
            .runtime
            .lock()
            .map_err(|_| "Failed to acquire camera state".to_string())?;
        guard.take()
    };

    if let Some(runtime) = runtime {
        runtime.stop_flag.store(true, Ordering::Relaxed);
        runtime
            .handle
            .join()
            .map_err(|_| "Camera worker panicked".to_string())?;
    }

    Ok(())
}

fn select_camera(
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

            return Err(format!("Camera not found: {}", camera_id));
        }
    }

    Ok(cameras[0].clone())
}

fn camera_worker(
    app: AppHandle,
    path: String,
    requested_width: u32,
    requested_height: u32,
    requested_fps: u32,
    stop_flag: Arc<AtomicBool>,
) -> Result<(), String> {
    let dev =
        Device::with_path(&path).map_err(|error| format!("Failed to open {}: {}", path, error))?;

    let mut fmt = dev
        .format()
        .map_err(|error| format!("Failed to read camera format: {}", error))?;

    fmt.width = requested_width;
    fmt.height = requested_height;
    fmt.fourcc = FourCC::new(b"MJPG");

    let actual_fmt = match dev.set_format(&fmt) {
        Ok(format) => format,
        Err(_) => {
            let mut fallback = dev
                .format()
                .map_err(|error| format!("Failed to read camera format: {}", error))?;

            fallback.width = requested_width;
            fallback.height = requested_height;
            fallback.fourcc = FourCC::new(b"YUYV");

            dev.set_format(&fallback)
                .map_err(|error| format!("Failed to configure camera: {}", error))?
        }
    };

    let params = CaptureParameters::with_fps(requested_fps);
    let _ = dev.set_params(&params);

    let pixel_format = actual_fmt
        .fourcc
        .str()
        .map(|value| value.to_string())
        .unwrap_or_else(|_| "UNKNOWN".to_string());

    emit_status(
        &app,
        "running",
        format!(
            "Streaming {}x{} {}",
            actual_fmt.width, actual_fmt.height, pixel_format
        ),
    );

    let mut stream = MmapStream::with_buffers(&dev, Type::VideoCapture, STREAM_BUFFER_COUNT)
        .map_err(|error| format!("Failed to create capture stream: {}", error))?;
    stream.set_timeout(Duration::from_millis(FIRST_FRAME_TIMEOUT_MS));

    let mut last_emit = Instant::now()
        .checked_sub(Duration::from_millis(PREVIEW_FRAME_INTERVAL_MS))
        .unwrap_or_else(Instant::now);
    {
        let (first_buf, first_meta) = match stream.next() {
            Ok(frame) => frame,
            Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {
                return Err(format!(
                    "Camera did not produce a first frame within {} ms",
                    FIRST_FRAME_TIMEOUT_MS
                ));
            }
            Err(error) => {
                return Err(format!("Failed to capture first frame: {}", error));
            }
        };

        let used = usize::try_from(first_meta.bytesused)
            .map_err(|_| "Failed to read first frame size".to_string())?;

        if used > 0 && used <= first_buf.len() {
            let frame = &first_buf[..used];
            let data_url = frame_to_data_url(
                frame,
                actual_fmt.width,
                actual_fmt.height,
                actual_fmt.fourcc,
            )
            .map_err(|error| format!("Failed to encode preview frame: {}", error))?;

            let payload = CameraFramePayload {
                data_url,
                width: actual_fmt.width,
                height: actual_fmt.height,
                pixel_format: pixel_format.clone(),
                sequence: first_meta.sequence,
            };

            if let Err(error) = app.emit(CAMERA_FRAME_EVENT, &payload) {
                return Err(format!("Failed to emit frame event: {}", error));
            }

            last_emit = Instant::now();
        }
    }

    stream.set_timeout(Duration::from_millis(STREAM_TIMEOUT_MS));

    loop {
        if stop_flag.load(Ordering::Relaxed) {
            break;
        }

        let (buf, meta) = match stream.next() {
            Ok(frame) => frame,
            Err(error) if error.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(error) => {
                return Err(format!("Failed to capture frame: {}", error));
            }
        };

        let used =
            usize::try_from(meta.bytesused).map_err(|_| "Failed to read frame size".to_string())?;

        if used == 0 || used > buf.len() {
            continue;
        }

        if last_emit.elapsed() < Duration::from_millis(PREVIEW_FRAME_INTERVAL_MS) {
            continue;
        }
        last_emit = Instant::now();

        let frame = &buf[..used];
        let data_url = frame_to_data_url(
            frame,
            actual_fmt.width,
            actual_fmt.height,
            actual_fmt.fourcc,
        )
        .map_err(|error| format!("Failed to encode preview frame: {}", error))?;

        let payload = CameraFramePayload {
            data_url,
            width: actual_fmt.width,
            height: actual_fmt.height,
            pixel_format: pixel_format.clone(),
            sequence: meta.sequence,
        };

        if let Err(error) = app.emit(CAMERA_FRAME_EVENT, &payload) {
            return Err(format!("Failed to emit frame event: {}", error));
        }
    }

    Ok(())
}

fn frame_to_data_url(
    buffer: &[u8],
    width: u32,
    height: u32,
    fourcc: FourCC,
) -> Result<String, String> {
    if fourcc == FourCC::new(b"MJPG") || fourcc == FourCC::new(b"JPEG") {
        return Ok(format!("data:image/jpeg;base64,{}", BASE64.encode(buffer)));
    }

    if fourcc == FourCC::new(b"YUYV") {
        let rgb = yuyv_to_rgb(buffer, width, height)?;
        let jpeg = encode_rgb_as_jpeg(&rgb, width, height)?;
        return Ok(format!("data:image/jpeg;base64,{}", BASE64.encode(jpeg)));
    }

    Err(format!(
        "Unsupported pixel format: {}",
        fourcc
            .str()
            .map(|value| value.to_string())
            .unwrap_or_else(|_| "UNKNOWN".to_string())
    ))
}

fn yuyv_to_rgb(buffer: &[u8], width: u32, height: u32) -> Result<Vec<u8>, String> {
    let expected_len = (width as usize) * (height as usize) * 2;
    if buffer.len() < expected_len {
        return Err(format!(
            "Frame buffer too small for YUYV: expected at least {}, got {}",
            expected_len,
            buffer.len()
        ));
    }

    let mut rgb = Vec::with_capacity((width as usize) * (height as usize) * 3);

    for chunk in buffer[..expected_len].chunks_exact(4) {
        let y0 = chunk[0] as f32;
        let u = chunk[1] as f32 - 128.0;
        let y1 = chunk[2] as f32;
        let v = chunk[3] as f32 - 128.0;

        push_rgb_pixel(&mut rgb, y0, u, v);
        push_rgb_pixel(&mut rgb, y1, u, v);
    }

    Ok(rgb)
}

fn push_rgb_pixel(output: &mut Vec<u8>, y: f32, u: f32, v: f32) {
    let r = clamp_to_u8(y + 1.402 * v);
    let g = clamp_to_u8(y - 0.344_136 * u - 0.714_136 * v);
    let b = clamp_to_u8(y + 1.772 * u);

    output.push(r);
    output.push(g);
    output.push(b);
}

fn clamp_to_u8(value: f32) -> u8 {
    value.round().clamp(0.0, 255.0) as u8
}

fn encode_rgb_as_jpeg(rgb: &[u8], width: u32, height: u32) -> Result<Vec<u8>, String> {
    let mut output = Vec::new();
    let mut cursor = Cursor::new(&mut output);
    let encoder = JpegEncoder::new_with_quality(&mut cursor, 70);

    encoder
        .write_image(rgb, width, height, ColorType::Rgb8.into())
        .map_err(|error| error.to_string())?;

    Ok(output)
}

fn emit_status(app: &AppHandle, state: &str, message: String) {
    let payload = CameraStatusPayload {
        state: state.to_string(),
        message,
    };

    let _ = app.emit(CAMERA_STATUS_EVENT, payload);
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(CameraState::default())
        .invoke_handler(tauri::generate_handler![
            list_cameras,
            start_camera,
            stop_camera
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
