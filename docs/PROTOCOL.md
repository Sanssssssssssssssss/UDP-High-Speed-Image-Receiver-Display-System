# Wire contract / 数据协议

This describes the current receiver and demo, not a newly invented FPGA protocol.
Source: `UdpFramePipelineWorker::processFrameData` and `LocalDemoSender`.

| Part | Interpretation |
| --- | --- |
| First 4 bytes | Skipped by the UDP parser; not trusted as line/frame indices |
| Start marker | Nonempty payload after the first 4 bytes consists entirely of `AA` |
| Image row | Up to 800 bytes: 400 RGB565 pixels, high byte first |
| End marker | Nonempty payload consists entirely of `BB` |
| Frame | Start marker → 400 sequential row packets → end marker |

Nominal datagram length is 804 bytes. The demo puts `55 33 11 77` before markers
and counters before rows, but the receiver intentionally does not rely on those
counters. The FT601 scaffold scans for `55 33 11 77` and emits 804-byte logical
packets; it has no D3XX device read loop yet.

## Bounds, loss and recovery

- Packets shorter than four bytes are ignored. A four-byte packet inside a frame
  consumes an empty row, matching the existing parser behavior.
- Oversized row payloads are clipped to 800 bytes. Short rows are zero-padded;
  an odd trailing byte does not form a pixel.
- A new start marker discards an unfinished frame. End markers without a start
  and row packets outside a frame do not publish an image.
- Rows beyond the 400-row limit are ignored. The end marker finalizes the frame.
- Missing trailing rows use neighbouring received/recovered bytes where possible,
  otherwise black. No row indices means a missing middle row shifts later data;
  the receiver cannot identify its original position or reorder packets.
- A payload entirely `AA` or `BB` is indistinguishable from a marker in this
  protocol. Fixing that ambiguity requires a sender/receiver protocol revision.
- Queue overflow drops oldest batches and invalidates parser state until a fresh
  start marker. This limits lag; it does not retransmit or recover dropped data.

Default bind is `0.0.0.0:8080`; demo destination is `127.0.0.1:8080`.
The 60 FPS demo target corresponds to 24,120 datagrams/s and 19,392,480 payload
bytes/s including its four-byte transport headers, excluding UDP/IP/Ethernet overhead.
Neither this arithmetic nor a loopback screenshot establishes hardware throughput.

中文提示：当前协议按到达顺序收行，前四字节不作为可信行号使用。中间丢包和乱序不能被精确修复；
队列溢出后会等待新帧头。不要把插值或补行描述成无损恢复。
