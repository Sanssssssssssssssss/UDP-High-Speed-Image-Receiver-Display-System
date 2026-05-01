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

## Validation

- `python -m py_compile scripts/onnx_helper.py scripts/inference_helper_bench.py`
- `powershell -ExecutionPolicy Bypass -File scripts\vscode-qt.ps1 -Action Build`
- `build-vscode\debug\newudp.exe --demo` stayed running after an 8 second smoke launch.
