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
