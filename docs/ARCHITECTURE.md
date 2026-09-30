# Architecture / 架构

This is one Qt desktop process with an optional Python child. Frontend/backend
directories describe ownership, not a web service boundary.

| Module | Owner and responsibility |
| --- | --- |
| `src/app/ApplicationLauncher.cpp` | GUI thread; window wiring, startup flags, demo sender lifecycle |
| `src/frontend/ControlUI.*` | GUI thread; controls and status pages |
| `src/frontend/UdpFrameProcessor.*` | GUI thread; forwards controls, presents completed QImages |
| `src/backend/network/UdpReceiver.*` | Receiver thread; socket bind/read, batching, optional Npcap thread |
| `src/backend/network/Ft601Receiver.*` | USB worker scaffold; runtime detection and stream packetization |
| `src/backend/processing/UdpFramePipelineWorker.*` | Pipeline thread; parser, processing, snapshots, dispatch |
| `src/backend/processing/VideoRecorderWorker.*` | Recorder thread; RGB→BGR conversion and OpenCV VideoWriter |
| `src/backend/inference/YoloProcessor.*` | Inference thread; model/backend lifecycle and latest-frame mailbox |
| `src/backend/inference/onnx_helper.py` | Child process; binary RGB transport, ORT inference and NMS |

## Receive to display

1. `UdpReceiver::readPendingDatagrams` emits batches of at most 1,024 packets.
2. A **direct** connection calls `enqueueFrameBatch` on the ingress thread. A mutex
   protects this queue, capped at 2,412 packets (six nominal frames). Old batches
   are discarded when full; parsing must resynchronize at a new start marker.
3. One queued drain runs on the pipeline thread. It skips four transport bytes,
   consumes rows sequentially and finalizes on an all-`BB` payload.
4. Incomplete trailing rows are filled by the existing recovery rule. That loop
   is sequential because it reads flags/data updated by neighbouring iterations.
   Independent RGB565 conversion and other row operations can use OpenMP/SSE2.
5. Processing composes a display image and emits `frameReady`. QImage uses shared
   storage and detaches before mutation. The frontend remembers the latest image
   and requests paint on a 16 ms timer; `paintEvent` only scales and draws it.

## Inference and recording

`wantsFrame` and the mutex-protected `submitFrame` avoid an unbounded queue of AI
frame events. The inference worker runs one frame and retains at most one pending
frame. Detection rectangles return through queued signals to the pipeline.
The native backend tries OpenCV DNN first. The fallback starts a persistent Python
helper, sends one JSON line followed by exactly `stride × height` raw RGB bytes,
and reads one JSON result. Stdout is protocol-only; diagnostics belong on stderr.

Recording is asynchronous. Its worker retains at most six frames once delivered.
The Qt signal delivery queue before that worker is not independently bounded;
sustained slow encoders still need load testing. Closing a recording clears pending
frames before finalizing the container, so it does not promise lossless recording.

## Shutdown and controls

Controls reach the pipeline through queued signals. Runtime source switches reset
frame state and request receiver rebind. Shutdown stops ingress, joins its threads,
then joins inference and recording workers. The frontend joins the pipeline before
its widget is destroyed. Do not perform widget operations in a worker thread.

Comments at the direct-delivery boundary, parser, recovery loop and inference
mailbox explain these constraints. Tests use a single friend test class for parser
state checks, so production interfaces remain unchanged.

## Verification boundary

CTest covers parser content, malformed short input, bounded queue overflow,
loopback reception, image persistence, AVI decoding and GUI startup. Python tests
exercise the actual model and process framing. No test proves FPGA link quality,
exact loss concealment, clinical accuracy or FT601 hardware support.

中文概要：接收线程只负责收包与入队；流水线线程负责解析和图像处理；GUI 线程只负责界面。
AI 和录像独立运行。关键约束是跨线程缓冲区所有权、队列上限和丢包后的重新同步。
