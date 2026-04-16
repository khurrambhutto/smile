import { useEffect, useMemo, useRef, useState } from "react";
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
  const [cameras, setCameras] = useState<CameraInfo[]>([]);
  const [selectedCameraId, setSelectedCameraId] = useState("");
  const [previewSrc, setPreviewSrc] = useState("");
  const [status, setStatus] = useState("Starting camera…");
  const [statusState, setStatusState] = useState("starting");
  const [error, setError] = useState("");
  const [isRunning, setIsRunning] = useState(false);
  const [frameInfo, setFrameInfo] = useState<CameraFramePayload | null>(null);
  const autoStartedRef = useRef(false);

  const selectedCamera = useMemo(
    () => cameras.find((camera) => camera.id === selectedCameraId) ?? null,
    [cameras, selectedCameraId],
  );

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

  const footerLabel = selectedCamera?.name ?? "Camera";
  const showOverlay = !previewSrc;

  return (
    <main className="booth-app">
      <section className="booth-stage">
        {previewSrc ? (
          <img
            className="booth-preview"
            src={previewSrc}
            alt="Live camera preview"
          />
        ) : (
          <div className="booth-preview booth-preview-placeholder" />
        )}

        <div className={`booth-overlay ${showOverlay ? "visible" : ""}`}>
          <div className="booth-status-card">
            <div className="booth-status-dot" />
            <div>
              <strong className="booth-status-title">
                {error
                  ? "Camera Error"
                  : statusState === "empty"
                    ? "No Camera Found"
                    : "Opening Camera"}
              </strong>
              <p className="booth-status-text">
                {error ||
                  (statusState === "empty"
                    ? "Connect a camera to continue."
                    : status)}
              </p>
            </div>
          </div>
        </div>

        <div className="booth-top-label">
          <span>smile</span>
        </div>

        <footer className="booth-dock" aria-hidden="true">
          <div className="dock-side dock-left">
            <button className="dock-icon-button" type="button" disabled>
              <span className="icon-grid">
                <span />
                <span />
                <span />
                <span />
              </span>
            </button>

            <button className="dock-icon-button" type="button" disabled>
              <span className="icon-photo" />
            </button>

            <button className="dock-icon-button" type="button" disabled>
              <span className="icon-video" />
            </button>
          </div>

          <div className="dock-center">
            <button
              className="shutter-button"
              type="button"
              disabled
              aria-label="Shutter"
            >
              <span className="shutter-inner">
                <span className="shutter-camera-glyph" />
              </span>
            </button>
          </div>

          <div className="dock-side dock-right">
            <button className="effects-button" type="button" disabled>
              Effects
            </button>
          </div>
        </footer>

        <div className="booth-meta">
          <span>{footerLabel}</span>
          {frameInfo ? (
            <span>
              {frameInfo.width}×{frameInfo.height} · {frameInfo.pixelFormat}
            </span>
          ) : null}
          <span>{isRunning ? "Live" : "Standby"}</span>
        </div>
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
