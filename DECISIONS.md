# DECISIONS

## D-001 Use repository memory files as project source of truth
- Status: Accepted
- Date: 2026-03-09
- Decision: Maintain project context in `PROJECT_BRIEF.md`, `REQUIREMENTS.md`, `ARCHITECTURE.md`, `DECISIONS.md`, `TASKS.md`, and `STATE.md`.
- Reason: the user explicitly requires externalized memory across sessions.

## D-002 Keep current Qt/C++/qmake baseline for the initial milestone
- Status: Accepted
- Date: 2026-03-09
- Decision: Do not replace the current stack during initialization.
- Reason: user requested stable, constraint-consistent progress from the existing repository; major rewrites would add risk before requirements are clarified.

## D-003 Prioritize a runnable demo path early
- Status: Accepted
- Date: 2026-03-09
- Decision: after requirement clarification and baseline stabilization, the first meaningful milestone is a runnable end-to-end demo, including a software-only path if hardware is unavailable.
- Reason: user explicitly expects future demo work to run the program through successfully.

## D-004 Use a long-lived optimization branch instead of working on `main`
- Status: Accepted
- Date: 2026-03-09
- Decision: keep ongoing work on the long-lived `Post-Train` branch instead of `main`.
- Reason: the user explicitly requested staying on a dedicated optimization branch and not focusing on merge workflow for now.

## D-005 Normalize branch name to `Post-Train`
- Status: Accepted
- Date: 2026-03-09
- Decision: use `Post-Train` as the actual Git branch name.
- Reason: Git branch names cannot contain spaces, so the requested label `Post Train` must be normalized.

## D-006 File layout is reorganized by responsibility
- Status: Accepted
- Date: 2026-03-09
- Decision: active code lives under `src/` and `include/`, while inactive or local-only artifacts live under `legacy/`.
- Reason: the root directory was mixing active code, legacy UI scaffolding, alternate project files, and IDE-local files.

## D-007 Optimization work is performance-first
- Status: Accepted
- Date: 2026-03-09
- Decision: favor changes that improve throughput, parallelism, compatibility, system load, and inference readiness, provided they remain understandable and testable.
- Reason: this is the user's explicit long-term development priority for the project.

## D-008 Keep a root-level launcher entry point
- Status: Accepted
- Date: 2026-03-09
- Decision: retain a repository-root `main.cpp` that directly launches the application, with the rest of the startup logic living under `src/app`.
- Reason: the user explicitly requested a directly runnable external `main`.

## D-009 UDP protocol and upstream packet construction are immutable
- Status: Accepted
- Date: 2026-03-09
- Decision: receiver-side optimization must not alter the UDP packet layout, upstream packet generation process, or pixel transfer semantics.
- Reason: the user explicitly forbids changes to the sender-side and protocol-layer behavior.

## D-010 Built-in demo traffic remains protocol-compatible
- Status: Accepted
- Date: 2026-03-09
- Decision: the startup demo sends local loopback UDP traffic that follows the receiver's existing frame-start, line-payload, and frame-end semantics instead of introducing a separate test protocol.
- Reason: the user requires a startup demo but also requires the UDP semantics to remain unchanged.

## D-011 VS Code uses toolchain detection instead of fixed local paths
- Status: Accepted
- Date: 2026-03-10
- Decision: the workspace uses PowerShell tasks that detect `qmake`, `mingw32-make`, `gdb`, and `OPENCV_ROOT` at run time and fail with explicit diagnostics if they are missing.
- Reason: this machine did not initially expose a complete Qt C++ toolchain, so hardcoded VS Code paths would have been brittle and misleading.

## D-012 Install the toolchain inside the repository for this machine
- Status: Accepted
- Date: 2026-03-10
- Decision: install Qt 5.15.2, MinGW 8.1, and OpenCV 3.4.8 into `.local/toolchain` under the repository and let VS Code tasks prefer these local paths.
- Reason: this satisfies the user's request to execute the setup here and avoids depending on preexisting system-wide installs.

## D-013 Apply UI image adjustments on the receiver side only
- Status: Accepted
- Date: 2026-03-10
- Decision: brightness, gamma, sharpness, and denoise sliders operate on the locally reconstructed image after UDP frame reconstruction, without altering packet parsing or sender-side semantics.
- Reason: the user explicitly requested working slider behavior while forbidding changes to the upstream UDP packet flow.

## D-014 Render using a raw-frame plus processed-display-frame split
- Status: Accepted
- Date: 2026-03-10
- Decision: keep a raw reconstructed frame buffer and derive the displayed/recorded frame from a lightweight receiver-side post-processing stage.
- Reason: this preserves protocol fidelity, makes local image controls possible, and keeps display transforms separate from packet reconstruction logic.

## D-015 Use a video-first desktop control-console layout
- Status: Accepted
- Date: 2026-03-10
- Decision: keep the main window visually centered around a large video surface, with a larger card-based control panel for tuning, orientation, and capture operations, using a restrained black non-gradient palette instead of blue-heavy styling.
- Reason: the previous UI was visually weak and made the software feel unfinished despite having core functionality; the updated palette and scale better match a serious tooling interface.

## D-016 Remove destructive UDP buffer clearing and batch cross-thread packet delivery
- Status: Accepted
- Date: 2026-03-10
- Decision: the receiver must not periodically discard pending datagrams, packet delivery from the socket thread to the UI-side processor should be batched rather than queued one datagram at a time, and the user-space pending queue must remain bounded to a small number of frame-equivalents.
- Reason: periodic buffer clearing was an explicit packet-loss source, per-datagram queued delivery adds avoidable event-loop pressure under high-rate traffic, and an unbounded pending queue would hide overload until latency becomes unusable.

## D-017 Reconstruct frames by sequential line order within A/B frame markers
- Status: Accepted
- Date: 2026-03-10
- Decision: line packets are written sequentially between an all-`0xAA` frame-start packet and an all-`0xBB` frame-end packet, because the UDP line payloads do not carry an explicit line index in the protocol.
- Reason: this matches the actual immutable sender-side protocol confirmed by the user; receiver logic must not invent a line-number field that does not exist on the wire.

## D-018 Do not enable local stress demo or tshark bootstrap by default
- Status: Accepted
- Date: 2026-03-10
- Decision: the application starts in hardware-input mode by default, while the built-in 60 fps stress demo and tshark capture bootstrap are enabled only via explicit flags or environment variables.
- Reason: always-on synthetic traffic and capture bootstrap add avoidable startup load and interfere with real hardware validation.

## D-019 Keep live status pinned and move secondary controls into paged subviews
- Status: Accepted
- Date: 2026-03-10
- Decision: retain the stream status card as a persistent top-level element, while image tuning, capture, network, and AI controls live in dedicated switchable subpages inside the right-side panel.
- Reason: the single long control column had become too tall and visually dense, reducing operator usability.

## D-020 Apply bind-address and bind-port changes by receiver-side rebind instead of full app restart
- Status: Accepted
- Date: 2026-03-10
- Decision: changing receiver endpoint settings from the UI clears pending receiver-side data, rebinds the UDP socket in its owning thread, and keeps the rest of the app running.
- Reason: the user explicitly requested editable port/network parameters with a lightweight refresh behavior, and a receiver-thread rebind is lower risk than restarting the whole GUI.

## D-021 Keep AI detection explicitly opt-in and isolated from the hot path
- Status: Accepted
- Date: 2026-03-10
- Decision: expose AI detection as a dedicated UI-controlled path with explicit status reporting, but do not let it run implicitly on the receive pipeline until model/runtime availability is confirmed.
- Reason: the user wants AI control in the UI, but current inference assets are absent and the receive path remains performance-critical.

## D-022 Deploy runtime DLLs into the built output directory
- Status: Accepted
- Date: 2026-03-10
- Decision: the VS Code build flow shall run `windeployqt` and copy the required OpenCV and MinGW runtime DLLs into the executable output directory so `newudp.exe` can be launched directly.
- Reason: direct launches were failing with missing Qt/OpenCV runtime libraries even though the project built successfully.

## D-023 Keep all control pages visible through a compact multi-row switcher
- Status: Accepted
- Date: 2026-03-10
- Decision: replace scrollable page tabs with a compact multi-row page button layout so all right-side sections remain directly visible within the control panel width.
- Reason: the scrollable tab strip was wasting width and hiding sections behind navigation arrows, which made the control workflow feel broken.

## D-024 Decouple parsing throughput from presentation cadence
- Status: Accepted
- Date: 2026-03-10
- Decision: frame parsing may run as fast as data arrives, but screen presentation is driven by a fixed refresh cadence and reported separately as present FPS.
- Reason: the previous "update on every completed frame" path could report high FPS while still looking visually stuttery because parsing and painting were not paced independently.

## D-025 Use a contiguous frame buffer for line assembly
- Status: Accepted
- Date: 2026-03-11
- Decision: store received line payloads in one contiguous frame buffer with per-line length metadata instead of allocating one `QByteArray` per line.
- Reason: per-line dynamic allocation and interpolation temporary objects were wasting CPU time in the hottest receive/reconstruct path.

## D-026 Favor larger UDP batch emission under high packet rates
- Status: Accepted
- Date: 2026-03-11
- Decision: increase the receiver-side datagram batch size so the socket thread emits fewer batch signals under sustained load.
- Reason: under 24k packets per second, too-small batch sizes create avoidable cross-thread/container overhead even when packet parsing logic is unchanged.

## D-027 Keep the built-in demo sender off the GUI thread
- Status: Accepted
- Date: 2026-03-11
- Decision: run the local loopback stress sender on a dedicated worker thread instead of the main GUI thread.
- Reason: when the sender shares the GUI thread, local stress testing can under-report throughput and make the UI path look worse than the real receive-side pipeline alone.

## D-028 Keep the built-in demo runtime-controllable from the Network page
- Status: Accepted
- Date: 2026-03-11
- Decision: expose the protocol-compatible local UDP demo as an explicit Network-page toggle that can start and stop the sender during a running session.
- Reason: the user wants fast switching between hardware mode and local demo mode without reopening the app or dropping into PowerShell.

## D-029 Keep VS Code launch paths explicit for hardware mode and demo mode
- Status: Accepted
- Date: 2026-03-11
- Decision: provide separate VS Code launch/task entries for hardware-input mode and built-in demo mode instead of requiring ad hoc command-line arguments.
- Reason: the user wants a direct editor-driven run flow and should not need to remember or type `--demo` manually each time.

## D-030 Keep a dedicated optimization change log in-repo
- Status: Accepted
- Date: 2026-03-12
- Decision: record receiver, rendering, recording, and future YOLO optimization steps in a dedicated `OPTIMIZATION_LOG.md` file that explicitly states what logic changed from what previous logic.
- Reason: the user wants a durable engineering log of hot-path changes beyond the normal state/decision/task summaries.

## D-031 Keep UI responsibility limited to final frame presentation
- Status: Accepted
- Date: 2026-03-12
- Decision: move packet draining, frame reconstruction, local image processing, and recording away from the `QWidget` class into dedicated worker objects, leaving UI paint code responsible only for presenting the final frame.
- Reason: lower latency now matters more than keeping all responsibilities on one class, and the prior structure was still too tightly coupled to the GUI path.

## D-032 Use OpenMP and SIMD selectively on pure pixel hot paths
- Status: Accepted
- Date: 2026-03-12
- Decision: use OpenMP only on independent row-level loops and use SIMD first on byte-wise interpolation logic, while avoiding these techniques inside Qt GUI operations or high-level OpenCV DNN calls.
- Reason: this matches the low-latency goal without creating extra contention or unstable behavior in UI-bound code.

## D-033 Move recording onto a dedicated bounded writer path
- Status: Accepted
- Date: 2026-03-12
- Decision: record final frames through a dedicated `VideoRecorderWorker` thread with a bounded queue instead of writing video from the live processing hot path.
- Reason: synchronous encoding work was directly increasing end-to-end latency and could stall live display under load.

## D-034 Use typed queued signal-slot forwarding for worker-side controls
- Status: Accepted
- Date: 2026-03-12
- Decision: forward flip, image-tuning, capture, receiver-settings, and AI-control changes from `UdpFrameProcessor` to `UdpFramePipelineWorker` through explicit typed Qt signals connected with queued signal-slot connections, instead of string-based `QMetaObject::invokeMethod(...)`.
- Reason: after the worker-thread render refactor, the control path must remain compile-time checked and reliably bound to the worker thread; the string-based forwarding layer was too brittle and obscured failures.

## D-035 Use packaged ONNX for the first real AI integration
- Status: Accepted
- Date: 2026-03-19
- Decision: integrate AI through a packaged `models/best.onnx` file, trying OpenCV DNN first but allowing an alternate runtime path when the provided model is incompatible with the current OpenCV 3.4.8 importer.
- Reason: the user already has an ONNX artifact, but this exact model does not load natively through the current OpenCV DNN stack on this machine.

## D-036 Run YOLO inference on a dedicated latest-frame mailbox worker
- Status: Accepted
- Date: 2026-03-19
- Decision: feed AI only the latest completed frame and drop stale pending inference frames, while keeping inference on its own thread and returning only the newest detection rectangles for overlay.
- Reason: this preserves the low-latency receive/display goal and avoids unbounded AI backlog under sustained 60 fps UDP traffic.

## D-037 Preserve the old verified YOLO box decode semantics
- Status: Accepted
- Date: 2026-03-19
- Decision: keep the YOLO decode path aligned with the previously verified logic: input size 416, output interpreted as `reshape(1, 5).t()`, confidence from the fifth scalar, and NMS thresholds of 0.3 / 0.5. Only the final box projection is adapted to the current 400x400 frame composition path.
- Reason: the user explicitly confirmed that the previous decode semantics were correct and should not be changed casually.

## D-038 Use a repo-local Python ONNX helper as the current compatibility fallback
- Status: Accepted
- Date: 2026-03-19
- Decision: when the packaged ONNX model cannot be imported by OpenCV 3.4.8 DNN, fall back to a repo-local Python helper backed by `onnxruntime`, while keeping the same box decode semantics and latest-frame mailbox policy.
- Reason: this is the only currently validated runtime path for the provided `best.onnx` on this machine without pausing the project for a full OpenCV/runtime upgrade.

## D-039 Keep the built-in demo visually aligned with the real target scene
- Status: Accepted
- Date: 2026-03-19
- Decision: the built-in UDP demo should prioritize a stable, dark-field, bright-target scene with gentle brightness pulsing over synthetic packet-loss tricks or abstract motion patterns.
- Reason: the user wants the demo to validate AI activation and target perception, not only receive stress behavior.

## D-040 Sanitize the helper Python environment explicitly
- Status: Accepted
- Date: 2026-03-19
- Decision: launch the repo-local Python ONNX helper with a sanitized process environment and isolated interpreter mode, explicitly removing inherited `PYTHONHOME` / `PYTHONPATH` contamination from the parent process.
- Reason: the current desktop launch environment can inject incompatible Python configuration that breaks the helper before inference even starts.

## D-041 Support exact demo-image replay through a reference asset
- Status: Accepted
- Date: 2026-03-19
- Decision: the built-in UDP demo may load `assets/demo_reference.*`, resize it to `400x400`, pulse its brightness, convert it pixel-by-pixel to RGB565, and packetize it without changing the UDP protocol.
- Reason: the user explicitly wants the demo to validate against a real target image rather than only a procedural approximation.
