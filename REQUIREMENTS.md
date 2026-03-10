# REQUIREMENTS

## 1. Status Legend
- Confirmed: directly supported by current code or explicitly requested by the user.
- Assumed: needed to move forward, but still requires confirmation.
- Gap: described in README or UI, but not fully implemented or verified in code yet.

## 2. Functional Requirements

### FR-001 Receive UDP image data
- Status: Confirmed
- The system shall listen on a configured UDP endpoint and read incoming datagrams continuously.
- Source basis: `UdpReceiver` binds a UDP socket and emits received datagrams.

### FR-002 Reconstruct image frames from UDP packets
- Status: Confirmed
- The system shall reconstruct image frames from ordered UDP payloads using frame start, line payload, and frame end semantics.
- Source basis: `UdpFrameProcessor::processFrameData`.

### FR-003 Display reconstructed frames in the desktop UI
- Status: Confirmed
- The system shall render the latest reconstructed frame in a Qt GUI.

### FR-004 Show receive/display throughput indicator
- Status: Confirmed
- The system shall expose frame rate information in the UI.

### FR-005 Save snapshots
- Status: Confirmed
- The system shall allow the user to save the current frame as a PNG image.

### FR-006 Record video
- Status: Confirmed
- The system shall allow the user to start and stop video recording to a selected directory.

### FR-007 Support horizontal/vertical flipping
- Status: Confirmed
- The system shall allow horizontal and vertical flip controls for the displayed image.

### FR-008 Runtime configuration instead of hardcoded network/environment values
- Status: Assumed
- The system should allow configuration of bind IP, port, and external tool paths without source-code edits.
- Reason: current code hardcodes network and tool paths, which blocks portability.

### FR-009 Local demo mode without dedicated hardware
- Status: Confirmed
- The system shall provide a software-only demo path that emits the same receiver-side packet semantics so the pipeline can be validated locally without dedicated hardware.
- Reason: user explicitly requires a startup demo for software verification.

### FR-010 Brightness/gamma/sharpness/denoise controls affect image output
- Status: Confirmed
- The UI shall apply brightness, gamma, sharpness, and denoise controls locally on the receiver-side image output without changing the incoming UDP packet semantics.

## 3. Non-Functional Requirements

### NFR-001 Maintainability
- Status: Confirmed
- Project decisions, state, and tasks must be documented in repository memory files.

### NFR-002 Reproducible setup
- Status: Confirmed
- Dependencies, build steps, and runtime prerequisites must be documented clearly enough for repeatable setup.

### NFR-003 Stability first
- Status: Confirmed
- Prefer simple, robust implementations over premature optimization or feature expansion.

### NFR-004 Performance baseline
- Status: Assumed
- The system should preserve the project's intended high-throughput use case and avoid obvious regressions in the receive/display path.

### NFR-005 System load efficiency
- Status: Confirmed
- Optimization work shall explicitly consider CPU load, memory pressure, thread contention, and avoidable data copies under sustained UDP traffic.

### NFR-006 Compatibility
- Status: Confirmed
- The project should reduce environment-specific assumptions so the codebase is easier to bring up across compatible Windows development setups.

## 4. Business / Project Rules
- Every implementation change should map back to a requirement in this file.
- If a requirement is unclear, record the assumption before implementation.
- Do not replace the current Qt/C++ stack without explicit approval.
- Do not change the upstream UDP packet format, sender-side construction flow, or pixel transfer semantics; optimization is limited to the receiver-side implementation.

## 5. Acceptance Criteria For First Runnable Version
- The project builds in a documented local environment.
- The application launches successfully.
- A documented demo path produces visible frames in the UI.
- Snapshot save works.
- At least one of recording or packet capture is either working or explicitly deferred with rationale.
- Remaining environment-specific limitations are documented in `STATE.md`.

## 6. Known Gaps / Conflicts To Resolve
- README claims 800x800 support, but code currently reconstructs a 400x400 image and paints it into an 800x800 target area.
- `main.cpp` and `UdpFrameProcessor.cpp` hardcode receive IP and port usage.
- `UdpReceiver.cpp` hardcodes tshark executable path and capture output path.
- `newudp.pro` hardcodes OpenCV include/lib paths to a local Windows installation.
- `YoloProcessor` exists as a future inference path, but it is not wired into the active receive/display pipeline.
