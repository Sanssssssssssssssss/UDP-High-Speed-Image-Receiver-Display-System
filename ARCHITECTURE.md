# ARCHITECTURE

## 1. Current Stack
- Language: C++11
- UI: Qt Widgets
- Networking: `QUdpSocket`
- Concurrency: Qt thread + queued/direct signal/slot + OpenMP row-level parallel loops
- Image/video: `QImage`, `QPainter`, OpenCV `VideoWriter`
- Build: qmake `.pro`
- Target environment observed in code: Windows + MinGW + local OpenCV install

## 2. Current Modules

### `UdpReceiver`
- Owns the UDP socket and optional tshark bootstrap process.
- Receives datagrams in bounded batches and emits raw packet payload batches upstream.
- Supports receiver-side rebind to a new address/port without restarting the full application.

### `UdpFrameProcessor`
- Is now a presentation shell only.
- Owns the final `QWidget` paint path and presents the latest finished frame from the pipeline worker.
- Keeps UI paint responsibility limited to the final blit step.

### `UdpFramePipelineWorker`
- Owns receiver-thread coordination, packet draining, frame reconstruction, missing-line recovery, RGB565 expansion, local image tuning, flip application, and snapshot handling.
- Produces final display-ready frames off the UI thread.
- Uses cached LUT state, OpenMP row-parallel loops, and SSE2-assisted interpolation on hot paths.

### `VideoRecorderWorker`
- Owns `VideoWriter` on a dedicated thread.
- Accepts final frames through a bounded queue so recording does not directly stall the live display pipeline.

### `ControlUI`
- Keeps stream status pinned while exposing switchable subpages for image tuning, capture, network, and AI controls.
- Emits adjustment signals for brightness/gamma/sharpness/denoise, flip state, runtime bind address/port changes, and AI enable state.

## 3. Current Data Flow
1. `main.cpp` creates `UdpFrameProcessor` and `ControlUI`.
2. `src/app/main.cpp` can optionally start a local UDP stress demo for startup verification.
3. `UdpFrameProcessor` creates `UdpReceiver` on a worker thread.
4. `UdpReceiver` binds the UDP socket and emits `newFrameBatch`.
5. `UdpFramePipelineWorker` drains packet batches, reconstructs frames, performs local image processing, and emits a final display-ready `QImage`.
6. `UdpFrameProcessor` receives the final frame and only paints it to the widget surface.
7. `ControlUI` sends runtime tuning, capture, network, and AI requests into the pipeline worker through queued signals.
8. Optional recording is handled by `VideoRecorderWorker` on a separate thread through a bounded frame queue.

## 4. Architectural Constraints
- Preserve the existing Qt Widgets baseline for the first milestone.
- Prefer incremental refactoring over wholesale redesign.
- New features should separate protocol parsing, frame assembly, and presentation more clearly where practical.
- Performance-sensitive changes should focus first on hot-path copies, thread handoff cost, and blocking operations on the UI thread.
- UI code should avoid owning packet parsing, frame assembly, and encoding work directly.

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
- Keep a durable optimization log for hot-path changes and future YOLO optimization history.
