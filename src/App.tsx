import { useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import "./App.css";

type CameraInfo = {
  id: string;
  name: string;
  path: string;
};

type CameraStatusPayload = {
  state: string;
  message: string;
};

type RecordingStatusPayload = {
  state: string;
  message: string;
  path: string | null;
  isRecording: boolean;
};

type CaptureMode = "photo" | "video";

const CAMERA_STATUS_EVENT = "camera-status";
const RECORDING_STATUS_EVENT = "recording-status";
const RECONNECT_DELAY_MS = 750;

function App() {
  const [, setCameras] = useState<CameraInfo[]>([]);
  const [, setSelectedCameraId] = useState("");
  const [previewUrl, setPreviewUrl] = useState("");
  const [previewToken, setPreviewToken] = useState(0);
  const [status, setStatus] = useState("Starting camera…");
  const [statusState, setStatusState] = useState("starting");
  const [error, setError] = useState("");
  const [isRunning, setIsRunning] = useState(false);
  const [mode, setMode] = useState<CaptureMode>("photo");
  const [isRecording, setIsRecording] = useState(false);

  const autoStartedRef = useRef(false);
  const reconnectTimerRef = useRef<number | null>(null);

  useEffect(() => {
    let mounted = true;
    let unlistenStatus: UnlistenFn | null = null;
    let unlistenRecording: UnlistenFn | null = null;

    const setup = async () => {
      try {
        unlistenStatus = await listen<CameraStatusPayload>(
          CAMERA_STATUS_EVENT,
          (event) => {
            if (!mounted) return;

            const { state, message } = event.payload;
            setStatusState(state);
            setStatus(message);

            if (state === "running") {
              setIsRunning(true);
              setError("");
            } else if (state === "error") {
              setIsRunning(false);
              setError(message);
            } else if (state === "stopped") {
              setIsRunning(false);
            }
          },
        );

        unlistenRecording = await listen<RecordingStatusPayload>(
          RECORDING_STATUS_EVENT,
          (event) => {
            if (!mounted) return;
            applyRecordingStatus(event.payload);
          },
        );

        const url = await invoke<string>("get_preview_url");
        if (!mounted) return;
        setPreviewUrl(url);

        const recording = await invoke<RecordingStatusPayload>(
          "get_recording_status",
        );
        if (!mounted) return;
        applyRecordingStatus(recording);

        const found = await invoke<CameraInfo[]>("list_cameras");
        if (!mounted) return;

        setCameras(found);
        const firstCameraId = found[0]?.id ?? "";
        setSelectedCameraId(firstCameraId);

        if (!firstCameraId) {
          setStatus("No camera detected");
          setStatusState("empty");
          return;
        }

        if (!autoStartedRef.current) {
          autoStartedRef.current = true;
          await startCamera(firstCameraId);
        }
      } catch (err) {
        if (!mounted) return;
        setError(getErrorMessage(err));
        setStatus("Failed to initialize camera");
        setStatusState("error");
      }
    };

    void setup();

    return () => {
      mounted = false;
      if (unlistenStatus) void unlistenStatus();
      if (unlistenRecording) void unlistenRecording();
      if (reconnectTimerRef.current !== null) {
        window.clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
    };
  }, []);

  async function startCamera(cameraId: string) {
    try {
      setError("");
      setStatus("Starting camera…");
      setStatusState("starting");

      const updated = await invoke<CameraInfo[]>("start_camera", {
        request: {
          cameraId,
          width: 1280,
          height: 720,
          fps: 30,
        },
      });

      setCameras(updated);
      if (updated.some((camera) => camera.id === cameraId)) {
        setSelectedCameraId(cameraId);
      } else {
        setSelectedCameraId(updated[0]?.id ?? "");
      }
    } catch (err) {
      setIsRunning(false);
      setError(getErrorMessage(err));
      setStatus("Camera failed to start");
      setStatusState("error");
    }
  }

  function applyRecordingStatus(payload: RecordingStatusPayload) {
    setIsRecording(payload.isRecording);
    if (payload.isRecording) setError("");

    if (payload.state === "error") {
      setError(payload.message);
    }
  }

  // The preview server serves multipart/x-mixed-replace, but if the camera
  // never produced a frame (e.g. device error, permission denied) the
  // browser will eventually give up on the <img>. Re-mount the element on
  // error to reconnect once the camera recovers.
  function handlePreviewError() {
    if (reconnectTimerRef.current !== null) return;
    reconnectTimerRef.current = window.setTimeout(() => {
      reconnectTimerRef.current = null;
      setPreviewToken((token) => token + 1);
    }, RECONNECT_DELAY_MS);
  }

  async function toggleRecording() {
    if (!isRecording && !isRunning) return;

    try {
      setError("");

      const payload = isRecording
        ? await invoke<RecordingStatusPayload>("stop_recording")
        : await invoke<RecordingStatusPayload>("start_recording");

      applyRecordingStatus(payload);
    } catch (err) {
      setError(getErrorMessage(err));
    }
  }

  async function capturePhoto() {
    if (!isRunning) return;
    try {
      await invoke<string>("capture_photo");
    } catch (err) {
      setError(getErrorMessage(err));
    }
  }

  const previewSrc = previewUrl
    ? `${previewUrl}?t=${previewToken}`
    : "";

  const showOverlay = !isRunning;
  const videoPrimaryLabel = isRecording ? "Stop recording" : "Start recording";

  return (
    <main className="camera-app">
      <section className="camera-viewport">
        {previewSrc ? (
          <img
            className="camera-feed"
            src={previewSrc}
            alt="Live camera preview"
            onError={handlePreviewError}
          />
        ) : (
          <div className="camera-feed camera-feed-placeholder" />
        )}

        <div className={`camera-overlay ${showOverlay ? "visible" : ""}`}>
          <div className="status-card">
            <div className="status-indicator" />
            <div>
              <strong className="status-title">
                {error
                  ? "Camera Error"
                  : statusState === "empty"
                    ? "No Camera Found"
                    : "Opening Camera"}
              </strong>
              <p className="status-message">
                {error ||
                  (statusState === "empty"
                    ? "Connect a camera to continue."
                    : status)}
              </p>
            </div>
          </div>
        </div>

        <footer className="toolbar">
          <div className="toolbar-group toolbar-left">
            <button className="tool-btn" type="button" disabled aria-label="Gallery">
              <svg width="16" height="16" viewBox="0 0 16 16" fill="none">
                <rect x="1" y="1" width="6" height="6" rx="1.5" fill="currentColor" />
                <rect x="9" y="1" width="6" height="6" rx="1.5" fill="currentColor" />
                <rect x="1" y="9" width="6" height="6" rx="1.5" fill="currentColor" />
                <rect x="9" y="9" width="6" height="6" rx="1.5" fill="currentColor" />
              </svg>
            </button>

            <button
              className={`tool-btn ${mode === "photo" ? "active" : ""}`}
              type="button"
              onClick={() => !isRecording && setMode("photo")}
              disabled={isRecording}
              aria-pressed={mode === "photo"}
              aria-label="Photo mode"
            >
              <svg width="18" height="16" viewBox="0 0 18 16" fill="none">
                <path d="M6.5 1.5h5L13 3.5h2.25a1.25 1.25 0 0 1 1.25 1.25v8.5a1.25 1.25 0 0 1-1.25 1.25H2.75A1.25 1.25 0 0 1 1.5 13.25v-8.5A1.25 1.25 0 0 1 2.75 3.5H5L6.5 1.5Z" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
                <circle cx="9" cy="8.75" r="2.75" stroke="currentColor" strokeWidth="1.4" />
              </svg>
            </button>

            <button
              className={`tool-btn ${mode === "video" ? "active" : ""}`}
              type="button"
              onClick={() => setMode("video")}
              disabled={isRecording && mode !== "video"}
              aria-pressed={mode === "video"}
              aria-label="Video mode"
            >
              <svg width="20" height="14" viewBox="0 0 20 14" fill="none">
                <rect x="1" y="1.5" width="12.5" height="11" rx="2" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
                <path d="M13.5 5.5L18.5 2.5v9l-5-3v-3Z" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round" />
              </svg>
            </button>
          </div>

          <div className="toolbar-center">
            {mode === "photo" ? (
              <button
                className="shutter-btn"
                type="button"
                disabled={!isRunning}
                onClick={capturePhoto}
                aria-label="Take photo"
              >
                <svg width="22" height="18" viewBox="0 0 22 18" fill="none" className="shutter-icon">
                  <path d="M7 1.5h8l1.5 2.5h2.75A1.75 1.75 0 0 1 21 5.75v9.5A1.75 1.75 0 0 1 19.25 17H2.75A1.75 1.75 0 0 1 1 15.25v-9.5A1.75 1.75 0 0 1 2.75 4H5.5L7 1.5Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
                  <circle cx="11" cy="10.5" r="3.5" stroke="currentColor" strokeWidth="1.5" />
                </svg>
              </button>
            ) : (
              <button
                className={`record-btn ${isRecording ? "recording" : ""}`}
                type="button"
                disabled={!isRunning && !isRecording}
                onClick={toggleRecording}
                aria-label={videoPrimaryLabel}
              >
                <span className="record-btn-outer">
                  <span className="record-btn-inner">
                    <svg width="22" height="16" viewBox="0 0 22 16" fill="none" className="record-icon">
                      <rect x="1" y="1.5" width="14" height="13" rx="2.5" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
                      <path d="M15 6l5.5-3v10L15 10V6Z" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
                    </svg>
                  </span>
                </span>
              </button>
            )}
          </div>

          <div className="toolbar-group toolbar-right">
            <div className="status-stack">
              <button className="effects-btn" type="button" disabled>
                Effects
              </button>
            </div>
          </div>
        </footer>
      </section>
    </main>
  );
}

function getErrorMessage(error: unknown) {
  if (error instanceof Error) return error.message;
  if (typeof error === "string") return error;
  return "Unknown error";
}

export default App;
