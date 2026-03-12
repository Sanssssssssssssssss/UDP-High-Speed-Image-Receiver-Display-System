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
