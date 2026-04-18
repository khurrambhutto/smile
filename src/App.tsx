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

const CAMERA_STATUS_EVENT = "camera-status";
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

  const autoStartedRef = useRef(false);
  const reconnectTimerRef = useRef<number | null>(null);

  useEffect(() => {
    let mounted = true;
    let unlistenStatus: UnlistenFn | null = null;

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

        const url = await invoke<string>("get_preview_url");
        if (!mounted) return;
        setPreviewUrl(url);

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
            <button className="tool-btn" type="button" disabled>
              <span className="icon-grid">
                <span />
                <span />
                <span />
                <span />
              </span>
            </button>

            <button className="tool-btn active" type="button" disabled>
              <span className="icon-photo" />
            </button>

            <button className="tool-btn" type="button" disabled>
              <span className="icon-video" />
            </button>
          </div>

          <div className="toolbar-center">
            <button
              className="shutter-btn"
              type="button"
              disabled={!isRunning}
              onClick={capturePhoto}
              aria-label="Take photo"
            >
              <span className="shutter-fill" />
            </button>
          </div>

          <div className="toolbar-group toolbar-right">
            <button className="effects-btn" type="button" disabled>
              Effects
            </button>
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
