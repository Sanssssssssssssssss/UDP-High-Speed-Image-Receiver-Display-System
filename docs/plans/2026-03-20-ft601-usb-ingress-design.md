# FT601 USB Ingress Design

## Goal
- Add a future USB ingress path for FPGA image transfer through FT601 without changing the existing frame parser semantics.
- Keep the current receive pipeline reusable across UDP socket, Npcap diagnostic capture, and FT601 USB.

## Design Summary
- The application now supports an ingress-source selector in the Network page:
  - `UDP Socket`
  - `Npcap Diagnostic`
  - `FT601 USB`
- `UdpFramePipelineWorker` remains the single place that consumes packet batches.
- `Ft601Receiver` is a new ingress adapter that converts an FT601 byte stream into the same logical packets that the UDP parser already expects.

## Key Constraint
- USB bulk transfer does not preserve UDP-style datagram boundaries.
- Therefore the FT601 path must add a lightweight host-visible logical-packet boundary while preserving payload semantics.

## Recommended FT601 Logical Packet Format
- Total logical packet size: `804 bytes`
- Bytes `0..3`: fixed sync header `55 33 11 77`
- Bytes `4..803`: existing `800-byte` payload packet

## Payload Semantics
- The `800-byte` payload keeps the same meaning as the immutable UDP path:
  - all `0xAA` payload packet = frame start
  - normal line payload packet = one sequential RGB565 image line
  - all `0xBB` payload packet = frame end
- RGB565 byte order must remain unchanged from the current UDP sender path.

## Host-Side Packetization Behavior
- Read a continuous byte stream from FT601.
- Search for the 4-byte sync header `55 33 11 77`.
- Once aligned, slice every `804 bytes` into one logical packet.
- Strip the first 4 sync bytes.
- Forward the remaining `800-byte` payload into the unchanged parser.
- If alignment is lost, discard bytes until the next sync header and then resume.

## Suggested Software Defaults
- Ingress source: `FT601 USB`
- Device match string: `FT601`
- FT601 IN pipe: `0x82`
- Host read chunk size: `16384 bytes`

## FPGA-Side Interface Pattern
- Present one FT601 USB IN stream to the host.
- Send concatenated `804-byte` logical packets continuously.
- Never insert padding inside one logical packet.
- Prefer to start streaming on a frame boundary so the first payload packet after reset is an all-`0xAA` frame-start packet.
- If the stream is reset mid-frame, resume with a fresh all-`0xAA` packet before sending new line payloads.
- Keep line ordering identical to the current UDP sender order.

## Why This Pattern
- It keeps the existing parser and reconstruction logic intact.
- It isolates USB framing concerns to the FT601 ingress adapter.
- It gives the FPGA side a simple, deterministic contract that does not require redesigning the receiver pipeline.

## Current Software Status
- The repository now includes an FT601 USB ingress scaffold and UI switch.
- Current implementation status:
  - ingress mode selection is wired through the app
  - `Ft601Receiver` can packetize a byte stream using the sync-header scheme
  - runtime checks for `FTD3XX.dll` are in place
  - the actual D3XX read loop against real FT601 hardware is still pending

## Next Steps
1. Implement the real D3XX open/read loop in `Ft601Receiver`.
2. Validate packet alignment and sync-loss recovery on a host-side replay stream.
3. Validate FPGA-to-PC FT601 transfer with the exact `804-byte` logical packet contract.
