use std::{
    fs::File,
    io::{self, BufWriter, Seek, SeekFrom, Write},
    path::PathBuf,
    sync::Mutex,
};

use serde::Serialize;
use tauri::{AppHandle, Emitter};

pub const RECORDING_STATUS_EVENT: &str = "recording-status";

const AVIIF_KEYFRAME: u32 = 0x10;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RecordingStatus {
    pub state: String,
    pub message: String,
    pub path: Option<String>,
    pub is_recording: bool,
}

#[derive(Debug, Clone, Copy)]
pub struct RecordingConfig {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
}

pub struct RecordingState {
    inner: Mutex<RecordingInner>,
}

struct RecordingInner {
    session: Option<RecordingSession>,
    last_path: Option<String>,
    state: String,
    message: String,
}

struct RecordingSession {
    writer: AviMjpegWriter<BufWriter<File>>,
    path: PathBuf,
}

impl RecordingState {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(RecordingInner {
                session: None,
                last_path: None,
                state: "idle".to_string(),
                message: "Ready to record".to_string(),
            }),
        }
    }

    pub fn snapshot(&self) -> RecordingStatus {
        let guard = self.inner.lock().expect("recording state poisoned");
        snapshot_from_inner(&guard)
    }

    pub fn start(&self, app: &AppHandle, config: RecordingConfig) -> Result<RecordingStatus, String> {
        let path = build_output_path()?;
        let file = File::create(&path)
            .map_err(|error| format!("Failed to create video file {}: {error}", path.display()))?;
        let writer = AviMjpegWriter::create(
            BufWriter::new(file),
            config.width,
            config.height,
            config.fps.max(1),
        )
        .map_err(|error| format!("Failed to initialize video writer: {error}"))?;

        let snapshot = {
            let mut guard = self
                .inner
                .lock()
                .map_err(|_| "Failed to acquire recording state".to_string())?;

            if guard.session.is_some() {
                return Err("Recording is already in progress".to_string());
            }

            let path_str = path.to_string_lossy().to_string();
            guard.last_path = Some(path_str.clone());
            guard.state = "recording".to_string();
            guard.message = format!(
                "Recording {}x{} @ {}fps",
                config.width,
                config.height,
                config.fps.max(1)
            );
            guard.session = Some(RecordingSession { writer, path });
            snapshot_from_inner(&guard)
        };

        emit_status(app, snapshot.clone());
        Ok(snapshot)
    }

    pub fn stop(&self, app: &AppHandle) -> Result<RecordingStatus, String> {
        let snapshot = {
            let mut guard = self
                .inner
                .lock()
                .map_err(|_| "Failed to acquire recording state".to_string())?;

            let Some(session) = guard.session.take() else {
                return Err("Recording is not active".to_string());
            };

            let path = session.path.to_string_lossy().to_string();
            session
                .writer
                .finish()
                .map_err(|error| format!("Failed to finalize recording: {error}"))?;

            guard.last_path = Some(path.clone());
            guard.state = "idle".to_string();
            guard.message = format!("Saved video to {path}");
            snapshot_from_inner(&guard)
        };

        emit_status(app, snapshot.clone());
        Ok(snapshot)
    }

    pub fn stop_for_camera_shutdown(&self, app: &AppHandle) {
        let snapshot = {
            let mut guard = match self.inner.lock() {
                Ok(guard) => guard,
                Err(_) => return,
            };

            let Some(session) = guard.session.take() else {
                return;
            };

            let path = session.path.to_string_lossy().to_string();
            let _ = session.writer.finish();
            guard.last_path = Some(path.clone());
            guard.state = "idle".to_string();
            guard.message = format!("Saved video to {path}");
            snapshot_from_inner(&guard)
        };

        emit_status(app, snapshot);
    }

    pub fn fail_active(&self, app: &AppHandle, message: String) {
        let snapshot = {
            let mut guard = match self.inner.lock() {
                Ok(guard) => guard,
                Err(_) => return,
            };

            let Some(session) = guard.session.take() else {
                return;
            };

            let path = session.path.to_string_lossy().to_string();
            let _ = session.writer.finish();
            guard.last_path = Some(path);
            guard.state = "error".to_string();
            guard.message = message;
            snapshot_from_inner(&guard)
        };

        emit_status(app, snapshot);
    }

    pub fn write_frame(&self, app: &AppHandle, jpeg: &[u8]) {
        let maybe_error = {
            let mut guard = match self.inner.lock() {
                Ok(guard) => guard,
                Err(_) => return,
            };

            let Some(session) = guard.session.as_mut() else {
                return;
            };

            session
                .writer
                .write_frame(jpeg)
                .err()
                .map(|error| error.to_string())
        };

        if let Some(error) = maybe_error {
            self.fail_active(app, format!("Recording stopped: {error}"));
        }
    }
}

fn snapshot_from_inner(inner: &RecordingInner) -> RecordingStatus {
    RecordingStatus {
        state: inner.state.clone(),
        message: inner.message.clone(),
        path: inner.last_path.clone(),
        is_recording: inner.session.is_some(),
    }
}

fn emit_status(app: &AppHandle, status: RecordingStatus) {
    let _ = app.emit(RECORDING_STATUS_EVENT, status);
}

fn build_output_path() -> Result<PathBuf, String> {
    let home = std::env::var("HOME")
        .map(PathBuf::from)
        .map_err(|_| "Could not determine home directory".to_string())?;
    let save_dir = home.join("Videos").join("Camera");
    std::fs::create_dir_all(&save_dir)
        .map_err(|error| format!("Failed to create directory {}: {error}", save_dir.display()))?;

    let timestamp = chrono::Local::now().format("%Y%m%d_%H%M%S");
    Ok(save_dir.join(format!("VID_{timestamp}.avi")))
}

struct AviMjpegWriter<W: Write + Seek> {
    inner: W,
    riff_size_pos: u64,
    movi_list_start: u64,
    movi_size_pos: u64,
    movi_tag_start: u64,
    frame_count: u32,
    max_frame_size: u32,
    index_entries: Vec<IndexEntry>,
}

#[derive(Clone, Copy)]
struct IndexEntry {
    offset: u32,
    size: u32,
}

impl<W: Write + Seek> AviMjpegWriter<W> {
    fn create(mut inner: W, width: u32, height: u32, fps: u32) -> io::Result<Self> {
        inner.write_all(b"RIFF")?;
        let riff_size_pos = inner.stream_position()?;
        write_u32_le(&mut inner, 0)?;
        inner.write_all(b"AVI ")?;

        let hdrl = build_hdrl_list(width, height, fps.max(1));
        inner.write_all(&hdrl)?;

        let movi_list_start = inner.stream_position()?;
        inner.write_all(b"LIST")?;
        let movi_size_pos = inner.stream_position()?;
        write_u32_le(&mut inner, 0)?;
        let movi_tag_start = inner.stream_position()?;
        inner.write_all(b"movi")?;

        Ok(Self {
            inner,
            riff_size_pos,
            movi_list_start,
            movi_size_pos,
            movi_tag_start,
            frame_count: 0,
            max_frame_size: 0,
            index_entries: Vec::new(),
        })
    }

    fn write_frame(&mut self, jpeg: &[u8]) -> io::Result<()> {
        let chunk_start = self.inner.stream_position()?;
        self.inner.write_all(b"00dc")?;
        write_u32_le(&mut self.inner, jpeg.len() as u32)?;
        self.inner.write_all(jpeg)?;
        write_padding(&mut self.inner, jpeg.len())?;

        let offset = chunk_start
            .checked_sub(self.movi_tag_start)
            .ok_or_else(|| io::Error::other("Invalid AVI chunk offset"))?;

        self.index_entries.push(IndexEntry {
            offset: offset as u32,
            size: jpeg.len() as u32,
        });
        self.frame_count = self.frame_count.saturating_add(1);
        self.max_frame_size = self.max_frame_size.max(jpeg.len() as u32);
        Ok(())
    }

    fn finish(mut self) -> io::Result<()> {
        let movi_end = self.inner.stream_position()?;
        self.write_idx1()?;
        let file_end = self.inner.stream_position()?;

        let riff_size = file_end
            .checked_sub(8)
            .ok_or_else(|| io::Error::other("Invalid RIFF size"))?;
        let movi_size = movi_end
            .checked_sub(self.movi_list_start + 8)
            .ok_or_else(|| io::Error::other("Invalid movi size"))?;

        self.inner.seek(SeekFrom::Start(self.riff_size_pos))?;
        write_u32_le(&mut self.inner, riff_size as u32)?;
        self.inner.seek(SeekFrom::Start(self.movi_size_pos))?;
        write_u32_le(&mut self.inner, movi_size as u32)?;

        patch_header_field(&mut self.inner, 48, self.frame_count)?;
        patch_header_field(&mut self.inner, 140, self.frame_count)?;
        patch_header_field(&mut self.inner, 56, self.max_frame_size)?;
        patch_header_field(&mut self.inner, 144, self.max_frame_size)?;

        self.inner.seek(SeekFrom::Start(file_end))?;
        self.inner.flush()
    }

    fn write_idx1(&mut self) -> io::Result<()> {
        self.inner.write_all(b"idx1")?;
        write_u32_le(&mut self.inner, (self.index_entries.len() * 16) as u32)?;

        for entry in &self.index_entries {
            self.inner.write_all(b"00dc")?;
            write_u32_le(&mut self.inner, AVIIF_KEYFRAME)?;
            write_u32_le(&mut self.inner, entry.offset)?;
            write_u32_le(&mut self.inner, entry.size)?;
        }

        Ok(())
    }
}

fn build_hdrl_list(width: u32, height: u32, fps: u32) -> Vec<u8> {
    let mut hdrl_payload = Vec::new();
    hdrl_payload.extend(build_avih_chunk(width, height, fps));
    hdrl_payload.extend(build_strl_list(width, height, fps));
    build_list_chunk(*b"hdrl", hdrl_payload)
}

fn build_strl_list(width: u32, height: u32, fps: u32) -> Vec<u8> {
    let mut payload = Vec::new();
    payload.extend(build_strh_chunk(width, height, fps));
    payload.extend(build_strf_chunk(width, height));
    build_list_chunk(*b"strl", payload)
}

fn build_avih_chunk(width: u32, height: u32, fps: u32) -> Vec<u8> {
    let mut payload = Vec::with_capacity(56);
    let microseconds_per_frame = 1_000_000u32 / fps.max(1);
    push_u32_le(&mut payload, microseconds_per_frame);
    push_u32_le(&mut payload, width.saturating_mul(height).saturating_mul(3).saturating_mul(fps));
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0x10);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 1);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, width.saturating_mul(height).saturating_mul(3));
    push_u32_le(&mut payload, width);
    push_u32_le(&mut payload, height);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    build_chunk(*b"avih", payload)
}

fn build_strh_chunk(width: u32, height: u32, fps: u32) -> Vec<u8> {
    let mut payload = Vec::with_capacity(56);
    payload.extend_from_slice(b"vids");
    payload.extend_from_slice(b"MJPG");
    push_u32_le(&mut payload, 0);
    push_u16_le(&mut payload, 0);
    push_u16_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 1);
    push_u32_le(&mut payload, fps.max(1));
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, width.saturating_mul(height).saturating_mul(3));
    push_u32_le(&mut payload, u32::MAX);
    push_u32_le(&mut payload, 0);
    push_i16_le(&mut payload, 0);
    push_i16_le(&mut payload, 0);
    push_i16_le(&mut payload, width.min(i16::MAX as u32) as i16);
    push_i16_le(&mut payload, height.min(i16::MAX as u32) as i16);
    build_chunk(*b"strh", payload)
}

fn build_strf_chunk(width: u32, height: u32) -> Vec<u8> {
    let mut payload = Vec::with_capacity(40);
    push_u32_le(&mut payload, 40);
    push_i32_le(&mut payload, width.min(i32::MAX as u32) as i32);
    push_i32_le(&mut payload, height.min(i32::MAX as u32) as i32);
    push_u16_le(&mut payload, 1);
    push_u16_le(&mut payload, 24);
    payload.extend_from_slice(b"MJPG");
    push_u32_le(&mut payload, width.saturating_mul(height).saturating_mul(3));
    push_i32_le(&mut payload, 0);
    push_i32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    push_u32_le(&mut payload, 0);
    build_chunk(*b"strf", payload)
}

fn build_list_chunk(list_type: [u8; 4], mut payload: Vec<u8>) -> Vec<u8> {
    let mut chunk = Vec::with_capacity(payload.len() + 12);
    chunk.extend_from_slice(b"LIST");
    push_u32_le(&mut chunk, (payload.len() + 4) as u32);
    chunk.extend_from_slice(&list_type);
    chunk.append(&mut payload);
    if chunk.len() % 2 != 0 {
        chunk.push(0);
    }
    chunk
}

fn build_chunk(chunk_id: [u8; 4], mut payload: Vec<u8>) -> Vec<u8> {
    let mut chunk = Vec::with_capacity(payload.len() + 8);
    chunk.extend_from_slice(&chunk_id);
    push_u32_le(&mut chunk, payload.len() as u32);
    chunk.append(&mut payload);
    if chunk.len() % 2 != 0 {
        chunk.push(0);
    }
    chunk
}

fn patch_header_field<W: Write + Seek>(writer: &mut W, offset: u64, value: u32) -> io::Result<()> {
    writer.seek(SeekFrom::Start(offset))?;
    write_u32_le(writer, value)
}

fn write_padding<W: Write>(writer: &mut W, data_len: usize) -> io::Result<()> {
    let padding = (4 - (data_len % 4)) % 4;
    if padding == 0 {
        return Ok(());
    }

    writer.write_all(&[0; 3][..padding])
}

fn write_u32_le<W: Write>(writer: &mut W, value: u32) -> io::Result<()> {
    writer.write_all(&value.to_le_bytes())
}

fn push_u32_le(buf: &mut Vec<u8>, value: u32) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn push_u16_le(buf: &mut Vec<u8>, value: u16) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn push_i32_le(buf: &mut Vec<u8>, value: i32) {
    buf.extend_from_slice(&value.to_le_bytes());
}

fn push_i16_le(buf: &mut Vec<u8>, value: i16) {
    buf.extend_from_slice(&value.to_le_bytes());
}
