# PROJECT_BRIEF

## 1. Project Goal
- Build a stable, maintainable C++ desktop application for high-speed UDP image reception, frame reconstruction, live display, and basic recording/debugging.
- Use the existing Qt/OpenCV codebase as the baseline and push it to a reproducible, demo-ready state.
- Optimize the project over time for raw throughput, parallel execution, compatibility, lower system load, and faster model inference integration.

## 2. Background
- Current repository is a Qt Widgets application for receiving UDP image data and rendering reconstructed frames.
- The upstream README positions the project as a high-speed image receiver for FPGA-driven image transmission scenarios.
- The code currently contains several environment-specific assumptions, including hardcoded IP address, tshark path, capture path, and OpenCV include/library paths.

## 3. In Scope
- Understand and stabilize the current codebase.
- Clarify actual protocol, frame format, and runtime dependencies.
- Make the project buildable and runnable in a documented local development environment.
- Add a runnable demo path to validate the software even when dedicated hardware is unavailable.
- Improve maintainability, configuration, logging, and testability without breaking the baseline workflow.
- Continuously optimize hot paths with performance-first tradeoffs when they remain maintainable and measurable.

## 4. Out of Scope
- Replacing the full UI framework or rewriting the project into another language.
- Adding speculative features that are not tied to confirmed requirements.
- Deep FPGA/embedded-side development inside this repository unless explicitly added later.
- YOLO/AI inference work before the receive/display pipeline is stable and verified.

## 5. Success Criteria
- A new developer can understand the project through repository documents alone.
- The application can be built from a documented environment with clear dependency instructions.
- The application can display valid frames from either real UDP input or a local demo/simulator path.
- Core behavior is verified for at least one end-to-end scenario: receive -> reconstruct -> display -> optional snapshot/recording.
- Project state, decisions, and task progress remain externalized in repository documents.

## 6. Stakeholder Constraints
- Work only inside this repository folder.
- Treat repository memory files as the long-term source of truth.
- Prefer simple, robust, maintainable solutions, but prioritize measurable performance and code efficiency in optimization decisions.
- Do not expand requirements without explicit basis.
- Do not change the upstream UDP packet structure, packet generation process, or pixel transmission semantics before the packets reach the computer.

## 7. Open Questions
- What is the authoritative UDP packet/frame protocol definition?
- What image resolution(s) and pixel format(s) must be supported first?
- What are the real target OS/compiler/Qt/OpenCV versions for delivery?
- Is tshark a hard requirement or only a temporary debugging aid?
- Which first demo matters most: hardware-connected demo or local simulator demo?
- Which inference acceleration targets matter later: CPU-only, OpenCV DNN, CUDA, TensorRT, or another backend?
