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
- Owns UDP socket, tshark process, and periodic buffer-clearing timer.
- Receives datagrams and emits raw packet payloads upstream.

### `UdpFrameProcessor`
- Owns receive thread and frame reconstruction logic.
- Detects frame start/end markers.
- Buffers line data, interpolates missing lines, converts RGB565 to RGB888, and renders the frame.
- Handles snapshot/video recording and FPS reporting.

### `ControlUI`
- Exposes UI controls for FPS display, save path, recording, snapshot, and flip settings.
- Emits adjustment signals for brightness/gamma/sharpness/denoise, but those are not wired into processing yet.

## 3. Current Data Flow
1. `main.cpp` creates `UdpFrameProcessor` and `ControlUI`.
2. `src/app/main.cpp` now also starts a lightweight local UDP demo sender for startup verification.
3. `UdpFrameProcessor` creates `UdpReceiver` on a worker thread.
4. `UdpReceiver` binds UDP socket and emits `newFrameData`.
5. `UdpFrameProcessor::processFrameData` reconstructs image lines and updates the current `QImage`.
6. Widget repaint displays the latest frame.
7. Optional snapshot/recording consumes the current frame.

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
