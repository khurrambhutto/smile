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

function IconPhoto({ className }: { className?: string }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      aria-hidden
    >
      <path
        d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3l-2.5-3z"
        stroke="currentColor"
        strokeWidth="1.85"
        strokeLinejoin="round"
        strokeLinecap="round"
      />
      <circle
        cx="12"
        cy="13"
        r="3.25"
        stroke="currentColor"
        strokeWidth="1.85"
      />
    </svg>
  );
}

function IconVideo({ className }: { className?: string }) {
  return (
    <svg
      className={className}
      viewBox="0 0 24 24"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      aria-hidden
    >
      <rect
        x="2"
        y="6"
        width="14"
        height="12"
        rx="2"
        stroke="currentColor"
        strokeWidth="1.85"
        strokeLinejoin="round"
      />
      <path
        d="m16 10 5.223 3.482a.5.5 0 0 0 .777-.416V7.87a.5.5 0 0 0-.752-.432L16 10.5"
        stroke="currentColor"
        strokeWidth="1.85"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </svg>
  );
}

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
  const [previewFailed, setPreviewFailed] = useState(false);

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
    setPreviewFailed(true);
    if (reconnectTimerRef.current !== null) {
      window.clearTimeout(reconnectTimerRef.current);
    }
    reconnectTimerRef.current = window.setTimeout(() => {
      reconnectTimerRef.current = null;
      setPreviewToken((token) => token + 1);
    }, RECONNECT_DELAY_MS);
  }

  function handlePreviewLoad() {
    setPreviewFailed(false);
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
            key={previewToken}
            className="camera-feed"
            src={previewSrc}
            alt="Live camera preview"
            onLoad={handlePreviewLoad}
            onError={handlePreviewError}
            style={{ opacity: previewFailed ? 0 : 1 }}
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
            <div
              className="toolbar-glass mode-segmented"
              role="group"
              aria-label="Capture mode"
            >
              <div
                className="mode-segmented-track"
                data-mode={mode}
              >
                <div className="mode-segmented-thumb" aria-hidden />
                <button
                  className={`mode-segment ${mode === "photo" ? "is-active" : ""}`}
                  type="button"
                  onClick={() => !isRecording && setMode("photo")}
                  disabled={isRecording}
                  aria-pressed={mode === "photo"}
                  aria-label="Photo mode"
                >
                  <IconPhoto className="mode-icon" />
                </button>
                <button
                  className={`mode-segment ${mode === "video" ? "is-active" : ""}`}
                  type="button"
                  onClick={() => setMode("video")}
                  disabled={isRecording && mode !== "video"}
                  aria-pressed={mode === "video"}
                  aria-label="Video mode"
                >
                  <IconVideo className="mode-icon" />
                </button>
              </div>
            </div>
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
                <span className="shutter-fill" />
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
                  <span className="record-btn-inner" />
                </span>
              </button>
            )}
          </div>

          <div className="toolbar-group toolbar-right">
            <div className="status-stack">
              <button
                className="effects-btn toolbar-glass"
                type="button"
                disabled
              >
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
