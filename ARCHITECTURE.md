# ARCHITECTURE

## 1. Current Stack
- Language: C++11
- UI: Qt Widgets
- Networking: `QUdpSocket`
- Concurrency: Qt thread + queued signal/slot + `QtConcurrent`
- Image/video: `QImage`, `QPainter`, OpenCV `VideoWriter`
- Build: qmake `.pro`
- Target environment observed in code: Windows + MinGW + local OpenCV install

## 2. Current Modules

### `UdpReceiver`
- Owns the UDP socket and optional tshark bootstrap process.
- Receives datagrams in bounded batches and emits raw packet payload batches upstream.
- Supports receiver-side rebind to a new address/port without restarting the full application.

### `UdpFrameProcessor`
- Owns receive thread and frame reconstruction logic.
- Detects frame start/end markers.
- Buffers line data, interpolates missing lines, converts RGB565 to RGB888, and renders the frame.
- Handles snapshot/video recording, runtime receiver rebind, FPS reporting, and receiver/AI status fan-out to the UI.

### `ControlUI`
- Keeps stream status pinned while exposing switchable subpages for image tuning, capture, network, and AI controls.
- Emits adjustment signals for brightness/gamma/sharpness/denoise, flip state, runtime bind address/port changes, and AI enable state.

## 3. Current Data Flow
1. `main.cpp` creates `UdpFrameProcessor` and `ControlUI`.
2. `src/app/main.cpp` can optionally start a local UDP stress demo for startup verification.
3. `UdpFrameProcessor` creates `UdpReceiver` on a worker thread.
4. `UdpReceiver` binds the UDP socket and emits `newFrameBatch`.
5. `UdpFrameProcessor::processFrameData` reconstructs image lines and updates the current `QImage`.
6. `ControlUI` can request runtime receiver rebind or AI enable-state changes through queued signals.
7. Widget repaint displays the latest frame.
8. Optional snapshot/recording consumes the current frame.

## 4. Architectural Constraints
- Preserve the existing Qt Widgets baseline for the first milestone.
- Prefer incremental refactoring over wholesale redesign.
- New features should separate protocol parsing, frame assembly, and presentation more clearly where practical.
- Performance-sensitive changes should focus first on hot-path copies, thread handoff cost, and blocking operations on the UI thread.

## 5. Repository Layout
- `src/app`: application entry point
- `src/network`: UDP receive path
- `src/processing`: frame reconstruction and display path
- `src/ui`: control panel UI
- `src/inference`: future model inference components
- `include/*`: headers organized by module
- `legacy/*`: retained but currently non-active files, old project files, Qt Creator artifacts, and autosave content

## 6. Near-Term Target Direction
- Extract configuration from hardcoded constants.
- Isolate UDP protocol/frame parsing for easier testing.
- Add a simulator or replay source for demo/testing without hardware.
- Reduce environment-specific assumptions in build and runtime setup.
- Keep inference-related code isolated from the live receive/display path until the baseline pipeline is stable and benchmarked.
- Keep demo traffic protocol-compatible with the receiver's existing frame-start, line-payload, and frame-end interpretation.
