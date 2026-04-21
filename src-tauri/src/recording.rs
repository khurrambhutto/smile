use std::{
    fs::File,
    io::{self, BufWriter, Read, Seek, SeekFrom, Write},
    path::PathBuf,
    process::{Child, Command, Stdio},
    sync::{Arc, Mutex},
    thread::{self, JoinHandle},
    time::Duration,
};

use serde::Serialize;
use tauri::{AppHandle, Emitter};

pub const RECORDING_STATUS_EVENT: &str = "recording-status";

const AVIIF_KEYFRAME: u32 = 0x10;
const AVIF_HASINDEX: u32 = 0x10;
const WAVE_FORMAT_PCM: u16 = 0x0001;
const AUDIO_CHUNK_TARGET_BYTES: usize = 16 * 1024;

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

#[derive(Debug, Clone, Copy)]
struct VideoFormat {
    width: u32,
    height: u32,
    fps: u32,
}

#[derive(Debug, Clone, Copy)]
struct AudioFormat {
    channels: u16,
    sample_rate: u32,
    bits_per_sample: u16,
}

impl AudioFormat {
    fn block_align(self) -> u16 {
        self.channels.saturating_mul(self.bits_per_sample / 8)
    }

    fn avg_bytes_per_sec(self) -> u32 {
        self.sample_rate
            .saturating_mul(u32::from(self.block_align()))
    }
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
    writer: AviMuxWriter<BufWriter<File>>,
    path: PathBuf,
    audio_buffer: Arc<Mutex<Vec<u8>>>,
    audio_runtime: AudioCaptureRuntime,
}

struct AudioCaptureRuntime {
    child: Child,
    reader_thread: Option<JoinHandle<()>>,
    format: AudioFormat,
    error: Arc<Mutex<Option<String>>>,
}

#[derive(Clone, Copy)]
struct IndexEntry {
    chunk_id: [u8; 4],
    flags: u32,
    offset: u32,
    size: u32,
}

struct AviMuxWriter<W: Write + Seek> {
    inner: W,
    audio: AudioFormat,
    riff_size_pos: u64,
    movi_list_start: u64,
    movi_size_pos: u64,
    movi_tag_start: u64,
    avih_total_frames_pos: u64,
    avih_streams_pos: u64,
    avih_suggested_buffer_pos: u64,
    video_length_pos: u64,
    video_suggested_buffer_pos: u64,
    audio_length_pos: u64,
    audio_suggested_buffer_pos: u64,
    frame_count: u32,
    total_audio_bytes: u32,
    max_video_frame_size: u32,
    max_audio_chunk_size: u32,
    index_entries: Vec<IndexEntry>,
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
        let session = RecordingSession::start(path.clone(), config)?;

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
                "Recording {}x{} @ {}fps with audio",
                config.width,
                config.height,
                config.fps.max(1)
            );
            guard.session = Some(session);
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
            session.finish()?;

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
            let _ = session.finish();
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
            let _ = session.finish();
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

            session.write_frame(jpeg).err()
        };

        if let Some(error) = maybe_error {
            self.fail_active(app, format!("Recording stopped: {error}"));
        }
    }
}

impl RecordingSession {
    fn start(path: PathBuf, config: RecordingConfig) -> Result<Self, String> {
        let file = File::create(&path)
            .map_err(|error| format!("Failed to create video file {}: {error}", path.display()))?;
        let audio_buffer = Arc::new(Mutex::new(Vec::with_capacity(AUDIO_CHUNK_TARGET_BYTES * 2)));
        let audio_runtime = AudioCaptureRuntime::start(Arc::clone(&audio_buffer))?;
        let writer = AviMuxWriter::create(
            BufWriter::new(file),
            VideoFormat {
                width: config.width,
                height: config.height,
                fps: config.fps.max(1),
            },
            audio_runtime.format(),
        )
        .map_err(|error| format!("Failed to initialize recording writer: {error}"))?;

        Ok(Self {
            writer,
            path,
            audio_buffer,
            audio_runtime,
        })
    }

    fn write_frame(&mut self, jpeg: &[u8]) -> Result<(), String> {
        self.flush_audio(false)?;
        self.writer
            .write_video_frame(jpeg)
            .map_err(|error| format!("Failed to write video frame: {error}"))
    }

    fn finish(self) -> Result<(), String> {
        let RecordingSession {
            mut writer,
            path: _,
            audio_buffer,
            audio_runtime,
        } = self;

        if let Some(error) = audio_runtime.peek_error() {
            return Err(format!("Microphone capture failed: {error}"));
        }

        audio_runtime.stop()?;
        flush_audio_buffer(&audio_buffer, &mut writer, true)?;
        writer
            .finish()
            .map_err(|error| format!("Failed to finalize recording: {error}"))
    }

    fn flush_audio(&mut self, force: bool) -> Result<(), String> {
        if let Some(error) = self.audio_runtime.take_error() {
            return Err(format!("Microphone capture failed: {error}"));
        }

        flush_audio_buffer(&self.audio_buffer, &mut self.writer, force)
    }
}

impl AudioCaptureRuntime {
    fn start(buffer: Arc<Mutex<Vec<u8>>>) -> Result<Self, String> {
        let format = AudioFormat {
            channels: 2,
            sample_rate: 48_000,
            bits_per_sample: 16,
        };

        Self::start_arecord(Arc::clone(&buffer), format).or_else(|alsa_error| {
            Self::start_pw_record(buffer, format).map_err(|pipewire_error| {
                format!(
                    "Failed to start audio capture. arecord: {alsa_error}. pw-record: {pipewire_error}"
                )
            })
        })
    }

    fn format(&self) -> AudioFormat {
        self.format
    }

    fn take_error(&self) -> Option<String> {
        self.error.lock().ok().and_then(|mut guard| guard.take())
    }

    fn peek_error(&self) -> Option<String> {
        self.error.lock().ok().and_then(|guard| guard.clone())
    }

    fn stop(mut self) -> Result<(), String> {
        let _ = self.child.kill();
        let _ = self.child.wait();

        if let Some(thread) = self.reader_thread.take() {
            thread
                .join()
                .map_err(|_| "Audio capture reader thread panicked".to_string())?;
        }

        if let Some(error) = self.take_error() {
            return Err(format!("Audio capture failed: {error}"));
        }

        Ok(())
    }

    fn start_arecord(buffer: Arc<Mutex<Vec<u8>>>, format: AudioFormat) -> Result<Self, String> {
        Self::spawn_capture_process(
            "arecord",
            &["-q", "-t", "raw", "-f", "S16_LE", "-r", "48000", "-c", "2", "-"],
            buffer,
            format,
        )
    }

    fn start_pw_record(buffer: Arc<Mutex<Vec<u8>>>, format: AudioFormat) -> Result<Self, String> {
        Self::spawn_capture_process(
            "pw-record",
            &[
                "--target",
                "0",
                "--rate",
                "48000",
                "--channels",
                "2",
                "--format",
                "s16",
                "-",
            ],
            buffer,
            format,
        )
    }

    fn spawn_capture_process(
        command: &str,
        args: &[&str],
        buffer: Arc<Mutex<Vec<u8>>>,
        format: AudioFormat,
    ) -> Result<Self, String> {
        let error = Arc::new(Mutex::new(None));
        let mut child = Command::new(command)
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .map_err(|error| format!("Could not launch {command}: {error}"))?;

        let stdout = child
            .stdout
            .take()
            .ok_or_else(|| format!("Could not capture {command} stdout"))?;

        let reader_error = Arc::clone(&error);
        let reader_thread = thread::Builder::new()
            .name(format!("audio-{command}"))
            .spawn(move || read_audio_stream(stdout, buffer, reader_error))
            .map_err(|error| format!("Failed to start audio reader for {command}: {error}"))?;

        thread::sleep(Duration::from_millis(150));
        if let Some(status) = child
            .try_wait()
            .map_err(|error| format!("Failed to check {command} status: {error}"))?
        {
            let _ = reader_thread.join();
            return Err(format!("{command} exited immediately with status {status}"));
        }

        Ok(Self {
            child,
            reader_thread: Some(reader_thread),
            format,
            error,
        })
    }
}

impl<W: Write + Seek> AviMuxWriter<W> {
    fn create(mut inner: W, video: VideoFormat, audio: AudioFormat) -> io::Result<Self> {
        inner.write_all(b"RIFF")?;
        let riff_size_pos = inner.stream_position()?;
        write_u32_le(&mut inner, 0)?;
        inner.write_all(b"AVI ")?;

        let hdrl_list_start = start_list(&mut inner, *b"hdrl")?;
        let (avih_total_frames_pos, avih_streams_pos, avih_suggested_buffer_pos) =
            write_avih_chunk(&mut inner, video, audio)?;
        let (video_length_pos, video_suggested_buffer_pos) = write_video_strl(&mut inner, video)?;
        let (audio_length_pos, audio_suggested_buffer_pos) = write_audio_strl(&mut inner, audio)?;
        finish_list(&mut inner, hdrl_list_start)?;

        let movi_list_start = start_list(&mut inner, *b"movi")?;
        let movi_size_pos = movi_list_start + 4;
        let movi_tag_start = movi_size_pos + 4;

        Ok(Self {
            inner,
            audio,
            riff_size_pos,
            movi_list_start,
            movi_size_pos,
            movi_tag_start,
            avih_total_frames_pos,
            avih_streams_pos,
            avih_suggested_buffer_pos,
            video_length_pos,
            video_suggested_buffer_pos,
            audio_length_pos,
            audio_suggested_buffer_pos,
            frame_count: 0,
            total_audio_bytes: 0,
            max_video_frame_size: 0,
            max_audio_chunk_size: 0,
            index_entries: Vec::new(),
        })
    }

    fn write_video_frame(&mut self, jpeg: &[u8]) -> io::Result<()> {
        let chunk_start = self.write_chunk(*b"00dc", jpeg)?;
        self.index_entries.push(IndexEntry {
            chunk_id: *b"00dc",
            flags: AVIIF_KEYFRAME,
            offset: self.chunk_offset(chunk_start)?,
            size: jpeg.len() as u32,
        });
        self.frame_count = self.frame_count.saturating_add(1);
        self.max_video_frame_size = self.max_video_frame_size.max(jpeg.len() as u32);
        Ok(())
    }

    fn write_audio_chunk(&mut self, pcm: &[u8]) -> io::Result<()> {
        if pcm.is_empty() {
            return Ok(());
        }

        let chunk_start = self.write_chunk(*b"01wb", pcm)?;
        self.index_entries.push(IndexEntry {
            chunk_id: *b"01wb",
            flags: 0,
            offset: self.chunk_offset(chunk_start)?,
            size: pcm.len() as u32,
        });
        self.total_audio_bytes = self.total_audio_bytes.saturating_add(pcm.len() as u32);
        self.max_audio_chunk_size = self.max_audio_chunk_size.max(pcm.len() as u32);
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
        let audio_length = if self.audio.block_align() == 0 {
            0
        } else {
            self.total_audio_bytes / u32::from(self.audio.block_align())
        };
        let suggested_buffer = self.max_video_frame_size.max(self.max_audio_chunk_size);

        self.inner.seek(SeekFrom::Start(self.riff_size_pos))?;
        write_u32_le(&mut self.inner, riff_size as u32)?;
        self.inner.seek(SeekFrom::Start(self.movi_size_pos))?;
        write_u32_le(&mut self.inner, movi_size as u32)?;
        patch_header_field(&mut self.inner, self.avih_total_frames_pos, self.frame_count)?;
        patch_header_field(&mut self.inner, self.avih_streams_pos, 2)?;
        patch_header_field(&mut self.inner, self.avih_suggested_buffer_pos, suggested_buffer)?;
        patch_header_field(&mut self.inner, self.video_length_pos, self.frame_count)?;
        patch_header_field(
            &mut self.inner,
            self.video_suggested_buffer_pos,
            self.max_video_frame_size,
        )?;
        patch_header_field(&mut self.inner, self.audio_length_pos, audio_length)?;
        patch_header_field(
            &mut self.inner,
            self.audio_suggested_buffer_pos,
            self.max_audio_chunk_size.max(u32::from(self.audio.block_align())),
        )?;

        self.inner.seek(SeekFrom::Start(file_end))?;
        self.inner.flush()
    }

    fn write_idx1(&mut self) -> io::Result<()> {
        self.inner.write_all(b"idx1")?;
        write_u32_le(&mut self.inner, (self.index_entries.len() * 16) as u32)?;

        for entry in &self.index_entries {
            self.inner.write_all(&entry.chunk_id)?;
            write_u32_le(&mut self.inner, entry.flags)?;
            write_u32_le(&mut self.inner, entry.offset)?;
            write_u32_le(&mut self.inner, entry.size)?;
        }

        Ok(())
    }

    fn write_chunk(&mut self, chunk_id: [u8; 4], bytes: &[u8]) -> io::Result<u64> {
        let chunk_start = self.inner.stream_position()?;
        self.inner.write_all(&chunk_id)?;
        write_u32_le(&mut self.inner, bytes.len() as u32)?;
        self.inner.write_all(bytes)?;
        write_padding(&mut self.inner, bytes.len())?;
        Ok(chunk_start)
    }

    fn chunk_offset(&self, chunk_start: u64) -> io::Result<u32> {
        chunk_start
            .checked_sub(self.movi_tag_start)
            .and_then(|offset| u32::try_from(offset).ok())
            .ok_or_else(|| io::Error::other("Invalid AVI chunk offset"))
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

fn flush_audio_buffer<W: Write + Seek>(
    audio_buffer: &Arc<Mutex<Vec<u8>>>,
    writer: &mut AviMuxWriter<W>,
    force: bool,
) -> Result<(), String> {
    let bytes = {
        let mut guard = audio_buffer
            .lock()
            .map_err(|_| "Audio buffer state poisoned".to_string())?;
        if guard.is_empty() || (!force && guard.len() < AUDIO_CHUNK_TARGET_BYTES) {
            return Ok(());
        }
        std::mem::take(&mut *guard)
    };

    writer
        .write_audio_chunk(&bytes)
        .map_err(|error| format!("Failed to write audio chunk: {error}"))
}

fn with_audio_bytes(
    buffer: &Arc<Mutex<Vec<u8>>>,
    reserve: usize,
    f: impl FnOnce(&mut Vec<u8>),
) {
    if let Ok(mut guard) = buffer.lock() {
        guard.reserve(reserve);
        f(&mut guard);
    }
}

fn read_audio_stream(
    mut stdout: impl Read,
    buffer: Arc<Mutex<Vec<u8>>>,
    error: Arc<Mutex<Option<String>>>,
) {
    let mut chunk = [0_u8; 8192];

    loop {
        match stdout.read(&mut chunk) {
            Ok(0) => break,
            Ok(read) => with_audio_bytes(&buffer, read, |bytes| bytes.extend_from_slice(&chunk[..read])),
            Err(err) => {
                if let Ok(mut guard) = error.lock() {
                    *guard = Some(err.to_string());
                }
                break;
            }
        }
    }
}

fn write_avih_chunk<W: Write + Seek>(
    writer: &mut W,
    video: VideoFormat,
    audio: AudioFormat,
) -> io::Result<(u64, u64, u64)> {
    writer.write_all(b"avih")?;
    write_u32_le(writer, 56)?;
    write_u32_le(writer, 1_000_000 / video.fps.max(1))?;
    write_u32_le(
        writer,
        video
            .width
            .saturating_mul(video.height)
            .saturating_mul(3)
            .saturating_mul(video.fps)
            .saturating_add(audio.avg_bytes_per_sec()),
    )?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, AVIF_HASINDEX)?;
    let total_frames_pos = writer.stream_position()?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    let streams_pos = writer.stream_position()?;
    write_u32_le(writer, 2)?;
    let suggested_buffer_pos = writer.stream_position()?;
    write_u32_le(
        writer,
        video
            .width
            .saturating_mul(video.height)
            .saturating_mul(3)
            .max(u32::from(audio.block_align())),
    )?;
    write_u32_le(writer, video.width)?;
    write_u32_le(writer, video.height)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    Ok((total_frames_pos, streams_pos, suggested_buffer_pos))
}

fn write_video_strl<W: Write + Seek>(writer: &mut W, video: VideoFormat) -> io::Result<(u64, u64)> {
    let list_start = start_list(writer, *b"strl")?;

    writer.write_all(b"strh")?;
    write_u32_le(writer, 56)?;
    writer.write_all(b"vids")?;
    writer.write_all(b"MJPG")?;
    write_u32_le(writer, 0)?;
    write_u16_le(writer, 0)?;
    write_u16_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 1)?;
    write_u32_le(writer, video.fps)?;
    write_u32_le(writer, 0)?;
    let length_pos = writer.stream_position()?;
    write_u32_le(writer, 0)?;
    let suggested_buffer_pos = writer.stream_position()?;
    write_u32_le(
        writer,
        video
            .width
            .saturating_mul(video.height)
            .saturating_mul(3),
    )?;
    write_u32_le(writer, u32::MAX)?;
    write_u32_le(writer, 0)?;
    write_i16_le(writer, 0)?;
    write_i16_le(writer, 0)?;
    write_i16_le(writer, video.width.min(i16::MAX as u32) as i16)?;
    write_i16_le(writer, video.height.min(i16::MAX as u32) as i16)?;

    writer.write_all(b"strf")?;
    write_u32_le(writer, 40)?;
    write_u32_le(writer, 40)?;
    write_i32_le(writer, video.width.min(i32::MAX as u32) as i32)?;
    write_i32_le(writer, video.height.min(i32::MAX as u32) as i32)?;
    write_u16_le(writer, 1)?;
    write_u16_le(writer, 24)?;
    writer.write_all(b"MJPG")?;
    write_u32_le(
        writer,
        video
            .width
            .saturating_mul(video.height)
            .saturating_mul(3),
    )?;
    write_i32_le(writer, 0)?;
    write_i32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;

    finish_list(writer, list_start)?;
    Ok((length_pos, suggested_buffer_pos))
}

fn write_audio_strl<W: Write + Seek>(writer: &mut W, audio: AudioFormat) -> io::Result<(u64, u64)> {
    let list_start = start_list(writer, *b"strl")?;

    writer.write_all(b"strh")?;
    write_u32_le(writer, 56)?;
    writer.write_all(b"auds")?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, 0)?;
    write_u32_le(writer, u32::from(audio.block_align()))?;
    write_u32_le(writer, audio.avg_bytes_per_sec())?;
    write_u32_le(writer, 0)?;
    let length_pos = writer.stream_position()?;
    write_u32_le(writer, 0)?;
    let suggested_buffer_pos = writer.stream_position()?;
    write_u32_le(writer, u32::from(audio.block_align()))?;
    write_u32_le(writer, u32::MAX)?;
    write_u32_le(writer, u32::from(audio.block_align()))?;
    write_i16_le(writer, 0)?;
    write_i16_le(writer, 0)?;
    write_i16_le(writer, 0)?;
    write_i16_le(writer, 0)?;

    writer.write_all(b"strf")?;
    write_u32_le(writer, 18)?;
    write_u16_le(writer, WAVE_FORMAT_PCM)?;
    write_u16_le(writer, audio.channels)?;
    write_u32_le(writer, audio.sample_rate)?;
    write_u32_le(writer, audio.avg_bytes_per_sec())?;
    write_u16_le(writer, audio.block_align())?;
    write_u16_le(writer, audio.bits_per_sample)?;
    write_u16_le(writer, 0)?;

    finish_list(writer, list_start)?;
    Ok((length_pos, suggested_buffer_pos))
}

fn start_list<W: Write + Seek>(writer: &mut W, list_type: [u8; 4]) -> io::Result<u64> {
    let list_start = writer.stream_position()?;
    writer.write_all(b"LIST")?;
    write_u32_le(writer, 0)?;
    writer.write_all(&list_type)?;
    Ok(list_start)
}

fn finish_list<W: Write + Seek>(writer: &mut W, list_start: u64) -> io::Result<()> {
    let end = writer.stream_position()?;
    let size = end
        .checked_sub(list_start + 8)
        .ok_or_else(|| io::Error::other("Invalid LIST size"))?;
    writer.seek(SeekFrom::Start(list_start + 4))?;
    write_u32_le(writer, size as u32)?;
    writer.seek(SeekFrom::Start(end))?;
    Ok(())
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

fn write_u16_le<W: Write>(writer: &mut W, value: u16) -> io::Result<()> {
    writer.write_all(&value.to_le_bytes())
}

fn write_i32_le<W: Write>(writer: &mut W, value: i32) -> io::Result<()> {
    writer.write_all(&value.to_le_bytes())
}

fn write_i16_le<W: Write>(writer: &mut W, value: i16) -> io::Result<()> {
    writer.write_all(&value.to_le_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    #[test]
    fn audio_stream_list_has_expected_size() {
        let mut cursor = Cursor::new(Vec::new());
        let audio = AudioFormat {
            channels: 2,
            sample_rate: 48_000,
            bits_per_sample: 16,
        };

        let _ = write_audio_strl(&mut cursor, audio).expect("audio stream list should write");
        let bytes = cursor.into_inner();

        assert_eq!(&bytes[0..4], b"LIST");
        assert_eq!(&bytes[8..12], b"strl");
        assert_eq!(&bytes[12..16], b"strh");
        assert_eq!(u32::from_le_bytes(bytes[16..20].try_into().unwrap()), 56);
        assert_eq!(&bytes[76..80], b"strf");
        assert_eq!(bytes.len(), 102);
    }

    #[test]
    fn avi_header_places_width_and_height_correctly() {
        let mut cursor = Cursor::new(Vec::new());
        let video = VideoFormat {
            width: 1280,
            height: 720,
            fps: 30,
        };
        let audio = AudioFormat {
            channels: 2,
            sample_rate: 48_000,
            bits_per_sample: 16,
        };

        let _ = write_avih_chunk(&mut cursor, video, audio).expect("avih should write");
        let bytes = cursor.into_inner();

        assert_eq!(&bytes[0..4], b"avih");
        assert_eq!(u32::from_le_bytes(bytes[4..8].try_into().unwrap()), 56);
        assert_eq!(u32::from_le_bytes(bytes[32..36].try_into().unwrap()), 2);
        assert_eq!(u32::from_le_bytes(bytes[36..40].try_into().unwrap()), 1280 * 720 * 3);
        assert_eq!(u32::from_le_bytes(bytes[40..44].try_into().unwrap()), 1280);
        assert_eq!(u32::from_le_bytes(bytes[44..48].try_into().unwrap()), 720);
    }
}
