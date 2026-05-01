# CPU 性能压测报告

- 生成时间: 2026-05-01T01:26:36
- 项目路径: `D:\GPT_Project\UDP-High-Speed-Image-Receiver-Display-System`
- Git: branch=`Post-Train`, commit=`5719fee`, dirty=`True`
- CPU: Intel(R) Core(TM) Ultra 9 185H (16/22 cores/logical)
- OS: Microsoft Windows 11 家庭版 10.0.26200 build 26200
- Memory: total_mb=32373.4 free_mb=11442.2
- Python: `3.10.11 (tags/v3.10.11:7d4cc5a, Apr  5 2023, 00:38:17) [MSC v.1929 64 bit (AMD64)]`
- NumPy/ONNX Runtime: `2.2.6` / `1.23.0`
- ONNX Runtime available providers: `['DmlExecutionProvider', 'CPUExecutionProvider']`

## 结论摘要

本报告强制使用 `CPUExecutionProvider`，没有使用当前机器可用的 DirectML provider，因此数值反映 CPU 端压力。
- 模型纯 forward 最优场景: `intra_threads_12`，吞吐 57.46 FPS，P95 18.69 ms。
- 完整 AI 链路最优场景: `intra_threads_4`，吞吐 38.13 FPS，P95 28.26 ms。
- 当前代码常量为 400x400；400x400 帧处理最快 394.81 FPS，最重 400x400 场景约 48.30 FPS。
- UDP localhost 压力最低丢包场景: `24000_pps_804_bytes_1_senders`，接收 18,818 pps，丢包 0.0000%。
- UDP localhost 压力最高丢包场景: `144000_pps_804_bytes_4_senders`，接收 17,642 pps，丢包 78.7575%。
- 端到端 mailbox 模拟: 生产 48.50 FPS，CPU 推理 34.59 FPS，被覆盖/跳过帧 209。

## 测试边界与方法

- 网络测试使用 localhost UDP，验证主机 UDP socket 和 CPU 包处理压力，不等价于 FPGA 真实链路的网卡/驱动/交换机丢包。
- 帧处理测试复刻当前 worker 的关键语义: 丢线恢复、RGB565 到 RGB888、亮度/gamma LUT、降噪、锐化、翻转。OpenCV GaussianBlur 在脚本中用依赖更少的 NumPy separable blur 近似。
- AI 测试复刻 `YoloProcessor` / `onnx_helper.py` 的输入尺寸、NCHW float32、输出 reshape 与 NMS 语义，并强制 CPU provider。
- 当前源码 `UdpFramePipelineWorker` 的活跃分辨率常量是 400x400；报告额外包含 800x800 projection，用来评估 README 中旧目标分辨率的扩展风险。

## 模型信息

- 模型: `D:\GPT_Project\UDP-High-Speed-Image-Receiver-Display-System\models\best.onnx`
- 大小: 11.58 MB
- SHA256: `b6031503ff343021c4c8288e0c4c40c7a63d6c68df2e6346428a7d1b5e61ed87`
- 输入: `[{'name': 'images', 'shape': [1, 3, 416, 416]}]`
- 输出: `[{'name': 'output0', 'shape': [1, 5, 3549]}]`
- 节点数: `259`, opset: `[{'domain': 'ai.onnx', 'version': 12}]`

## AI 模型纯推理 CPU 压测

| 场景 | intra | FPS | Mean ms | P95 ms | P99 ms | CPU 单核% | CPU整机% | RSS峰值MB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| intra_threads_1 | 1 | 15.12 | 66.09 | 85.22 | 100.42 | 98.03 | 4.46 | 99.05 |
| intra_threads_2 | 2 | 28.97 | 34.50 | 40.65 | 43.14 | 196.64 | 8.94 | 107.29 |
| intra_threads_4 | 4 | 50.17 | 19.92 | 22.29 | 23.53 | 393.70 | 17.90 | 106.27 |
| intra_threads_8 | 8 | 55.30 | 18.07 | 19.59 | 20.29 | 790.28 | 35.92 | 107.08 |
| intra_threads_12 | 12 | 57.46 | 17.39 | 18.69 | 19.14 | 1,181.36 | 53.70 | 109.38 |
| intra_threads_16 | 16 | 53.34 | 18.73 | 20.66 | 22.52 | 1,544.03 | 70.18 | 110.40 |
| intra_threads_22 | 22 | 18.08 | 55.27 | 99.11 | 111.29 | 1,768.49 | 80.39 | 111.85 |

## AI 完整链路 CPU 压测

| 场景 | intra | FPS | Mean ms | P95 ms | P99 ms | 平均检测数 | CPU 单核% | RSS峰值MB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| intra_threads_1 | 1 | 14.53 | 68.80 | 85.65 | 90.58 | 2.00 | 97.59 | 105.20 |
| intra_threads_2 | 2 | 28.80 | 34.71 | 41.34 | 44.85 | 2.00 | 197.91 | 107.08 |
| intra_threads_4 | 4 | 38.13 | 26.22 | 28.26 | 29.53 | 2.00 | 392.67 | 107.81 |
| intra_threads_8 | 8 | 37.85 | 26.41 | 29.38 | 32.04 | 2.00 | 785.56 | 108.59 |
| intra_threads_16 | 16 | 35.18 | 28.41 | 32.90 | 34.38 | 2.00 | 1,547.67 | 109.47 |
| intra_threads_22 | 22 | 15.01 | 66.48 | 105.10 | 127.14 | 2.00 | 1,773.73 | 111.69 |

## 并发 AI 推理 CPU 压测

| 场景 | 并发session | 每session intra | 总FPS | Mean ms | P95 ms | P99 ms | CPU 单核% | RSS峰值MB | worker迭代min/max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1_sessions_x_intra_2 | 1 | 2 | 34.04 | 29.01 | 33.78 | 36.36 | 195.80 | 89.27 | 273/273 |
| 2_sessions_x_intra_2 | 2 | 2 | 56.38 | 34.95 | 39.25 | 41.31 | 394.40 | 103.16 | 226/227 |
| 4_sessions_x_intra_2 | 4 | 2 | 79.49 | 49.26 | 57.94 | 62.02 | 781.35 | 139.05 | 157/162 |
| 8_sessions_x_intra_2 | 8 | 2 | 89.10 | 87.15 | 113.11 | 129.02 | 1,491.90 | 261.25 | 87/95 |

## 帧重建与图像处理 CPU 压测

| 场景 | 分辨率 | 丢线% | FPS | Mean ms | P95 ms | P99 ms | 输入MB/s | CPU 单核% | RSS峰值MB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 400x400_default_0pct_loss | 400x400 | 0.00 | 394.81 | 2.53 | 3.67 | 4.53 | 120.49 | 98.69 | 81.64 |
| 400x400_default_1pct_loss | 400x400 | 1.00 | 368.79 | 2.71 | 3.92 | 4.87 | 112.54 | 97.12 | 80.52 |
| 400x400_heavy_tuning_0pct_loss | 400x400 | 0.00 | 48.30 | 20.69 | 27.01 | 29.60 | 14.74 | 98.12 | 85.00 |
| 400x400_heavy_tuning_5pct_loss | 400x400 | 5.00 | 53.59 | 18.65 | 26.46 | 33.81 | 16.35 | 97.26 | 85.81 |
| 800x800_projection_heavy_1pct_loss | 800x800 | 1.00 | 10.47 | 95.44 | 116.66 | 134.67 | 12.78 | 98.45 | 89.20 |

## UDP 并发吞吐压测

| 场景 | 目标pps | 包大小 | 发送线程 | 发送pps | 接收pps | 接收Mbps | 推断丢包% | 序号gap% | CPU 单核% |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 24000_pps_804_bytes_1_senders | 24,000 | 804 | 1 | 18,818 | 18,818 | 121.04 | 0.0000 | 0.0000 | 44.48 |
| 48000_pps_804_bytes_2_senders | 48,000 | 804 | 2 | 31,555 | 31,555 | 202.96 | 0.0000 | 0.0000 | 76.24 |
| 96000_pps_804_bytes_4_senders | 96,000 | 804 | 4 | 62,420 | 43,067 | 277.01 | 31.0047 | 31.0047 | 170.28 |
| 144000_pps_804_bytes_4_senders | 144,000 | 804 | 4 | 83,051 | 17,642 | 113.47 | 78.7575 | 78.7552 | 200.39 |
| 48000_pps_1300_bytes_2_senders | 48,000 | 1,300 | 2 | 31,275 | 31,275 | 325.26 | 0.0000 | 0.0000 | 68.11 |
| 96000_pps_1300_bytes_4_senders | 96,000 | 1,300 | 4 | 62,079 | 43,006 | 447.26 | 30.7239 | 30.7227 | 169.85 |

## 端到端 mailbox 模拟

| 场景 | 生产FPS | 推理FPS | 生产Mean ms | 生产P95 ms | 推理Mean ms | 推理P95 ms | 覆盖帧 | CPU 单核% | RSS峰值MB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| producer_60fps_intra_4 | 48.50 | 34.59 | 19.08 | 23.01 | 28.44 | 31.24 | 209 | 471.52 | 108.99 |

## 风险判断

- 如果 CPU-only AI P95 明显高于 16.7 ms，则 60 FPS 输入下不能做到逐帧 CPU 推理，必须继续保持 latest-frame mailbox/drop stale 策略。
- 如果 UDP localhost 在 96k pps 以上出现丢包，真实硬件链路还需要分离验证: 网卡中断合并、接收缓冲、Npcap/QUdpSocket 差异、FPGA 发送 pacing。
- 400x400 与 800x800 的帧处理耗时差异用于提示分辨率扩展成本；若恢复 README 的 800x800 目标，应先把 C++ 热路径做 native micro-benchmark。
- 当前 AI fallback helper 默认会优先选择 DirectML；本报告为了 CPU 侧结论强制 CPU provider，实际 UI 开启 AI 时可能比本报告更快。

## 优化建议

1. AI CPU 路径: 以本报告中吞吐最高且 P95 稳定的 intra thread 数作为 CPU fallback 默认值，避免盲目使用所有逻辑核造成抢占。
2. AI 调度: 保持单 worker/latest-frame mailbox，不建议在当前 CPU-only 配置下开多个并发推理 session，除非并发表显示总 FPS 随 session 数线性上升且 P95 可接受。
3. UDP 接收: 真实硬件测试时同时记录 app stats 中 `pkts/s`、`dropped pkts/s`、`queue max` 与 Windows 网卡计数器，定位丢包发生在网卡、socket 还是 worker 队列。
4. 帧处理: 重图像处理参数打开时，优先优化 OpenCV filter 调用和 flip 的内存访问；默认显示路径已经主要受 RGB565 扩展和内存带宽影响。
5. 报告复跑: 改模型、改分辨率、改 OpenMP 或 ONNX Runtime 线程配置后，使用同一脚本复跑，比较 JSON/CSV 中同名场景。

## 复现命令

```powershell
.\.local\py310-yolo\Scripts\python.exe scripts\cpu_stress_bench.py --suite full
```

## 原始输出

- JSON: `D:\GPT_Project\UDP-High-Speed-Image-Receiver-Display-System\docs\reports\cpu_stress_report_20260501_012631.json`
- CSV: `D:\GPT_Project\UDP-High-Speed-Image-Receiver-Display-System\docs\reports\cpu_stress_report_20260501_012631.csv`
