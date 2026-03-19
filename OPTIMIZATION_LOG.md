# OPTIMIZATION LOG

This file records receiver, rendering, recording, and future YOLO-path optimizations as a durable engineering log.

## Format
- Date
- Area
- Before
- After
- Expected Effect
- Validation

## Entries

### 2026-03-19 - ONNX YOLO path integrated into the worker architecture
- Area: AI inference
- Before: the active build exposed only an AI placeholder toggle; the old `YoloProcessor` was not wired into the live worker/display pipeline and depended on a hardcoded external ONNX path.
- After: the app now builds `YoloProcessor`, loads a packaged `models/best.onnx`, runs inference on a dedicated thread with a latest-frame mailbox, and overlays returned detection rectangles onto the final display frame.
- Expected Effect: AI can be turned on from the current UI without stalling the UDP receive path, and packaged builds keep the model with the executable.
- Validation: build succeeds with `opencv_dnn`; demo-mode and hardware-mode launches remain stable; AI page reports model-ready status.

### 2026-03-19 - ONNX compatibility fallback and old decode semantics restored
- Area: AI inference compatibility
- Before: the active ONNX path assumed OpenCV 3.4.8 DNN could import the provided `best.onnx`, and once that failed the UI only surfaced an unusable "onnx not load" style failure.
- After: `YoloProcessor` now tries OpenCV DNN first, then falls back to a repo-local Python `onnxruntime` helper. The helper uses the previously verified decode semantics: `416x416` input, `reshape(1, 5).t()` output view, confidence from the fifth scalar, and NMS thresholds `0.3 / 0.5`.
- Expected Effect: AI activation works again on this machine without silently changing the user's known-good box decode logic.
- Validation: helper init now succeeds against `models/best.onnx`, a request/response smoke test returns valid JSON, and the desktop app builds and launches in `--demo` mode.

### 2026-03-19 - Demo scene changed from abstract bars to a pulsing target image
- Area: Local UDP demo
- Before: the built-in demo generated colorful synthetic motion that was good for receive stress, but poor for validating whether YOLO was actually seeing the intended object class.
- After: the demo now emits a darker target-like scene with a bright pulsing foreground and structured dark region, so visibility oscillates while packet semantics stay unchanged.
- Expected Effect: local demo traffic becomes more useful for validating AI activation and detection behavior, not only receive throughput.
- Validation: demo-mode launch remains stable and the displayed scene visibly pulses instead of only sweeping with bars.

### 2026-03-19 - Demo scene corrected for stable AI validation
- Area: Local UDP demo
- Before: the first pulsing demo rewrite was still visually off-target and also injected periodic dropped lines, which made the scene unsuitable for validating AI or box stability.
- After: the demo scene now stays frame-complete and renders a darker, green-biased target composition with bright foreground blobs, a structured dark cavity, and low-frequency brightness pulsing only.
- Expected Effect: AI validation is no longer confounded by synthetic packet-loss artifacts or an obviously wrong test image.
- Validation: the app launches in `--demo` mode after the change, and runtime screenshots show the scene structure instead of the previous abstract sweep.

### 2026-03-12 - Typed control forwarding for worker-side image controls
- Area: UI control -> worker pipeline
- Before: `UdpFrameProcessor` forwarded flip, tuning, capture, and AI control changes with string-based `QMetaObject::invokeMethod(...)`, which became brittle after the worker-thread refactor and left `flip` / `image tuning` effectively dead on the active path.
- After: `UdpFrameProcessor` now exposes explicit typed request signals and connects them directly to `UdpFramePipelineWorker` slots with queued signal-slot connections.
- Expected Effect: flip and image-tuning changes reliably reach the worker thread and immediately trigger recomposition of the final display frame.
- Validation: build succeeds; worker-side stats now expose the active processing parameters so runtime changes can be verified from the UI.

### 2026-03-12 - Worker-side processing state added to live debug stats
- Area: Runtime diagnostics
- Before: the stats panel exposed packet, queue, and parser information, but not whether the worker had actually received the latest flip/tuning state.
- After: the stats text now includes `flipH`, `flipV`, `bright`, `gamma`, `sharp`, and `denoise` as the worker currently sees them.
- Expected Effect: control-path regressions become directly visible without needing to infer them only from the image.
- Validation: move a slider or toggle flip and confirm the stats values change on the next update.

### 2026-03-12 - OpenMP runtime DLL deployment fix
- Area: Windows runtime packaging
- Before: the build/deploy flow copied Qt, OpenCV, and core MinGW DLLs, but missed `libgomp-1.dll`, so direct launches of the OpenMP-enabled executable could fail with a DLL-not-found exit.
- After: the deployment script now copies `libgomp-1.dll` into the output directory alongside the other MinGW runtime DLLs.
- Expected Effect: direct `exe` launches stay compatible after enabling OpenMP.
- Validation: `build-vscode/debug/newudp.exe --demo` remains running after launch instead of exiting with `0xC0000135`.

### 2026-03-12 - Low-latency render architecture refactor
- Area: Receive -> reconstruct -> display
- Before: `UdpFrameProcessor` mixed packet draining, frame reconstruction, image processing, recording, and `QWidget` painting on the same object, with heavy work still coupled to the UI thread path.
- After: `UdpFrameProcessor` is reduced to a presentation widget; a dedicated `UdpFramePipelineWorker` now owns receiver-side buffering, frame reconstruction, image tuning, and final-frame production on a worker thread.
- Expected Effect: lower GUI-thread blocking, lower present jitter, and cleaner separation between hot-path processing and UI painting.
- Validation: build and smoke test required after each structural change.

### 2026-03-12 - Display path becomes "final blit only"
- Area: Rendering
- Before: `paintEvent()` could still mirror the image before drawing.
- After: flip is moved earlier into the worker-side frame composition path; `paintEvent()` now only centers and draws the latest final frame.
- Expected Effect: lower repaint cost and less repeated work during resize/expose/update storms.
- Validation: visual flip correctness and smoother present cadence.

### 2026-03-12 - Tone LUT caching
- Area: Image tuning
- Before: brightness/gamma LUT was rebuilt on every processed frame.
- After: LUT is rebuilt only when brightness or gamma changes.
- Expected Effect: less per-frame scalar setup overhead.
- Validation: slider response remains correct while frame-time drops under non-default tone settings.

### 2026-03-12 - SIMD interpolation for missing lines
- Area: Frame repair
- Before: missing-line interpolation averaged bytes with a scalar loop only.
- After: line averaging uses SSE2 `_mm_avg_epu8` on 16-byte chunks with scalar tail handling.
- Expected Effect: lower cost when frames contain recovered lines.
- Validation: interpolation visually stable; no corruption on recovered lines.

### 2026-03-12 - OpenMP row-parallel reconstruction
- Area: Frame finalization
- Before: missing-line repair and RGB565 expansion ran serially across all rows.
- After: row-level loops are OpenMP-enabled where rows are independent.
- Expected Effect: better multicore CPU utilization for the pure pixel hot path.
- Validation: build must succeed with `-fopenmp`; parse frame time should trend down on multicore hosts.

### 2026-03-12 - Async recording with bounded queue
- Area: Capture / recording
- Before: recording happened on the frame-processing hot path.
- After: `VideoRecorderWorker` writes encoded frames on a dedicated thread with a bounded frame queue.
- Expected Effect: recording should no longer directly stall live display processing.
- Validation: recording start/stop correctness and playable output file still need manual verification.

## Future Entries

### Template
- Area:
- Before:
- After:
- Expected Effect:
- Validation:

### Planned future area: YOLO path
- Use this file to record each inference optimization step, such as:
- frame mailbox strategy
- preprocess reuse
- backend or target switch
- SIMD/OpenMP changes in pre/postprocess
- queueing / drop policy changes
- measured latency or throughput deltas
