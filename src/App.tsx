import { useEffect, useRef, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import "./App.css";

type CameraInfo = {
  id: string;
  name: string;
  path: string;
};

type CameraFramePayload = {
  dataUrl: string;
  width: number;
  height: number;
  pixelFormat: string;
  sequence: number;
};

type CameraStatusPayload = {
  state: string;
  message: string;
};

const CAMERA_FRAME_EVENT = "camera-frame";
const CAMERA_STATUS_EVENT = "camera-status";

function App() {
  const [, setCameras] = useState<CameraInfo[]>([]);
  const [, setSelectedCameraId] = useState("");
  const [previewSrc, setPreviewSrc] = useState("");
  const [status, setStatus] = useState("Starting camera…");
  const [statusState, setStatusState] = useState("starting");
  const [error, setError] = useState("");
  const [, setIsRunning] = useState(false);
  const [, setFrameInfo] = useState<CameraFramePayload | null>(null);
  const autoStartedRef = useRef(false);

  useEffect(() => {
    let mounted = true;
    let unlistenFrame: UnlistenFn | null = null;
    let unlistenStatus: UnlistenFn | null = null;

    const setup = async () => {
      try {
        unlistenFrame = await listen<CameraFramePayload>(
          CAMERA_FRAME_EVENT,
          (event) => {
            if (!mounted) return;
            setPreviewSrc(event.payload.dataUrl);
            setFrameInfo(event.payload);
          },
        );

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
              setPreviewSrc("");
              setFrameInfo(null);
              setError(message);
            } else if (state === "stopped") {
              setIsRunning(false);
            }
          },
        );

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
      if (unlistenFrame) void unlistenFrame();
      if (unlistenStatus) void unlistenStatus();
    };
  }, []);

  async function startCamera(cameraId: string) {
    try {
      setError("");
      setStatus("Starting camera…");
      setStatusState("starting");
      setPreviewSrc("");
      setFrameInfo(null);

      const updated = await invoke<CameraInfo[]>("start_camera", {
        request: {
          cameraId,
          width: 640,
          height: 480,
          fps: 15,
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

  const showOverlay = !previewSrc;

  return (
    <main className="camera-app">
      <section className="camera-viewport">
        {previewSrc ? (
          <img
            className="camera-feed"
            src={previewSrc}
            alt="Live camera preview"
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
              disabled
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
