use std::{
    io::{self, Read},
    sync::Arc,
    thread::{self, JoinHandle},
    time::Duration,
};

use tiny_http::{Header, Method, Response, Server, StatusCode};

use crate::frame_bus::FrameBus;

/// Boundary used in the multipart/x-mixed-replace response. Any ASCII token
/// that is unlikely to appear in JPEG bytes works.
const MULTIPART_BOUNDARY: &str = "smileframe";

/// How long a streaming client will block waiting for a single new frame
/// before we re-enter the wait loop. The connection itself stays open
/// indefinitely; this timeout just gives tiny_http a chance to notice the
/// peer has disappeared.
const FRAME_WAIT_TIMEOUT: Duration = Duration::from_secs(5);

/// Handle to the preview HTTP server; keeps the accept thread alive for as
/// long as the handle is kept (i.e. the lifetime of the Tauri app).
pub struct PreviewServer {
    port: u16,
    _accept_thread: JoinHandle<()>,
}

impl PreviewServer {
    pub fn start(bus: Arc<FrameBus>) -> Result<Self, String> {
        let server = Server::http("127.0.0.1:0")
            .map_err(|error| format!("Failed to bind preview server: {error}"))?;

        let port = server
            .server_addr()
            .to_ip()
            .ok_or_else(|| "Preview server bound to non-IP address".to_string())?
            .port();

        let server = Arc::new(server);
        let accept_server = Arc::clone(&server);
        let accept_bus = Arc::clone(&bus);

        let accept_thread = thread::Builder::new()
            .name("preview-http".into())
            .spawn(move || {
                for request in accept_server.incoming_requests() {
                    let bus = Arc::clone(&accept_bus);
                    // One worker thread per connection. Browsers typically
                    // keep a single long-lived connection open for the
                    // <img> tag, so this thread count stays very small.
                    let _ = thread::Builder::new()
                        .name("preview-client".into())
                        .spawn(move || handle_request(request, bus));
                }
            })
            .map_err(|error| format!("Failed to spawn preview server thread: {error}"))?;

        Ok(Self {
            port,
            _accept_thread: accept_thread,
        })
    }


    pub fn preview_url(&self) -> String {
        format!("http://127.0.0.1:{}/preview", self.port)
    }
}

fn handle_request(request: tiny_http::Request, bus: Arc<FrameBus>) {
    if !matches!(request.method(), Method::Get | Method::Head) {
        let _ = request.respond(Response::empty(StatusCode(405)));
        return;
    }

    // `request.url()` returns the raw request-target, which includes any
    // query string (e.g. `/preview?t=3`). The frontend appends a cache-buster
    // on reconnect, so we match on the path portion only.
    let path = request.url().split('?').next().unwrap_or("");

    match path {
        "/preview" => serve_preview(request, bus),
        "/" => {
            let _ = request.respond(
                Response::from_string("smile preview server")
                    .with_status_code(StatusCode(200)),
            );
        }
        _ => {
            let _ = request.respond(Response::empty(StatusCode(404)));
        }
    }
}

fn serve_preview(request: tiny_http::Request, bus: Arc<FrameBus>) {
    let content_type = format!("multipart/x-mixed-replace;boundary={MULTIPART_BOUNDARY}");

    // NOTE: invalid-header errors should be impossible for these constant
    // inputs; unwrapping keeps the code readable.
    let headers = vec![
        header(b"Content-Type", content_type.as_bytes()),
        header(b"Cache-Control", b"no-store, no-cache, must-revalidate, max-age=0"),
        header(b"Pragma", b"no-cache"),
        header(b"Connection", b"close"),
        header(b"X-Accel-Buffering", b"no"),
    ];

    let reader = MultipartFrameStream::new(bus);
    let response = Response::new(StatusCode(200), headers, reader, None, None);
    let _ = request.respond(response);
}

fn header(name: &[u8], value: &[u8]) -> Header {
    Header::from_bytes(name, value).expect("static header should always be valid")
}

/// `Read` adapter that fetches the latest JPEG from the `FrameBus` and
/// formats it as a multipart/x-mixed-replace part, transparently refilling
/// its internal buffer whenever tiny_http asks for more bytes.
struct MultipartFrameStream {
    bus: Arc<FrameBus>,
    last_seen: u64,
    buffer: Vec<u8>,
    cursor: usize,
}

impl MultipartFrameStream {
    fn new(bus: Arc<FrameBus>) -> Self {
        Self {
            bus,
            last_seen: 0,
            buffer: Vec::new(),
            cursor: 0,
        }
    }

    fn ensure_buffered(&mut self) -> io::Result<bool> {
        if self.cursor < self.buffer.len() {
            return Ok(true);
        }

        // Block for a new frame. On timeout we loop and keep waiting; the
        // connection stays open even across camera restarts. tiny_http will
        // stop pulling bytes once the socket errors out, so blocking here
        // is safe: the worker thread is dedicated to this connection.
        loop {
            match self.bus.next(self.last_seen, FRAME_WAIT_TIMEOUT) {
                Some((frame, seq)) => {
                    self.last_seen = seq;
                    self.buffer.clear();
                    self.buffer.reserve(frame.len() + 96);
                    self.buffer
                        .extend_from_slice(format!("--{MULTIPART_BOUNDARY}\r\n").as_bytes());
                    self.buffer.extend_from_slice(b"Content-Type: image/jpeg\r\n");
                    self.buffer
                        .extend_from_slice(format!("Content-Length: {}\r\n\r\n", frame.len()).as_bytes());
                    self.buffer.extend_from_slice(&frame);
                    self.buffer.extend_from_slice(b"\r\n");
                    self.cursor = 0;
                    return Ok(true);
                }
                None => {
                    // No frame for a while — keep waiting. Continuing the
                    // loop also gives tiny_http the chance to poll the
                    // write side and detect a dead peer on the next tick.
                    continue;
                }
            }
        }
    }
}

impl Read for MultipartFrameStream {
    fn read(&mut self, out: &mut [u8]) -> io::Result<usize> {
        if out.is_empty() {
            return Ok(0);
        }
        if !self.ensure_buffered()? {
            return Ok(0);
        }
        let remaining = &self.buffer[self.cursor..];
        let n = remaining.len().min(out.len());
        out[..n].copy_from_slice(&remaining[..n]);
        self.cursor += n;
        Ok(n)
    }
}
