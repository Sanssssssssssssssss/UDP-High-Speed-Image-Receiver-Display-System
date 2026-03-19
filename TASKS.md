# TASKS

## Todo
- [ ] Clarify authoritative product goal for the first deliverable: hardware bring-up, local demo, or both.
- [ ] Confirm target toolchain: Qt version, compiler, OpenCV version, and Windows environment expectations.
- [ ] Document the UDP packet/frame protocol precisely from code and/or upstream hardware project.
- [ ] Remove or parameterize hardcoded runtime paths and network settings.
- [ ] Verify snapshot and recording behavior end to end with an interactive save/output check.
- [ ] Decide whether tshark integration stays as core feature or optional diagnostic tool.
- [ ] Benchmark the current receive -> reconstruct -> display pipeline to identify the real hot path.
- [ ] Reduce avoidable copies, queued work, and lock contention in the frame processing path.
- [ ] Define the target compatibility matrix for supported Windows/Qt/OpenCV combinations.
- [ ] Decide the future inference acceleration direction and backend strategy.
- [ ] Replace remaining hardcoded runtime configuration with receiver-side configurable parameters.
- [ ] Replace AI placeholder control with a verified model-backed inference path once the runtime/model location is defined.
- [ ] Benchmark and optimize RGB565 -> RGB888 conversion further with SIMD if LUT is not sufficient.
- [ ] Investigate residual real-hardware smoothness issues after decoupling parse FPS and present FPS.
- [ ] Validate the new in-app demo toggle against the real hardware path and confirm it does not contaminate baseline measurements when disabled.
- [ ] Benchmark the new worker-thread render architecture on the real hardware path and quantify remaining end-to-end latency.
- [ ] Manually validate the new async recording output path under sustained load.
- [ ] Validate on the live UI that worker-side `flip` and image-tuning stats track user input exactly under load.
- [ ] Validate ONNX detection quality on the real hardware scene and tune confidence/NMS/input-size settings if needed.
- [ ] Decide whether AI model path should stay packaged-only or become operator-configurable from the AI page.

## In Progress
- [ ] Receiver-side hot-path optimization under immutable UDP protocol constraints.

## Completed
- [x] Clone upstream repository into `D:\GPT_Project\UDP-High-Speed-Image-Receiver-Display-System`.
- [x] Move active source files into module-based directories under `src/` and `include/`.
- [x] Isolate legacy UI files, alternate project files, autosave content, and Qt Creator artifacts under `legacy/`.
- [x] Switch active development branch to `Post-Train`.
- [x] Review baseline repository structure and identify key constraints/gaps.
- [x] Restore a repository-root `main.cpp` launcher entry.
- [x] Remove redundant queued dispatch in the frame processing path without changing packet semantics.
- [x] Replace marker-packet detection and line buffering with lower-overhead receiver-side logic while keeping the same protocol interpretation.
- [x] Remove cross-thread `QUdpSocket` buffer clearing and keep socket access on its owning thread.
- [x] Fix recording state UI feedback so normal stop does not report a failure.
- [x] Add a built-in loopback UDP demo sender for startup verification.
- [x] Add first-pass receive-chain performance statistics: packets/s, average frame time, interpolation cost, and recovered lines/s.
- [x] Replace per-pixel RGB565 expansion math with LUT-based conversion.
- [x] Enlarge the display area and let the video surface scale with the main window while preserving aspect ratio.
- [x] Connect brightness/gamma/sharpness/denoise sliders to receiver-side image processing.
- [x] Fix horizontal and vertical flip behavior for displayed, captured, and recorded frames.
- [x] Rework recording startup to use the processed frame and valid codec selection.
- [x] Redesign the desktop UI into a larger video-first layout with a card-based control panel.
- [x] Replace the blue gradient UI palette with a larger black non-gradient control-console style.
- [x] Remove periodic receiver-side UDP buffer clearing that was discarding pending datagrams.
- [x] Batch UDP datagrams across the receiver-thread to processor-thread handoff to reduce queue pressure.
- [x] Increase the requested UDP socket receive buffer on startup.
- [x] Upgrade the built-in UDP demo sender to a 60 fps stress mode at roughly 24k packets per second.
- [x] Add a bounded pending-batch queue with overload dropping and resync behavior on the processor side.
- [x] Reconfirm and enforce the actual immutable protocol: all-`0xAA` frame start, sequential line payloads, all-`0xBB` frame end.
- [x] Add debug counters for marker imbalance, orphan line packets, duplicate lines, out-of-range lines, queue drops, and parser resyncs.
- [x] Make the local stress demo and tshark bootstrap opt-in instead of always-on.
- [x] Add VS Code workspace files and a local Windows PowerShell toolchain detection script.
- [x] Install a local Qt 5.15.2 + MinGW 8.1 + OpenCV 3.4.8 toolchain under `.local/toolchain`.
- [x] Configure user environment variables `QT_QMAKE`, `MINGW_MAKE`, `GDB_PATH`, and `OPENCV_ROOT`.
- [x] Build the project successfully with the local VS Code toolchain flow.
- [x] Complete a startup smoke test by launching the built executable successfully.
- [x] Rebuild and smoke-test the application after the display/processing/recording refactor.
- [x] Split the right-side control surface into status + paged subviews for image, capture, network, and AI controls.
- [x] Add operator-editable bind address and port controls with receiver-side rebind support.
- [x] Add an explicit AI control entry and status pane in the UI without forcing inference onto the hot path.
- [x] Deploy Qt, OpenCV, and MinGW runtime DLLs into the built executable directory so `newudp.exe` can run directly.
- [x] Replace scrollable page tabs with a compact multi-row page switcher in the right-side control panel.
- [x] Decouple frame parsing from presentation cadence and expose parse FPS vs present FPS in the debug stats.
- [x] Produce a portable package directory and zip under `dist/` for running on other Windows machines.
- [x] Replace per-line `QByteArray` frame storage with a contiguous frame buffer to reduce allocations and interpolation overhead.
- [x] Increase receiver batch size to cut cross-thread signal churn under high packet rates.
- [x] Move the built-in demo sender off the GUI thread so local stress testing does not self-throttle the UI path as heavily.
- [x] Add a Network-page toggle that starts and stops the built-in protocol-compatible local UDP demo without restarting the app.
- [x] Add separate VS Code hardware-mode and demo-mode launch entries so the app can be started without a manual PowerShell command.
- [x] Add `OPTIMIZATION_LOG.md` to record hot-path logic changes and future YOLO optimization history.
- [x] Refactor the render pipeline so UI paint is limited to presenting the final frame while a worker thread owns packet draining, frame reconstruction, and local image tuning.
- [x] Move live recording off the frame-processing hot path into a dedicated writer worker with a bounded queue.
- [x] Add OpenMP row-level parallelism and SSE2 line interpolation to the pure pixel hot paths.
- [x] Replace brittle string-based worker control forwarding with typed queued signal-slot forwarding for flip and image-tuning controls.
- [x] Add worker-side processing state to the live debug stats so control-path regressions are visible in the UI.
- [x] Fix OpenMP runtime DLL deployment so the built executable can launch directly after `-fopenmp` is enabled.
- [x] Integrate a packaged ONNX YOLO model into the active worker/display pipeline.
- [x] Change the local UDP demo scene to a pulsing target-like image that is more useful for AI validation.

## Blocked
- [ ] Protocol validation is blocked on missing formal packet/frame specification.
