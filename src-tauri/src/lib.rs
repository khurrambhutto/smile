mod camera;
mod frame_bus;
mod preview_server;

use std::path::PathBuf;
use std::sync::{atomic::Ordering, Arc, Mutex};

use tauri::Manager;

use camera::{CameraInfo, CameraRuntime, StartCameraRequest};
use frame_bus::FrameBus;
use preview_server::PreviewServer;

pub struct AppState {
    frame_bus: Arc<FrameBus>,
    preview_server: Mutex<Option<PreviewServer>>,
    runtime: Mutex<Option<CameraRuntime>>,
}

impl AppState {
    fn new() -> Self {
        Self {
            frame_bus: FrameBus::new(),
            preview_server: Mutex::new(None),
            runtime: Mutex::new(None),
        }
    }

    fn preview_url(&self) -> Result<String, String> {
        let guard = self
            .preview_server
            .lock()
            .map_err(|_| "Preview server state poisoned".to_string())?;
        match guard.as_ref() {
            Some(server) => Ok(server.preview_url()),
            None => Err("Preview server is not running".to_string()),
        }
    }
}

#[tauri::command]
fn list_cameras() -> Result<Vec<CameraInfo>, String> {
    camera::list_cameras()
}

#[tauri::command]
fn start_camera(
    app: tauri::AppHandle,
    state: tauri::State<'_, AppState>,
    request: Option<StartCameraRequest>,
) -> Result<Vec<CameraInfo>, String> {
    stop_active_runtime(&state)?;

    let cameras = camera::list_cameras()?;
    let selected = camera::select_camera(&cameras, request.as_ref())?;

    let runtime = camera::spawn_worker(
        app,
        Arc::clone(&state.frame_bus),
        selected.path.clone(),
        request.as_ref(),
    )?;

    let mut guard = state
        .runtime
        .lock()
        .map_err(|_| "Failed to acquire camera state".to_string())?;
    *guard = Some(runtime);

    Ok(cameras)
}

#[tauri::command]
fn stop_camera(state: tauri::State<'_, AppState>) -> Result<(), String> {
    stop_active_runtime(&state)
}

#[tauri::command]
fn get_preview_url(state: tauri::State<'_, AppState>) -> Result<String, String> {
    state.preview_url()
}

#[tauri::command]
fn capture_photo(state: tauri::State<'_, AppState>) -> Result<String, String> {
    let frame = state
        .frame_bus
        .latest()
        .ok_or("No frame available — is the camera running?")?;

    let home = std::env::var("HOME")
        .map(PathBuf::from)
        .map_err(|_| "Could not determine home directory".to_string())?;
    let save_dir = home.join("Pictures").join("Camera");
    std::fs::create_dir_all(&save_dir)
        .map_err(|e| format!("Failed to create directory: {e}"))?;

    let timestamp = chrono::Local::now().format("%Y%m%d_%H%M%S");
    let filename = format!("IMG_{timestamp}.jpg");
    let path = save_dir.join(&filename);

    std::fs::write(&path, frame.as_ref())
        .map_err(|e| format!("Failed to save photo: {e}"))?;

    Ok(path.to_string_lossy().to_string())
}

fn stop_active_runtime(state: &AppState) -> Result<(), String> {
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(AppState::new())
        .setup(|app| {
            let state = app.state::<AppState>();
            let server = PreviewServer::start(Arc::clone(&state.frame_bus))
                .map_err(|error| -> Box<dyn std::error::Error> { error.into() })?;

            *state
                .preview_server
                .lock()
                .map_err(|_| -> Box<dyn std::error::Error> {
                    "preview server mutex poisoned".into()
                })? = Some(server);

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            list_cameras,
            start_camera,
            stop_camera,
            get_preview_url,
            capture_photo,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
