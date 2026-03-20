# STATE

## Current Status
- Repository has been cloned locally and is now on branch `Post-Train`.
- Project memory files are active and remain the long-term source of truth for future sessions.
- Active source files are organized under `src/` and `include/`, with legacy content isolated under `legacy/`.
- A root-level `main.cpp` launcher remains available.
- The live pipeline has already been split so `UdpFrameProcessor` is presentation-only while `UdpFramePipelineWorker` owns packet draining, reconstruction, local image tuning, and final-frame generation off the UI thread.
- Recording runs through a dedicated bounded writer worker instead of the live processing hot path.
- The UI supports runtime control of image tuning, flip, capture, network parameters, demo mode, and AI enable state.
- The app builds locally through the VS Code / qmake flow and `build-vscode/debug/newudp.exe` can launch directly on this machine.
- The packaged ONNX model path is active, and AI inference currently runs through a repo-local Python `onnxruntime` helper while preserving the previously verified YOLO decode semantics.
- The local Python ONNX environment now exposes `DmlExecutionProvider` on this Intel Arc machine.
- The built-in UDP demo supports both a procedural scene and an exact pixel-by-pixel reference-image replay through `assets/demo_reference.*`.
- An optional Windows-only Npcap diagnostic mode is now available from the Network page and feeds captured UDP payloads into the unchanged parser.
- Npcap is now installed on this machine, so the diagnostic capture path is available for hardware debugging.
- Hardware bring-up has improved from "zero visible traffic" to "some data visible", but the user still reports severe packet loss and unstable image reception.
- The software now supports a third ingress mode, `FT601 USB`, alongside `UDP Socket` and `Npcap Diagnostic`.
- The Network page now exposes only two operator-facing input choices, `UDP Socket` and `FT601 USB`; Npcap is a separate optional diagnostic toggle under the UDP path.
- The FT601 path is currently a software scaffold: ingress-mode wiring, UI controls, runtime `FTD3XX.dll` checks, and a host-side packetizer are in place.
- The FT601 host-side packetizer is designed around `804-byte` logical packets made of a 4-byte sync header `55 33 11 77` plus the unchanged `800-byte` payload packet semantics.
- The real FT601 D3XX device open/read loop is still pending.

## Current Understanding
- This is a Windows-oriented Qt Widgets image receiver for an immutable packet protocol built around all-`0xAA` frame start, sequential line payloads, and all-`0xBB` frame end.
- UDP sender-side packet content and ordering are not allowed to change.
- Additional ingress modes must adapt transport framing at the boundary and still feed the same downstream parser contract.
- Current hardware risk is no longer only "no traffic at all"; it has shifted to "some traffic is visible, but loss is severe and the exact choke point is still unknown."

## Immediate Next Step
- Use the installed Npcap path to compare socket-mode loss vs capture-mode loss on the live FPGA link, while keeping the FT601 ingress scaffold ready for future USB-based FPGA output.

## Risks
- The exact external packet/frame specification is still not fully formalized in-repo.
- Severe packet loss on the live FPGA path may still be caused by a mix of link-level loss, addressing/capture behavior, host receive limitations, and remaining software-side overload.
- The FT601 path is not yet backed by a real D3XX read loop, so future USB bring-up still depends on additional driver-level integration.
- Recording behavior has smoke coverage but still needs manual output-file validation under sustained load.
- The current ONNX deployment path still depends on a repo-local Python helper for this exact model/toolchain combination.
- DirectML acceleration is machine-dependent and may fall back on other hosts.

## Handoff Notes
- Start each new session by reading `PROJECT_BRIEF.md`, `REQUIREMENTS.md`, `ARCHITECTURE.md`, `DECISIONS.md`, `TASKS.md`, `STATE.md`, and `OPTIMIZATION_LOG.md`.
- If new information conflicts with these files, resolve the conflict explicitly and update the documents before coding.
- Keep work inside this repository and continue on branch `Post-Train` unless a new branch strategy is explicitly agreed.
