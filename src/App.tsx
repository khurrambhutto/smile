import { useEffect, useMemo, useState } from "react";
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
  const [selectedCameraId, setSelectedCameraId] = useState<string>("");
  const [previewSrc, setPreviewSrc] = useState<string>("");
  const [status, setStatus] = useState<string>("Idle");
  const [statusState, setStatusState] = useState<string>("idle");
  const [error, setError] = useState<string>("");
  const [isRunning, setIsRunning] = useState(false);
  const [isBusy, setIsBusy] = useState(false);
  const [frameInfo, setFrameInfo] = useState<CameraFramePayload | null>(null);

  const hasCameras = cameras.length > 0;

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
              setIsBusy(false);
              setError("");
            } else if (state === "starting") {
              setIsBusy(true);
              setError("");
            } else if (state === "error") {
              setIsRunning(false);
              setIsBusy(false);
              setPreviewSrc("");
              setFrameInfo(null);
              setError(message);
            } else if (state === "stopped") {
              setIsRunning(false);
              setIsBusy(false);
            }
          },
        );

        await loadCameras();
      } catch (err) {
        if (!mounted) return;
        setError(getErrorMessage(err));
        setStatus("Failed to initialize camera UI");
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

  async function loadCameras() {
    try {
      setError("");
      const found = await invoke<CameraInfo[]>("list_cameras");
      setCameras(found);

      setSelectedCameraId((current) => {
        if (current && found.some((camera) => camera.id === current)) {
          return current;
        }
        return found[0]?.id ?? "";
      });

      if (found.length === 0) {
        setStatus("No camera detected");
        setStatusState("empty");
      } else if (!isRunning) {
        setStatus(`Found ${found.length} camera${found.length > 1 ? "s" : ""}`);
        setStatusState("ready");
      }
    } catch (err) {
      setError(getErrorMessage(err));
      setStatus("Failed to load cameras");
      setStatusState("error");
    }
  }

  async function startCamera() {
    if (!selectedCameraId) {
      setError("Select a camera first");
      return;
    }

    try {
      setIsBusy(true);
      setError("");
      setPreviewSrc("");
      setFrameInfo(null);

      const updated = await invoke<CameraInfo[]>("start_camera", {
        request: {
          cameraId: selectedCameraId,
          width: 640,
          height: 480,
          fps: 15,
        },
      });

      setCameras(updated);
      setStatus("Starting camera...");
      setStatusState("starting");
    } catch (err) {
      setIsBusy(false);
      setIsRunning(false);
      setError(getErrorMessage(err));
      setStatus("Failed to start camera");
      setStatusState("error");
    }
  }

  async function stopCamera() {
    try {
      setIsBusy(true);
      await invoke("stop_camera");
      setIsRunning(false);
      setIsBusy(false);
      setPreviewSrc("");
      setFrameInfo(null);
      setStatus("Camera stopped");
      setStatusState("stopped");
    } catch (err) {
      setIsBusy(false);
      setError(getErrorMessage(err));
      setStatus("Failed to stop camera");
      setStatusState("error");
    }
  }

  return (
    <main className="camera-app">
      <section className="camera-shell">
        <header className="camera-header">
          <div>
            <p className="eyebrow">Smile</p>
            <h1>Linux Camera Preview</h1>
            <p className="subtle">
              Native V4L2 camera preview for Ubuntu and other Linux distros.
            </p>
          </div>

          <button
            className="secondary-button"
            onClick={loadCameras}
            disabled={isBusy}
          >
            Refresh Cameras
          </button>
        </header>

        <section className="camera-controls">
          <label className="field">
            <span>Camera</span>
            <select
              value={selectedCameraId}
              onChange={(event) =>
                setSelectedCameraId(event.currentTarget.value)
              }
              disabled={!hasCameras || isBusy || isRunning}
            >
              {hasCameras ? (
                cameras.map((camera) => (
                  <option key={camera.id} value={camera.id}>
                    {camera.name} — {camera.path}
                  </option>
                ))
              ) : (
                <option value="">No cameras found</option>
              )}
            </select>
          </label>

          <div className="action-row">
            <button
              className="primary-button"
              onClick={startCamera}
              disabled={!hasCameras || !selectedCameraId || isBusy || isRunning}
            >
              {isBusy && !isRunning ? "Starting..." : "Start Camera"}
            </button>

            <button
              className="secondary-button"
              onClick={stopCamera}
              disabled={isBusy || !isRunning}
            >
              Stop Camera
            </button>
          </div>
        </section>

        <section className="status-row">
          <div className={`status-pill status-${statusState}`}>{status}</div>
          {selectedCamera ? (
            <div className="camera-meta">Using: {selectedCamera.name}</div>
          ) : null}
        </section>

        <section className="preview-card">
          {previewSrc ? (
            <img
              className="preview-image"
              src={previewSrc}
              alt="Live camera preview"
            />
          ) : (
            <div className="preview-placeholder">
              <div className="preview-icon">◉</div>
              <p>
                {hasCameras
                  ? "Start the camera to see your face here."
                  : "Connect a camera to begin."}
              </p>
            </div>
          )}
        </section>

        <section className="details-grid">
          <div className="detail-card">
            <span className="detail-label">State</span>
            <strong>{isRunning ? "Running" : "Stopped"}</strong>
          </div>

          <div className="detail-card">
            <span className="detail-label">Resolution</span>
            <strong>
              {frameInfo ? `${frameInfo.width}×${frameInfo.height}` : "—"}
            </strong>
          </div>

          <div className="detail-card">
            <span className="detail-label">Format</span>
            <strong>{frameInfo?.pixelFormat ?? "—"}</strong>
          </div>

          <div className="detail-card">
            <span className="detail-label">Frame</span>
            <strong>{frameInfo?.sequence ?? "—"}</strong>
          </div>
        </section>

        {error ? (
          <section className="error-card" role="alert">
            <strong>Camera Error</strong>
            <p>{error}</p>
          </section>
        ) : null}
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
