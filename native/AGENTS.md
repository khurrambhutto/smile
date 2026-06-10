# AGENTS.md - My Linux Camera App

**Project Name**: Smile
**Goal**: Build a fast, lightweight, high-quality native camera application for Linux using **Qt 6**.

## Project Objectives

- Excellent performance and low memory usage
- Highest possible image and video quality
- Native look and feel across GNOME (Ubuntu/Debian) and KDE (Fedora)
- Modern, clean, and intuitive UI
- Easy installation via `.deb`, `.rpm`, and Flatpak
- Cross-distro compatibility

---

## Development Phases

### Phase 1: Research & Analysis (Current)

**Task**:
1. Clone the two main competitor projects:
   - [QtCAM](https://github.com/econsysqtcam/qtcam)
   - [Webcamoid](https://github.com/webcamoid/webcamoid)

2. Analyze their architecture, especially:
   - Camera initialization and format selection
   - Live preview implementation
   - High-quality photo capture
   - Video recording pipeline
   - QML structure and UI patterns
   - Performance optimizations
   - Code organization and modularity

3. Document **best practices** and **lessons learned** in this file (update AGENTS.md).

**Deliverable**: Updated `AGENTS.md` with key findings, recommended architecture, and decisions.

---

### Phase 2: Basic Image Capture (High Quality + Speed)

**Goals**:
- Smooth live preview using `VideoOutput`
- Camera selection + highest available resolution/format
- High-quality still photo capture
- Fast capture with minimal latency
- Clean, modern native UI (QML + Qt Quick Controls)
- Proper error handling

**Success Criteria**:
- Fast startup with live preview
- Highest quality photo capture
- Low CPU/RAM usage

---

### Phase 3: Video Recording

**Goals**:
- Start/stop high-quality video recording
- Support for H.264 / H.265 with best settings
- Audio recording
- Recording status and duration
- Proper MP4 output

---

### Phase 4+: Future Features

- Real-time filters and effects
- Manual camera controls (exposure, focus, etc.)
- Multi-camera support
- Settings window
- Packaging (`.deb`, `.rpm`, Flatpak)

---

## Tech Stack

- **Framework**: Qt 6.5+
- **UI**: QML + Qt Quick + Qt Quick Controls
- **Language**: C++17/20
- **Build System**: CMake
- **Multimedia**: Qt Multimedia

---

## Coding Standards & Agent Rules

- **Modularity is mandatory** — Never put all code in a single `main.cpp` or one big file.
- Break the code into clean, logical modules/classes:
  - `CameraManager` (core camera logic)
  - `ImageCaptureHandler`
  - `VideoRecorder`
  - `DeviceManager` (camera enumeration)
  - UI components in separate QML files
  - Models / Helpers as needed
- Follow clean code principles:
  - Single Responsibility Principle
  - Proper separation of backend (C++) and frontend (QML)
  - Clear naming conventions
  - Good documentation for important classes and functions
- Use modern C++ practices and Qt best practices
- Keep UI code clean and maintainable in QML

---

## Agent Instructions

- Always prioritize **performance**, **code quality**, and **modularity**.
- Keep UI modern, minimal, and native-feeling.
- After completing each phase, update this `AGENTS.md` with summary, decisions, and next steps.
- Document important architectural choices here.
- When in doubt, choose simplicity and reliability.

---

## Phase 1 Research Findings

### Source Repositories Reviewed

- `qtcam/` at commit `ae61259` (`Added new pid for SEE3CAM_130E_H02R1 and SEE3CAM_130E_H03R1 variants of See3CAM_130E.`)
- `webcamoid/` at commit `a36859406` (`Added a simple slider with presets to select the recording quality.`)

Both local clones were clean at the time of this review.

### QtCAM Findings

QtCAM is most useful as a low-level V4L2 reference, not as an application architecture to copy.

- V4L2 access is direct and explicit through `src/v4l2-api.h` plus `src/videostreaming.cpp`.
- Capture startup uses the standard production V4L2 flow: request mmap buffers, query/map buffers, queue all buffers, then `VIDIOC_STREAMON`.
- Frame handling validates buffer sizes by pixel format before using the frame. This is important for real webcams because MJPEG/YUYV/Y16/Y12/GREY cameras can report or emit bad frames.
- Still capture sometimes switches resolution/format, skips warm-up frames, then restores preview settings. This is useful for high-quality photos when the best still format differs from the preview format.
- Recording is isolated behind `VideoEncoder`, using FFmpeg/libavcodec and audio capture through PulseAudio.
- Rendering uses OpenGL shader paths for packed formats such as UYVY/Y8 so preview conversion does not always happen on the CPU.
- The biggest weakness is code organization: `Videostreaming` is very large and contains device-specific behavior, still capture logic, preview logic, and recording coordination in one place.

Lessons for this project:

- Keep the V4L2 lifecycle explicit and testable.
- Prefer mmap streaming for preview and capture.
- Validate frame size/format before preview, photo, or recording use.
- Support MJPEG and YUYV first, then add other formats behind focused converters.
- Do not copy QtCAM's monolithic structure or large camera-specific branching into core app code.

### Webcamoid Findings

Webcamoid is most useful as a modularity and QML structure reference.

- Capture implementations live behind plugin-style interfaces under `libAvKys/Plugins/VideoCapture`.
- Linux V4L2 capture is isolated in `capture/v4l2sys/src/capturev4l2.cpp`.
- Capability discovery enumerates V4L2 formats, frame sizes, and frame intervals, then maps them into structured video capability objects.
- Format selection prefers compressed formats first, then raw formats, and moves a nearest default resolution to the front for startup.
- V4L2 initialization sets device format, FPS, I/O method, buffers, stream state, and cleanup in a contained backend class.
- The camera loop is separate from the UI and emits packets downstream; compressed camera packets can be decoded on a conversion path instead of forcing capture to always output RGB.
- QML screens are split by responsibility: `main.qml`, `ImageCapture.qml`, `VideoRecording.qml`, device options, output options, plugin config, dialogs, and reusable controls.
- Recording settings expose format, quality, bitrate, GOP, audio, and destination as UI state instead of hardcoding one recording mode.

Lessons for this project:

- Use a narrow backend interface between QML and camera internals.
- Keep device enumeration, capability selection, preview streaming, still capture, and recording in separate modules.
- Store camera capability data in a structured model that the QML UI can render without knowing V4L2 details.
- Keep capture and conversion decoupled so MJPEG can stay compressed until preview decode or recording requires conversion.
- Provide simple presets first, with advanced controls hidden behind a separate settings view.

## Architecture Decisions For This App

### Stack Decision

Build Phase 2 as a Qt 6.5+ CMake app using C++17/20, QML, Qt Quick Controls, and a native Linux V4L2 backend.

Do not use browser `getUserMedia()` for camera access. The app must talk to Linux cameras through the native backend.

### Recommended Module Layout

Use this structure when implementation starts:

```text
src/
  main.cpp
  app/
    AppController.{h,cpp}
    SettingsStore.{h,cpp}
  camera/
    CameraDevice.{h,cpp}
    CameraFormat.{h,cpp}
    DeviceManager.{h,cpp}
    CameraManager.{h,cpp}
    V4L2CaptureSession.{h,cpp}
    V4L2Controls.{h,cpp}
    FrameBuffer.{h,cpp}
    FrameConverter.{h,cpp}
  capture/
    ImageCaptureHandler.{h,cpp}
    VideoRecorder.{h,cpp}
    EncoderSettings.{h,cpp}
  models/
    CameraDeviceModel.{h,cpp}
    CameraFormatModel.{h,cpp}
  qml/
    Main.qml
    components/
      HeaderBar.qml
      CameraPreview.qml
      CaptureControls.qml
      DeviceSelector.qml
      SettingsDialog.qml
      FormatSelector.qml
      RecordingIndicator.qml
```

### Backend Responsibilities

- `DeviceManager`: enumerate `/dev/video*`, query capabilities, expose user-friendly names, and watch for hotplug changes.
- `CameraManager`: own selected device state, selected format, preview lifecycle, errors, and QML-facing commands.
- `V4L2CaptureSession`: implement open/query/set format/request buffers/mmap/qbuf/dqbuf/streamon/streamoff/cleanup.
- `FrameConverter`: convert only where needed. Prefer GPU/QVideoFrame-friendly preview paths later; start with reliable MJPEG/YUYV conversion.
- `ImageCaptureHandler`: capture at the selected best still format, skip unstable initial frames when switching modes, save high-quality images.
- `VideoRecorder`: encode MP4 with H.264 first, H.265 later if available; keep audio and video timing explicit.
- `SettingsStore`: persist selected camera, preferred resolution/FPS, image format/quality, video quality preset, and save paths.

### Capability Selection Policy

- Enumerate all formats using `VIDIOC_ENUM_FMT`, `VIDIOC_ENUM_FRAMESIZES`, and `VIDIOC_ENUM_FRAMEINTERVALS`.
- Prefer a stable preview format in this order for Phase 2: MJPEG at high resolution/FPS, then YUYV.
- Prefer highest photo resolution available, even if capture must temporarily switch from preview settings.
- Prefer 30 FPS for default startup unless the selected resolution can reliably do 60 FPS without high CPU usage.
- Treat camera-reported data as untrusted: validate dimensions, bytes used, and pixel format before processing frames.

### Preview Policy

- Keep preview responsive before adding heavy features.
- Do not perform expensive per-frame software transforms in the hot preview path unless unavoidable.
- If orientation, mirroring, or rotation is required, first check V4L2 hardware controls; otherwise apply it in the cheapest display layer possible for preview and separately in saved media.
- Use bounded buffers and drop stale preview frames instead of letting latency accumulate.

### Photo Quality Policy

- Photo capture should use the best still resolution/format, not simply a scaled preview frame.
- If switching camera format for still capture, pause preview, switch format, skip warm-up frames, capture, save, then restore preview.
- Save JPEG with explicit quality settings and keep PNG available for lossless output.
- Keep EXIF/orientation handling explicit so preview, photos, and videos match visually.

### Video Recording Policy

- Phase 3 should use an encoder abstraction rather than putting FFmpeg/GStreamer calls inside camera code.
- Start with MP4 + H.264 + AAC/Opus depending on available system libraries.
- Keep bitrate, FPS, GOP, and audio settings in `EncoderSettings`.
- Avoid reducing source resolution silently; if the camera or encoder cannot support a requested mode, show a clear error and offer a lower preset.

### UI Decisions

- Use QML components that mirror GNOME-style app structure: header bar, full-bleed preview, compact bottom capture controls, settings dialog, device/format selectors.
- Keep QML declarative and thin; business logic belongs in C++ backend classes and models.
- Use simple defaults on the main screen and move advanced controls into settings.
- Expose camera errors and permission/device issues clearly without blocking the whole app when another camera is available.

## Phase 1 Summary

Phase 1 is complete enough to move into Phase 2.

Key decisions:

- Use Webcamoid's modular separation as the main architecture influence.
- Use QtCAM's V4L2 lifecycle, frame validation, still-capture mode switching, and encoder separation as low-level implementation guidance.
- Avoid QtCAM-style monolithic camera classes.
- Keep preview speed, saved photo/video quality, and orientation consistency as non-negotiable quality targets.

## Phase 2 Next Steps

1. Scaffold the CMake/Qt/QML project if not already present.
2. Implement `DeviceManager` and `CameraDeviceModel` first.
3. Implement `V4L2CaptureSession` with MJPEG/YUYV capability enumeration and mmap streaming.
4. Render a live preview in QML through a focused `CameraPreview` component.
5. Add `ImageCaptureHandler` after preview is stable, using highest-resolution still capture.
6. Document Phase 2 implementation decisions here after the first working preview/photo path.

## Phase 2 Implementation Progress

Initial Phase 2 scaffolding is now in place.

Implemented modules:

- Root CMake/Qt project with a `smile` executable.
- `CameraDevice`, `CameraFormat`, `DeviceManager`, `CameraDeviceModel`, and `CameraFormatModel`.
- `V4L2CaptureSession` with explicit open/query/set-format/request-buffers/mmap/qbuf/dqbuf/streamon/streamoff cleanup flow.
- MJPEG and YUYV support for preview, with frame-size validation before conversion.
- `FrameConverter` for MJPEG decode through Qt image plugins and YUYV-to-RGB conversion.
- `CameraManager` as the narrow QML-facing backend bridge.
- QML UI split into `Main.qml` plus focused components for header, preview, selectors, capture controls, settings, and recording indicator placeholder.
- `ImageCaptureHandler` that saves JPEG captures at quality 95 under `Pictures/Smile`.

Current Phase 2 behavior:

- Enumerates compatible `/dev/video*` V4L2 devices that support video capture and streaming.
- Enumerates MJPEG/YUYV formats, frame sizes, and frame intervals.
- Sorts formats to prefer MJPEG first, then YUYV, with high resolution and 30+ FPS favored.
- Streams preview frames from V4L2 mmap buffers into QML `VideoOutput` through a `QVideoSink` bridge.
- Drops stale queued camera frames by draining bounded batches and presenting the newest decoded frame per notifier activation.
- Allows camera and format selection from the header bar.
- Restarts preview when the selected camera or format changes.
- Captures the latest full camera frame to JPEG without scaling the displayed preview.

Implementation notes and decisions:

- The documented target remains Qt 6.5+, but local validation was performed against Qt 6.4.2 because that is the installed system Qt version. The current code avoids APIs unavailable in Qt 6.4, such as `QQmlApplicationEngine::loadFromModule` and `QVideoFrame(QImage)`.
- Preview currently uses a CPU copy from `QImage` into `QVideoFrame`. This is reliable for Phase 2 and keeps the UI path simple; later optimization should move toward zero-copy or GPU-friendly frame paths where possible.
- Still capture currently saves the latest decoded full-resolution selected-format frame. The next quality improvement is dedicated still-mode switching: pause preview, switch to the highest still format, skip warm-up frames, capture, save, then restore preview.
- Video recording classes are scaffolded only; encoding remains Phase 3.
- Runtime smoke testing requires the Qt 6 Quick Controls QML plugin (`QtQuick.Controls` for Qt 6). The local machine only had the Qt 5 controls plugin installed, so GUI launch could not complete until that runtime package is installed.

Updated Phase 2 next steps:

1. Install/verify Qt 6 Quick Controls runtime QML plugins on the development machine.
2. Test live preview with real webcams across MJPEG and YUYV modes.
3. Add dedicated highest-resolution still-capture mode switching with warm-up frame skipping.
4. Add hotplug monitoring and better camera permission diagnostics.
5. Add lightweight automated tests for format scoring and frame conversion.

---

**Current Phase**: Phase 2 (Basic Image Capture - High Quality + Speed)

---

*Last Updated*: June 5, 2026
