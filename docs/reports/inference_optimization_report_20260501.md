# AI Inference Hot-Path Optimization Report

Generated: 2026-05-01

## Scope

This optimization pass targets the app's active fallback inference path:

- `UdpFramePipelineWorker` -> `YoloProcessor`
- `YoloProcessor` -> long-lived `scripts/onnx_helper.py`
- ONNX Runtime inference using `models/best.onnx`

The native OpenCV DNN import path is still retained, but this model currently runs through the Python ONNX Runtime helper on this machine.

## Changes

1. Replaced per-frame JSON/base64 image payloads with a compact JSON header followed by raw RGB24 bytes.
2. Kept the old `image_rgb24` base64 request format in the helper for compatibility and A/B benchmarking.
3. Reused preprocessing buffers in the helper and vectorized output filtering/NMS preparation.
4. Added helper environment overrides:
   - `POST_TRAIN_ORT_PROVIDER`
   - `POST_TRAIN_ORT_INTRA_THREADS`
   - `POST_TRAIN_ORT_WARMUP`
5. Added model warmup during helper startup to avoid the first enabled AI frame paying the full kernel/session warmup.
6. Changed AI frame scheduling so Qt does not queue full-frame copies while inference is busy. The pipeline now submits directly to a thread-safe latest-frame mailbox only when the helper wants one more frame.

## Microbenchmark

Command:

```powershell
.\.local\py310-yolo\Scripts\python.exe scripts\inference_helper_bench.py --provider CPUExecutionProvider --intra-threads 4 --iterations 120 --warmup-iterations 10
.\.local\py310-yolo\Scripts\python.exe scripts\inference_helper_bench.py --provider DmlExecutionProvider --intra-threads 4 --iterations 120 --warmup-iterations 10
```

| Provider | Protocol | Round-trip mean | Round-trip P95 | Helper total mean | Inference mean | Boxes |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| CPUExecutionProvider | base64 JSON | 33.38 ms | 39.24 ms | 26.76 ms | 21.56 ms | 2 |
| CPUExecutionProvider | binary RGB24 | 24.20 ms | 27.19 ms | 23.44 ms | 18.90 ms | 2 |
| DmlExecutionProvider | base64 JSON | 18.58 ms | 23.03 ms | 11.93 ms | 6.39 ms | 2 |
| DmlExecutionProvider | binary RGB24 | 11.41 ms | 12.83 ms | 10.61 ms | 6.89 ms | 2 |

## Result

- CPU helper round-trip mean improved from `33.38 ms` to `24.20 ms`, about `27.5%` faster.
- DirectML helper round-trip mean improved from `18.58 ms` to `11.41 ms`, about `38.6%` faster.
- The app-side frame handoff now avoids hidden Qt event-queue buildup of full image copies, so live AI mode should show less stale-frame lag under sustained load.

## Second Pass: C++ Pre-Resize

The first binary transport pass still let Python/Pillow resize the `400x400` frame to the model's `416x416` input. The second pass moves that resize into `YoloProcessor` with OpenCV and sends a pre-sized RGB24 payload. The helper keeps the original frame size in metadata for output box scaling, then skips Pillow and builds the NCHW float tensor from a NumPy view.

Fallback remains available:

```powershell
$env:POST_TRAIN_CPP_PREPROCESS = "0"
```

| Provider | Protocol | Round-trip mean | Round-trip P95 | Helper preprocess mean | Boxes |
| --- | --- | ---: | ---: | ---: | ---: |
| CPUExecutionProvider | binary 400x400, helper resize | 22.61 ms | 29.18 ms | 3.20 ms | 2 |
| CPUExecutionProvider | binary 416x416, C++ pre-resize | 16.84 ms | 25.69 ms | 0.09 ms | 2 |
| DmlExecutionProvider | binary 400x400, helper resize | 9.17 ms | 9.69 ms | 2.04 ms | 2 |
| DmlExecutionProvider | binary 416x416, C++ pre-resize | 7.73 ms | 7.76 ms | 0.03 ms | 2 |

## Third Pass: Native OpenCV Path Trim

The native OpenCV DNN backend was also tightened so the fallback is not the only optimized path. The previous code converted the already-RGB Qt frame to BGR and then asked `blobFromImage(..., swapRB=true)` to swap it back to RGB. The new path feeds the RGB frame directly with `swapRB=false`, reuses the per-instance blob/output buffers, and forwards through the existing `net` instead of creating a local `Net` handle each frame.

This keeps the same channel order and decode thresholds while removing avoidable per-frame work from machines where OpenCV can import the ONNX model.

## Infra Roadmap: DMA / Zero-Copy Ingress

DMA is a good fit for the broader receiver infrastructure, but it should live below the AI inference API rather than inside the model call itself. The current inference optimization reduces the cost after a frame has reached `YoloProcessor`; DMA and zero-copy work should reduce the cost of getting frame bytes from hardware into the parser, display, recorder, and AI mailbox.

### Where DMA Applies

| Ingress | DMA fit | Notes |
| --- | --- | --- |
| UDP socket | Indirect | The NIC already DMA-transfers packets into kernel buffers. The app can still improve socket buffer sizing, receive batching, queue policy, and copy count, but true user-space DMA bypass needs a lower-level packet I/O stack or driver. |
| Npcap diagnostic mode | Low | Useful for hardware bring-up and capture visibility, but it is not the primary high-throughput path and should not be treated as the performance transport. |
| FT601 USB path | Medium | The existing `FT601 USB` scaffold is the right boundary for overlapped/asynchronous bulk reads, preallocated buffers, and vendor-driver DMA behavior where available. |
| PCIe FPGA capture / custom driver | High | This is the cleanest true-DMA design: device DMA writes into pinned ring buffers owned by a KMDF/UMDF driver, and the app consumes frame descriptors through IOCTL/shared mappings. |

### Recommended Infra Work

1. Add transport-neutral buffer pools: fixed-size, aligned packet/frame slabs reused across UDP, FT601, recorder, and AI handoff.
2. Track copy count per frame: RX payload copy, RGB565 expansion, final `QImage`, AI resize, recorder copy. This gives the DMA work a measurable target.
3. Move ingress implementations behind a common batch API: `UdpReceiver`, `Npcap`, and `Ft601Receiver` should all emit batches backed by reusable buffers rather than freshly allocated byte arrays.
4. Add bounded SPSC queues or latest-frame mailboxes at each hot boundary: ingress -> parser, parser -> display, parser -> AI, parser -> recorder.
5. Add thread and memory policy controls: worker affinity, receiver priority, larger socket buffers, and explicit backpressure/drop counters.
6. For FT601: implement overlapped read rings with multiple in-flight transfers and parse directly from completed transfer buffers.
7. For future PCIe DMA: design a descriptor ring plus pinned host memory, expose buffer indices and timestamps to the Qt app, and avoid copying payload bytes into `QByteArray`.

### Success Metrics

- Lower frame age P95/P99 under sustained traffic.
- Fewer full-frame copies before display and AI.
- Stable packet/frame queue depth with clear drop counters.
- Higher maximum input FPS before drops.
- Lower CPU use in receive/reconstruct stages at the same AI setting.

## Validation

- `python -m py_compile scripts/onnx_helper.py scripts/inference_helper_bench.py`
- `powershell -ExecutionPolicy Bypass -File scripts\vscode-qt.ps1 -Action Build`
- `build-vscode\debug\newudp.exe --demo` stayed running after an 8 second smoke launch.
