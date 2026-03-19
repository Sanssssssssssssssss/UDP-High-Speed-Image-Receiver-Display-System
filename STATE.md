# STATE

## Current Status
- Repository has been cloned locally and is now on branch `Post-Train`.
- Baseline code review is complete at a high level.
- Project memory files have been initialized to serve as durable context for future sessions.
- Active source files have been reorganized into module directories, and non-active files have been isolated under `legacy/`.
- A root-level `main.cpp` launcher has been restored.
- The receive/display hot path has been trimmed without changing UDP packet structure or sender-side behavior.
- A built-in local loopback demo sender now starts with the application to verify the receive/display path.
- The UI now shows first-pass performance statistics for packets/s, average frame time, interpolation time, and recovered lines/s.
- The main window now gives more space to the video surface, and the display scales with the window while preserving aspect ratio.
- Brightness, gamma, sharpness, and denoise sliders now apply receiver-side pixel adjustments to the displayed frame.
- Horizontal and vertical flip behavior has been corrected for displayed, captured, and recorded output.
- Video recording now initializes from the processed display frame instead of the stale raw path.
- The desktop UI now uses a larger right-side control area, and the control text and interactive widgets have been enlarged further while keeping the current title hierarchy and black non-gradient palette.
- The right-side control area now keeps stream status pinned while image tuning, capture, network, and AI controls live in switchable subpages instead of one long scrolling column.
- Bind address and port can now be changed from the control panel, and applying them triggers a receiver-side UDP socket rebind without restarting the whole application.
- The UI now exposes an explicit AI detection toggle and status area, but inference remains opt-in and idle until a real model/runtime path is configured.
- The VS Code build flow now deploys Qt, OpenCV, and MinGW runtime DLLs into the executable folder, and `build-vscode/debug/newudp.exe` can launch directly without external PATH setup.
- The right-side page switcher no longer uses scrollable tabs; it now uses a compact multi-row button grid to keep all sections visible within the narrower control panel.
- Presentation is now decoupled from frame parsing with a fixed refresh cadence, and the UI debug stats now show both parse FPS and present FPS.
- Frame assembly no longer stores each received line in a separate `QByteArray`; it now uses a contiguous frame buffer with direct interpolation/copy logic to reduce hot-path allocations.
- Receiver-side batch size has been increased so high-rate UDP bursts spend less time on repeated batch signal emission.
- A portable package and zip are now produced under `dist/`, and the packaged `newudp.exe` has passed a direct launch smoke test.
- The performance stats now expose receiver-drain busy time and max drain time per second so GUI-thread starvation can be distinguished from pure link-side packet loss.
- The built-in demo sender now runs on its own worker thread instead of sharing the main GUI thread, making local software-path stress tests less self-throttling.
- The Network page now includes a built-in demo toggle so protocol-compatible local UDP traffic can be started or stopped from inside the running app.
- VS Code now exposes separate hardware-mode and demo-mode launch entries so the app can be started without opening PowerShell manually.
- The receive/reconstruct/render path has now been split so `UdpFrameProcessor` acts as a presentation-only widget while a dedicated pipeline worker owns packet draining, frame reconstruction, local image tuning, and final-frame generation off the UI thread.
- Recording now runs through a dedicated writer worker with a bounded queue instead of encoding directly on the live processing path.
- An `OPTIMIZATION_LOG.md` file now tracks exactly which hot-path logic changed from which previous logic, including future YOLO optimization history.
- OpenMP is now enabled for row-level hot loops and SSE2 is used for missing-line interpolation inside the worker-side pipeline.
- Flip and image-tuning control delivery now uses explicit typed queued signal-slot forwarding into the worker thread instead of string-based runtime dispatch.
- The live performance text now also shows the worker's active `flip` and image-tuning parameters so control-path regressions can be diagnosed from the UI.
- The Windows deploy script now copies `libgomp-1.dll`, fixing direct launches of the OpenMP-enabled executable.
- The UDP receiver no longer periodically discards pending datagrams, now requests a larger socket receive buffer, and batches packet delivery from the socket thread into the frame processor.
- The built-in loopback demo now targets 60 fps and roughly 24k UDP packets per second, matching the intended stress level more closely.
- The processor-side ingress queue is now explicitly bounded to 6 frame-equivalents (2412 packets); beyond that, the oldest pending batches are dropped and the parser forces a resync on the next frame marker.
- Frame reconstruction follows the confirmed immutable protocol: all-`0xAA` starts a frame, all-`0xBB` ends a frame, and line payloads are consumed sequentially between them because the UDP line packets do not carry an explicit line index.
- The app now starts in hardware mode by default; the local UDP stress demo and tshark bootstrap only start when explicitly requested.
- The UI performance text now exposes marker imbalance, orphan/overflow line packets, short frame ends, queue drops, and parser resync counts for live debugging.
- A `.vscode` workspace and `scripts/vscode-qt.ps1` toolchain script have been added for this machine.
- A local Qt 5.15.2 + MinGW 8.1 + OpenCV 3.4.8 toolchain is now installed under `.local/toolchain`.
- The project now builds successfully through the VS Code task flow, and the executable has passed a startup smoke test on this machine.
- A packaged `models/best.onnx` model is now deployed with the executable, and the AI path is no longer placeholder-only.
- AI inference now runs on a dedicated thread with a latest-frame mailbox, and returned detection boxes are overlaid on the final display frame.
- The local UDP demo scene now uses a pulsing target-like image to help validate AI activation, not only packet receive stability.

## Current Understanding
- This is a Windows-oriented Qt Widgets UDP image receiver that reconstructs RGB565 line data into a displayed frame.
- The code currently assumes a 400-line frame buffer, paints into an 800x800 area, and includes snapshot/recording/flip controls.
- Several runtime assumptions are still environment-specific, but the image adjustment controls are now connected on the receiver side.

## Immediate Next Step
- Validate on the running UI that `best.onnx` produces sensible detections on both the pulsing demo scene and the real hardware scene, then tune thresholds/input handling if the model shape or confidence behavior needs adjustment.

## Risks
- Hardcoded paths will prevent portability and make onboarding brittle.
- README capability claims are ahead of the actual connected implementation in several places.
- The exact external UDP protocol definition is not yet documented in-repo.
- Hardware dependency may slow validation unless a local simulator/replay path is built early.
- The current code likely contains avoidable copies and thread-handoff overhead in the hot path, but this still needs measurement.
- The strongest remaining software-side risk is that packet draining, frame assembly, and QWidget-driven presentation are still too tightly coupled around the GUI thread path.
- If the external traffic truly depends on NIC promiscuous/mirror mode rather than normal host-addressed UDP delivery, Qt's normal UDP socket path may still be constrained by system/network configuration.
- The build now depends on a project-local toolchain path inside this repository, so moving the repository will require refreshing the environment variables or relying on the detection script.
- Recording behavior has compile-time and startup smoke coverage now, but it still needs a manual output-file validation pass.
- Runtime network rebinding is now available, but it still needs hardware-path validation on the actual UDP source to confirm it behaves correctly under real traffic.
- The portable package path is now working on this machine, but it still needs verification on a second Windows machine to confirm no hidden local dependency remains.
- The new worker-thread render architecture has startup coverage now, but it still needs real-hardware latency validation and recording-file verification under load.

## Handoff Notes
- Start each new session by reading `PROJECT_BRIEF.md`, `REQUIREMENTS.md`, `ARCHITECTURE.md`, `DECISIONS.md`, `TASKS.md`, and `STATE.md`.
- If new information conflicts with these files, resolve the conflict explicitly and update the documents before coding.
- Keep work inside this repository and continue on branch `Post-Train` unless a new branch strategy is explicitly agreed.
