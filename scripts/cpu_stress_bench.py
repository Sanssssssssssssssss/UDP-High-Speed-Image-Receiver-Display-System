#!/usr/bin/env python3
"""CPU stress benchmark suite for the UDP vision console.

The suite is intentionally self-contained and uses the repo-local Python
environment dependencies that already support the ONNX helper path:
NumPy, Pillow, ONNX, and ONNX Runtime.

It measures:
  - ONNX Runtime CPU inference latency and throughput
  - preprocessing + inference + postprocessing cost
  - concurrent CPU inference saturation
  - UDP localhost packet ingest pressure
  - RGB565 frame reconstruction and display-processing cost
  - a mailbox-style end-to-end live pipeline simulation

Outputs are written as JSON, CSV, and Markdown under docs/reports by default.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import ctypes.wintypes as wintypes
import datetime as _dt
import hashlib
import json
import math
import os
import platform
import queue
import socket
import statistics
import struct
import subprocess
import sys
import threading
import time
import traceback
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional, Tuple

import numpy as np
from PIL import Image

try:
    import onnx
except Exception:  # pragma: no cover - optional in case a lean env is used.
    onnx = None

import onnxruntime as ort


FRAME_WIDTH = 400
FRAME_HEIGHT = 400
PACKET_HEADER_BYTES = 4
LINE_PAYLOAD_BYTES = FRAME_WIDTH * 2
LOGICAL_PACKET_BYTES = PACKET_HEADER_BYTES + LINE_PAYLOAD_BYTES
PACKETS_PER_FRAME = FRAME_HEIGHT + 2
MODEL_INPUT_SIZE = 416

EXPAND5_TO_8 = np.asarray([(i << 3) | (i >> 2) for i in range(32)], dtype=np.uint8)
EXPAND6_TO_8 = np.asarray([(i << 2) | (i >> 4) for i in range(64)], dtype=np.uint8)


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def now_stamp() -> str:
    return _dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def run_text_command(command: List[str], cwd: Path, timeout: int = 10) -> str:
    try:
        completed = subprocess.run(
            command,
            cwd=str(cwd),
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        text = completed.stdout.strip()
        if not text:
            text = completed.stderr.strip()
        return text
    except Exception as exc:
        return f"unavailable: {exc}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except Exception:
        return default


def percentile(sorted_values: List[float], pct: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    rank = (len(sorted_values) - 1) * (pct / 100.0)
    low = int(math.floor(rank))
    high = int(math.ceil(rank))
    if low == high:
        return sorted_values[low]
    weight = rank - low
    return (sorted_values[low] * (1.0 - weight)) + (sorted_values[high] * weight)


def summarize_latencies_ms(values: Iterable[float]) -> Dict[str, float]:
    vals = [float(v) for v in values]
    if not vals:
        return {
            "samples": 0,
            "min_ms": 0.0,
            "mean_ms": 0.0,
            "median_ms": 0.0,
            "p90_ms": 0.0,
            "p95_ms": 0.0,
            "p99_ms": 0.0,
            "max_ms": 0.0,
            "stdev_ms": 0.0,
        }
    vals.sort()
    return {
        "samples": len(vals),
        "min_ms": vals[0],
        "mean_ms": statistics.fmean(vals),
        "median_ms": percentile(vals, 50),
        "p90_ms": percentile(vals, 90),
        "p95_ms": percentile(vals, 95),
        "p99_ms": percentile(vals, 99),
        "max_ms": vals[-1],
        "stdev_ms": statistics.pstdev(vals) if len(vals) > 1 else 0.0,
    }


def fmt_num(value: Any, digits: int = 2) -> str:
    if value is None:
        return ""
    try:
        numeric = float(value)
    except Exception:
        return str(value)
    if abs(numeric) >= 1000:
        return f"{numeric:,.{digits}f}"
    return f"{numeric:.{digits}f}"


def fmt_int(value: Any) -> str:
    try:
        return f"{int(round(float(value))):,}"
    except Exception:
        return str(value)


def current_rss_mb() -> Optional[float]:
    if os.name != "nt":
        try:
            import resource  # type: ignore

            usage = resource.getrusage(resource.RUSAGE_SELF)
            # ru_maxrss is KB on Linux and bytes on macOS. This repo is Windows,
            # but keep the fallback conservative.
            rss = float(usage.ru_maxrss)
            if rss > 1024 * 1024 * 16:
                return rss / (1024.0 * 1024.0)
            return rss / 1024.0
        except Exception:
            return None

    class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
        _fields_ = [
            ("cb", ctypes.c_ulong),
            ("PageFaultCount", ctypes.c_ulong),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
            ("PrivateUsage", ctypes.c_size_t),
        ]

    try:
        counters = PROCESS_MEMORY_COUNTERS_EX()
        counters.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS_EX)
        process = ctypes.windll.kernel32.GetCurrentProcess()
        psapi = ctypes.WinDLL("Psapi.dll", use_last_error=True)
        psapi.GetProcessMemoryInfo.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(PROCESS_MEMORY_COUNTERS_EX),
            wintypes.DWORD,
        ]
        psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
        ok = psapi.GetProcessMemoryInfo(
            process,
            ctypes.byref(counters),
            counters.cb,
        )
        if not ok:
            return None
        return float(counters.WorkingSetSize) / (1024.0 * 1024.0)
    except Exception:
        return None


class Monitor:
    def __init__(self, logical_cpus: int) -> None:
        self.logical_cpus = max(1, int(logical_cpus))
        self.start_wall = 0.0
        self.start_cpu = 0.0
        self.start_rss = current_rss_mb()
        self.peak_rss = self.start_rss

    def start(self) -> None:
        self.start_wall = time.perf_counter()
        self.start_cpu = time.process_time()
        self.start_rss = current_rss_mb()
        self.peak_rss = self.start_rss

    def sample_memory(self) -> None:
        rss = current_rss_mb()
        if rss is not None:
            self.peak_rss = rss if self.peak_rss is None else max(self.peak_rss, rss)

    def stop(self) -> Dict[str, float]:
        wall = max(1e-9, time.perf_counter() - self.start_wall)
        cpu = max(0.0, time.process_time() - self.start_cpu)
        rss_end = current_rss_mb()
        if rss_end is not None:
            self.peak_rss = rss_end if self.peak_rss is None else max(self.peak_rss, rss_end)
        return {
            "wall_s": wall,
            "process_cpu_s": cpu,
            "cpu_pct_one_core": (cpu / wall) * 100.0,
            "cpu_pct_machine": (cpu / wall) * 100.0 / float(self.logical_cpus),
            "rss_start_mb": self.start_rss if self.start_rss is not None else 0.0,
            "rss_end_mb": rss_end if rss_end is not None else 0.0,
            "rss_peak_mb": self.peak_rss if self.peak_rss is not None else 0.0,
            "rss_delta_mb": ((rss_end or 0.0) - (self.start_rss or 0.0)),
        }


def collect_system_info(repo: Path, model_path: Path) -> Dict[str, Any]:
    logical = os.cpu_count() or 1
    cpu_name = run_text_command(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)",
        ],
        repo,
    )
    cpu_cores = run_text_command(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "$p=Get-CimInstance Win32_Processor | Select-Object -First 1; \"$($p.NumberOfCores)/$($p.NumberOfLogicalProcessors)\"",
        ],
        repo,
    )
    os_info = run_text_command(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "$o=Get-CimInstance Win32_OperatingSystem; \"$($o.Caption) $($o.Version) build $($o.BuildNumber)\"",
        ],
        repo,
    )
    memory_info = run_text_command(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "$o=Get-CimInstance Win32_OperatingSystem; \"total_mb=$([math]::Round($o.TotalVisibleMemorySize/1024,1)) free_mb=$([math]::Round($o.FreePhysicalMemory/1024,1))\"",
        ],
        repo,
    )
    git_commit = run_text_command(["git", "rev-parse", "--short", "HEAD"], repo)
    git_branch = run_text_command(["git", "branch", "--show-current"], repo)
    git_status = run_text_command(["git", "status", "--short"], repo)
    dirty = bool(git_status.strip())

    model_info: Dict[str, Any] = {
        "path": str(model_path),
        "exists": model_path.exists(),
        "size_mb": model_path.stat().st_size / (1024.0 * 1024.0) if model_path.exists() else 0.0,
        "sha256": sha256_file(model_path) if model_path.exists() else "",
    }

    if onnx is not None and model_path.exists():
        try:
            model = onnx.load(str(model_path))
            model_info["ir_version"] = model.ir_version
            model_info["opsets"] = [
                {"domain": opset.domain or "ai.onnx", "version": opset.version}
                for opset in model.opset_import
            ]
            model_info["graph_nodes"] = len(model.graph.node)
            model_info["graph_inputs"] = [
                {
                    "name": value.name,
                    "shape": [
                        dim.dim_value if dim.dim_value else (dim.dim_param or "?")
                        for dim in value.type.tensor_type.shape.dim
                    ],
                }
                for value in model.graph.input
            ]
            model_info["graph_outputs"] = [
                {
                    "name": value.name,
                    "shape": [
                        dim.dim_value if dim.dim_value else (dim.dim_param or "?")
                        for dim in value.type.tensor_type.shape.dim
                    ],
                }
                for value in model.graph.output
            ]
        except Exception as exc:
            model_info["onnx_inspect_error"] = str(exc)

    return {
        "timestamp": _dt.datetime.now().isoformat(timespec="seconds"),
        "repo": str(repo),
        "python": sys.version.replace("\n", " "),
        "platform": platform.platform(),
        "os": os_info,
        "cpu_name": cpu_name,
        "cpu_cores_logical": cpu_cores,
        "logical_cpus": logical,
        "memory": memory_info,
        "numpy_version": np.__version__,
        "onnxruntime_version": ort.__version__,
        "onnxruntime_available_providers": ort.get_available_providers(),
        "git_branch": git_branch,
        "git_commit": git_commit,
        "git_dirty": dirty,
        "git_status_short": git_status,
        "model": model_info,
    }


def build_cpu_session(model_path: Path, intra_threads: int, inter_threads: int = 1) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    options.log_severity_level = 3
    options.intra_op_num_threads = max(1, int(intra_threads))
    options.inter_op_num_threads = max(1, int(inter_threads))
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    return ort.InferenceSession(
        str(model_path),
        sess_options=options,
        providers=["CPUExecutionProvider"],
    )


def get_input_shape(session: ort.InferenceSession) -> Tuple[str, Tuple[int, int, int, int]]:
    meta = session.get_inputs()[0]
    shape: List[int] = []
    for index, dim in enumerate(meta.shape):
        if isinstance(dim, int) and dim > 0:
            shape.append(dim)
        elif index == 0:
            shape.append(1)
        elif index == 1:
            shape.append(3)
        else:
            shape.append(MODEL_INPUT_SIZE)
    if len(shape) != 4:
        raise RuntimeError(f"expected NCHW input shape, got {meta.shape}")
    return meta.name, (shape[0], shape[1], shape[2], shape[3])


def load_reference_frame(repo: Path, width: int = FRAME_WIDTH, height: int = FRAME_HEIGHT) -> np.ndarray:
    candidates = [
        repo / "assets" / "demo_reference.png",
        repo / "assets" / "demo_reference.jpg",
        repo / "assets" / "demo_reference.jpeg",
        repo / "assets" / "demo_reference.bmp",
    ]
    for candidate in candidates:
        if candidate.exists():
            img = Image.open(candidate).convert("RGB").resize((width, height), Image.BILINEAR)
            return np.ascontiguousarray(np.asarray(img, dtype=np.uint8))

    y, x = np.mgrid[0:height, 0:width]
    base = np.zeros((height, width, 3), dtype=np.uint8)
    cx = width * 0.56
    cy = height * 0.66
    ellipse = ((x - cx) / (width * 0.34)) ** 2 + ((y - cy) / (height * 0.22)) ** 2
    glow = np.clip((1.0 - ellipse) * 255.0, 0, 255).astype(np.uint8)
    base[..., 0] = np.clip(glow * 0.85 + (x % 37), 0, 255)
    base[..., 1] = np.clip(glow + (y % 29), 0, 255)
    base[..., 2] = np.clip(glow * 0.32 + ((x + y) % 19), 0, 255)
    return np.ascontiguousarray(base)


def preprocess_frame(frame_rgb: np.ndarray, input_hw: Tuple[int, int]) -> np.ndarray:
    input_h, input_w = input_hw
    resized = Image.fromarray(frame_rgb).resize((input_w, input_h), Image.BILINEAR)
    blob = np.asarray(resized, dtype=np.float32) / 255.0
    return np.ascontiguousarray(np.transpose(blob, (2, 0, 1))[None, ...])


def iou_xywh(box_a: List[int], box_b: List[int]) -> float:
    ax1, ay1, aw, ah = box_a
    bx1, by1, bw, bh = box_b
    ax2 = ax1 + aw
    ay2 = ay1 + ah
    bx2 = bx1 + bw
    by2 = by1 + bh
    inter_x1 = max(ax1, bx1)
    inter_y1 = max(ay1, by1)
    inter_x2 = min(ax2, bx2)
    inter_y2 = min(ay2, by2)
    inter_w = max(0, inter_x2 - inter_x1)
    inter_h = max(0, inter_y2 - inter_y1)
    inter = inter_w * inter_h
    denom = aw * ah + bw * bh - inter
    if denom <= 0:
        return 0.0
    return float(inter) / float(denom)


def nms(boxes: List[List[int]], scores: List[float], score_threshold: float, iou_threshold: float) -> List[int]:
    order = [i for i, score in enumerate(scores) if score >= score_threshold]
    order.sort(key=lambda idx: scores[idx], reverse=True)
    keep: List[int] = []
    while order:
        current = order.pop(0)
        keep.append(current)
        order = [idx for idx in order if iou_xywh(boxes[current], boxes[idx]) <= iou_threshold]
    return keep


def decode_yolo_outputs(
    outputs: List[np.ndarray],
    frame_w: int,
    frame_h: int,
    input_size: int = MODEL_INPUT_SIZE,
    confidence: float = 0.85,
    nms_score: float = 0.3,
    nms_threshold: float = 0.5,
) -> int:
    if not outputs:
        return 0
    try:
        output = outputs[0].reshape(5, -1).T
    except Exception:
        return 0

    boxes: List[List[int]] = []
    scores: List[float] = []
    scale_x = frame_w / float(input_size)
    scale_y = frame_h / float(input_size)
    for row in output:
        cx, cy, w, h, score = row[:5]
        score_f = float(score)
        if score_f < confidence:
            continue
        x1 = max(0, min(frame_w, int((float(cx) - (float(w) / 2.0)) * scale_x)))
        y1 = max(0, min(frame_h, int((float(cy) - (float(h) / 2.0)) * scale_y)))
        x2 = max(0, min(frame_w, int((float(cx) + (float(w) / 2.0)) * scale_x)))
        y2 = max(0, min(frame_h, int((float(cy) + (float(h) / 2.0)) * scale_y)))
        boxes.append([x1, y1, max(0, x2 - x1), max(0, y2 - y1)])
        scores.append(score_f)
    return len(nms(boxes, scores, nms_score, nms_threshold))


def timed_benchmark(
    logical_cpus: int,
    duration_s: float,
    warmup_iters: int,
    op: Callable[[], Any],
    latency_collector: Optional[List[float]] = None,
    memory_sample_interval_s: float = 0.5,
) -> Tuple[int, Dict[str, float]]:
    for _ in range(max(0, warmup_iters)):
        op()
    monitor = Monitor(logical_cpus)
    monitor.start()
    next_memory_sample = time.perf_counter() + memory_sample_interval_s
    count = 0
    deadline = time.perf_counter() + duration_s
    while time.perf_counter() < deadline:
        t0 = time.perf_counter()
        op()
        t1 = time.perf_counter()
        if latency_collector is not None:
            latency_collector.append((t1 - t0) * 1000.0)
        count += 1
        if t1 >= next_memory_sample:
            monitor.sample_memory()
            next_memory_sample = t1 + memory_sample_interval_s
    metrics = monitor.stop()
    return count, metrics


def make_result(
    suite: str,
    scenario: str,
    metrics: Dict[str, Any],
    latency_ms: Optional[List[float]] = None,
    notes: str = "",
) -> Dict[str, Any]:
    result: Dict[str, Any] = {
        "suite": suite,
        "scenario": scenario,
        "notes": notes,
    }
    result.update(metrics)
    if latency_ms is not None:
        result.update(summarize_latencies_ms(latency_ms))
    return result


def benchmark_inference_thread_sweep(
    repo: Path,
    model_path: Path,
    logical_cpus: int,
    duration_s: float,
    warmup_iters: int,
    thread_counts: List[int],
) -> List[Dict[str, Any]]:
    print("[1/6] ONNX CPU runtime-only thread sweep")
    results: List[Dict[str, Any]] = []
    for threads in thread_counts:
        session = build_cpu_session(model_path, threads)
        input_name, shape = get_input_shape(session)
        _, _, input_h, input_w = shape
        frame = load_reference_frame(repo)
        blob = preprocess_frame(frame, (input_h, input_w))
        latencies: List[float] = []

        def op() -> Any:
            return session.run(None, {input_name: blob})

        count, m = timed_benchmark(logical_cpus, duration_s, warmup_iters, op, latencies)
        m.update(
            {
                "iterations": count,
                "throughput_fps": count / max(1e-9, m["wall_s"]),
                "ort_intra_threads": threads,
                "ort_inter_threads": 1,
                "input_shape": list(shape),
                "provider": "CPUExecutionProvider",
            }
        )
        results.append(
            make_result(
                "onnx_runtime_only",
                f"intra_threads_{threads}",
                m,
                latencies,
                "Precomputed NCHW float32 blob; measures model forward() only.",
            )
        )
        print(f"      threads={threads:<2} fps={m['throughput_fps']:.2f} p95={summarize_latencies_ms(latencies)['p95_ms']:.2f} ms")
    return results


def benchmark_inference_pipeline(
    repo: Path,
    model_path: Path,
    logical_cpus: int,
    duration_s: float,
    warmup_iters: int,
    thread_counts: List[int],
) -> List[Dict[str, Any]]:
    print("[2/6] ONNX CPU preprocess + inference + decode")
    results: List[Dict[str, Any]] = []
    frame = load_reference_frame(repo)
    for threads in thread_counts:
        session = build_cpu_session(model_path, threads)
        input_name, shape = get_input_shape(session)
        _, _, input_h, input_w = shape
        latencies: List[float] = []
        detection_counts: List[int] = []

        def op() -> Any:
            blob = preprocess_frame(frame, (input_h, input_w))
            outputs = session.run(None, {input_name: blob})
            detections = decode_yolo_outputs(outputs, frame.shape[1], frame.shape[0], input_w)
            detection_counts.append(detections)
            return detections

        count, m = timed_benchmark(logical_cpus, duration_s, warmup_iters, op, latencies)
        m.update(
            {
                "iterations": count,
                "throughput_fps": count / max(1e-9, m["wall_s"]),
                "ort_intra_threads": threads,
                "ort_inter_threads": 1,
                "input_shape": list(shape),
                "provider": "CPUExecutionProvider",
                "detections_mean": statistics.fmean(detection_counts) if detection_counts else 0.0,
                "detections_max": max(detection_counts) if detection_counts else 0,
            }
        )
        results.append(
            make_result(
                "onnx_full_pipeline",
                f"intra_threads_{threads}",
                m,
                latencies,
                "Pillow resize + NCHW conversion + ONNX Runtime CPU + current YOLO decode/NMS semantics.",
            )
        )
        print(f"      threads={threads:<2} fps={m['throughput_fps']:.2f} p95={summarize_latencies_ms(latencies)['p95_ms']:.2f} ms")
    return results


def benchmark_inference_concurrency(
    repo: Path,
    model_path: Path,
    logical_cpus: int,
    duration_s: float,
    warmup_iters: int,
    concurrency_levels: List[int],
    intra_threads_per_session: int,
) -> List[Dict[str, Any]]:
    print("[3/6] Concurrent ONNX CPU sessions")
    results: List[Dict[str, Any]] = []
    frame = load_reference_frame(repo)

    for concurrency in concurrency_levels:
        barrier = threading.Barrier(concurrency + 1)
        deadline_box = {"deadline": 0.0}
        worker_results: "queue.Queue[Dict[str, Any]]" = queue.Queue()
        error_box: "queue.Queue[str]" = queue.Queue()

        def worker(worker_id: int) -> None:
            try:
                session = build_cpu_session(model_path, intra_threads_per_session)
                input_name, shape = get_input_shape(session)
                _, _, input_h, input_w = shape
                blob = preprocess_frame(frame, (input_h, input_w))
                for _ in range(max(0, warmup_iters)):
                    session.run(None, {input_name: blob})
                latencies: List[float] = []
                barrier.wait()
                while time.perf_counter() < deadline_box["deadline"]:
                    t0 = time.perf_counter()
                    session.run(None, {input_name: blob})
                    latencies.append((time.perf_counter() - t0) * 1000.0)
                worker_results.put(
                    {
                        "worker_id": worker_id,
                        "iterations": len(latencies),
                        "latencies_ms": latencies,
                    }
                )
            except Exception:
                error_box.put(traceback.format_exc())
                try:
                    barrier.abort()
                except Exception:
                    pass

        threads = [threading.Thread(target=worker, args=(i,), daemon=True) for i in range(concurrency)]
        monitor = Monitor(logical_cpus)
        for thread in threads:
            thread.start()
        monitor.start()
        deadline_box["deadline"] = time.perf_counter() + duration_s
        barrier.wait()
        for thread in threads:
            thread.join(timeout=duration_s + 60.0)
        m = monitor.stop()

        if not error_box.empty():
            raise RuntimeError(error_box.get())

        all_latencies: List[float] = []
        worker_iterations: List[int] = []
        while not worker_results.empty():
            item = worker_results.get()
            worker_iterations.append(int(item["iterations"]))
            all_latencies.extend(item["latencies_ms"])
        total = len(all_latencies)
        m.update(
            {
                "iterations": total,
                "throughput_fps": total / max(1e-9, m["wall_s"]),
                "concurrency": concurrency,
                "ort_intra_threads": intra_threads_per_session,
                "ort_inter_threads": 1,
                "provider": "CPUExecutionProvider",
                "worker_min_iterations": min(worker_iterations) if worker_iterations else 0,
                "worker_max_iterations": max(worker_iterations) if worker_iterations else 0,
            }
        )
        results.append(
            make_result(
                "onnx_concurrent_sessions",
                f"{concurrency}_sessions_x_intra_{intra_threads_per_session}",
                m,
                all_latencies,
                "Each worker owns a CPUExecutionProvider session and reuses a precomputed blob.",
            )
        )
        summary = summarize_latencies_ms(all_latencies)
        print(f"      sessions={concurrency:<2} fps={m['throughput_fps']:.2f} p95={summary['p95_ms']:.2f} ms")
    return results


def make_rgb565_source(width: int, height: int) -> np.ndarray:
    y, x = np.mgrid[0:height, 0:width]
    r = ((x * 3 + y * 2) & 0xFF).astype(np.uint8)
    g = ((x * 5 + y * 7 + 31) & 0xFF).astype(np.uint8)
    b = ((x * 11 + y * 13 + 17) & 0xFF).astype(np.uint8)
    rgb565 = (((r.astype(np.uint16) >> 3) & 0x1F) << 11) | (((g.astype(np.uint16) >> 2) & 0x3F) << 5) | ((b.astype(np.uint16) >> 3) & 0x1F)
    frame = np.empty((height, width, 2), dtype=np.uint8)
    frame[..., 0] = (rgb565 >> 8).astype(np.uint8)
    frame[..., 1] = (rgb565 & 0xFF).astype(np.uint8)
    return np.ascontiguousarray(frame)


def rgb565_to_rgb888(frame_bytes: np.ndarray) -> np.ndarray:
    high = frame_bytes[..., 0].astype(np.uint16)
    low = frame_bytes[..., 1].astype(np.uint16)
    rgb565 = (high << 8) | low
    rgb = np.empty((frame_bytes.shape[0], frame_bytes.shape[1], 3), dtype=np.uint8)
    rgb[..., 0] = EXPAND5_TO_8[(rgb565 >> 11) & 0x1F]
    rgb[..., 1] = EXPAND6_TO_8[(rgb565 >> 5) & 0x3F]
    rgb[..., 2] = EXPAND5_TO_8[rgb565 & 0x1F]
    return rgb


def build_line_loss_masks(height: int, loss_rate: float, count: int = 256) -> List[np.ndarray]:
    rng = np.random.default_rng(20260320 + int(loss_rate * 10000))
    masks: List[np.ndarray] = []
    for _ in range(count):
        mask = np.ones(height, dtype=bool)
        if loss_rate > 0:
            lost = rng.random(height) < loss_rate
            mask[lost] = False
            if not mask.any():
                mask[rng.integers(0, height)] = True
        masks.append(mask)
    return masks


def recover_missing_lines(frame_bytes: np.ndarray, valid_lines: np.ndarray) -> np.ndarray:
    if valid_lines.all():
        return frame_bytes
    recovered = frame_bytes.copy()
    height = frame_bytes.shape[0]
    for i in np.flatnonzero(~valid_lines):
        top = i - 1 if i > 0 and valid_lines[i - 1] else -1
        bottom = i + 1 if i + 1 < height and valid_lines[i + 1] else -1
        if top >= 0 and bottom >= 0:
            recovered[i] = ((frame_bytes[top].astype(np.uint16) + frame_bytes[bottom].astype(np.uint16)) // 2).astype(np.uint8)
        elif top >= 0:
            recovered[i] = frame_bytes[top]
        elif bottom >= 0:
            recovered[i] = frame_bytes[bottom]
        else:
            recovered[i].fill(0)
    return recovered


def build_tone_lut(brightness: int, gamma: int) -> np.ndarray:
    brightness_offset = brightness - 50
    gamma_exponent = math.pow(2.0, -float(gamma) / 100.0)
    values = []
    for i in range(256):
        normalized = min(255, max(0, i + brightness_offset)) / 255.0
        corrected = math.pow(normalized, gamma_exponent)
        values.append(int(min(255.0, max(0.0, corrected * 255.0))))
    return np.asarray(values, dtype=np.uint8)


def box_blur_rgb(image: np.ndarray, radius: int) -> np.ndarray:
    if radius <= 0:
        return image
    # One separable pass approximates the cost shape of the Gaussian blur used
    # by the app while keeping the benchmark dependency-free.
    padded = np.pad(image, ((0, 0), (radius, radius), (0, 0)), mode="edge")
    acc = np.zeros_like(image, dtype=np.uint16)
    for offset in range(radius * 2 + 1):
        acc += padded[:, offset : offset + image.shape[1], :].astype(np.uint16)
    horizontal = (acc // (radius * 2 + 1)).astype(np.uint8)

    padded_v = np.pad(horizontal, ((radius, radius), (0, 0), (0, 0)), mode="edge")
    acc_v = np.zeros_like(horizontal, dtype=np.uint16)
    for offset in range(radius * 2 + 1):
        acc_v += padded_v[offset : offset + image.shape[0], :, :].astype(np.uint16)
    return (acc_v // (radius * 2 + 1)).astype(np.uint8)


def apply_display_processing(
    rgb: np.ndarray,
    brightness: int,
    gamma: int,
    sharpness: int,
    denoise: int,
    flip_h: bool,
    flip_v: bool,
) -> np.ndarray:
    if brightness == 50 and gamma == 0 and sharpness == 0 and denoise == 0:
        dest = rgb.copy()
    else:
        lut = build_tone_lut(brightness, gamma)
        dest = lut[rgb]
        if denoise > 0:
            radius = max(1, denoise // 20)
            dest = box_blur_rgb(dest, radius)
        if sharpness > 0:
            blurred = box_blur_rgb(dest, 1)
            amount = float(sharpness) / 50.0
            sharpened = dest.astype(np.float32) * (1.0 + amount) - blurred.astype(np.float32) * amount
            dest = np.clip(sharpened, 0, 255).astype(np.uint8)
    if flip_v:
        dest = dest[::-1, :, :]
    if flip_h:
        dest = dest[:, ::-1, :]
    return np.ascontiguousarray(dest)


def benchmark_frame_processing(
    logical_cpus: int,
    duration_s: float,
    warmup_iters: int,
) -> List[Dict[str, Any]]:
    print("[4/6] RGB565 frame reconstruction and display processing")
    results: List[Dict[str, Any]] = []
    scenarios = [
        {
            "name": "400x400_default_0pct_loss",
            "width": 400,
            "height": 400,
            "loss": 0.0,
            "brightness": 50,
            "gamma": 0,
            "sharpness": 0,
            "denoise": 0,
            "flip_h": False,
            "flip_v": False,
        },
        {
            "name": "400x400_default_1pct_loss",
            "width": 400,
            "height": 400,
            "loss": 0.01,
            "brightness": 50,
            "gamma": 0,
            "sharpness": 0,
            "denoise": 0,
            "flip_h": False,
            "flip_v": False,
        },
        {
            "name": "400x400_heavy_tuning_0pct_loss",
            "width": 400,
            "height": 400,
            "loss": 0.0,
            "brightness": 64,
            "gamma": 35,
            "sharpness": 60,
            "denoise": 45,
            "flip_h": True,
            "flip_v": True,
        },
        {
            "name": "400x400_heavy_tuning_5pct_loss",
            "width": 400,
            "height": 400,
            "loss": 0.05,
            "brightness": 64,
            "gamma": 35,
            "sharpness": 60,
            "denoise": 45,
            "flip_h": True,
            "flip_v": True,
        },
        {
            "name": "800x800_projection_heavy_1pct_loss",
            "width": 800,
            "height": 800,
            "loss": 0.01,
            "brightness": 64,
            "gamma": 35,
            "sharpness": 60,
            "denoise": 45,
            "flip_h": True,
            "flip_v": True,
        },
    ]

    for scenario in scenarios:
        width = int(scenario["width"])
        height = int(scenario["height"])
        source = make_rgb565_source(width, height)
        masks = build_line_loss_masks(height, float(scenario["loss"]))
        state = {"index": 0}
        latencies: List[float] = []

        def op() -> np.ndarray:
            idx = state["index"]
            state["index"] = idx + 1
            data = recover_missing_lines(source, masks[idx % len(masks)])
            rgb = rgb565_to_rgb888(data)
            return apply_display_processing(
                rgb,
                int(scenario["brightness"]),
                int(scenario["gamma"]),
                int(scenario["sharpness"]),
                int(scenario["denoise"]),
                bool(scenario["flip_h"]),
                bool(scenario["flip_v"]),
            )

        count, m = timed_benchmark(logical_cpus, duration_s, warmup_iters, op, latencies)
        payload_mb = (width * height * 2 * count) / (1024.0 * 1024.0)
        m.update(
            {
                "iterations": count,
                "throughput_fps": count / max(1e-9, m["wall_s"]),
                "frame_width": width,
                "frame_height": height,
                "line_loss_rate": float(scenario["loss"]),
                "input_payload_mb_s": payload_mb / max(1e-9, m["wall_s"]),
                "brightness": scenario["brightness"],
                "gamma": scenario["gamma"],
                "sharpness": scenario["sharpness"],
                "denoise": scenario["denoise"],
                "flip_h": int(bool(scenario["flip_h"])),
                "flip_v": int(bool(scenario["flip_v"])),
            }
        )
        results.append(
            make_result(
                "frame_processing",
                str(scenario["name"]),
                m,
                latencies,
                "Dependency-free NumPy surrogate of worker hot paths: line recovery, RGB565 expansion, tone/filter/flip.",
            )
        )
        summary = summarize_latencies_ms(latencies)
        print(f"      {scenario['name']:<36} fps={m['throughput_fps']:.2f} p95={summary['p95_ms']:.2f} ms")
    return results


def udp_stress_once(
    logical_cpus: int,
    target_pps: int,
    packet_size: int,
    sender_threads: int,
    duration_s: float,
    recv_buffer_bytes: int,
) -> Dict[str, Any]:
    bind_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bind_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, recv_buffer_bytes)
    bind_sock.bind(("127.0.0.1", 0))
    bind_sock.settimeout(0.05)
    actual_recv_buffer = bind_sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
    port = bind_sock.getsockname()[1]
    stop_event = threading.Event()
    barrier = threading.Barrier(sender_threads + 2)

    recv_stats = {
        "received": 0,
        "bytes": 0,
        "gap_loss": 0,
        "duplicates_or_reorder": 0,
        "last_seq": [-1 for _ in range(sender_threads)],
    }
    sent_counts = [0 for _ in range(sender_threads)]
    sent_bytes = [0 for _ in range(sender_threads)]

    def receiver() -> None:
        barrier.wait()
        while not stop_event.is_set():
            try:
                data, _ = bind_sock.recvfrom(max(65536, packet_size + 64))
            except socket.timeout:
                continue
            except OSError:
                break
            recv_stats["received"] += 1
            recv_stats["bytes"] += len(data)
            if len(data) >= 8:
                sender_id, seq = struct.unpack_from("!II", data, 0)
                if 0 <= sender_id < sender_threads:
                    last = recv_stats["last_seq"][sender_id]
                    if last >= 0:
                        if seq == last:
                            recv_stats["duplicates_or_reorder"] += 1
                        elif seq < last:
                            recv_stats["duplicates_or_reorder"] += 1
                        elif seq > last + 1:
                            recv_stats["gap_loss"] += seq - last - 1
                    if seq > last:
                        recv_stats["last_seq"][sender_id] = seq

    def sender(sender_id: int) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        packet = bytearray(max(8, packet_size))
        payload_tail = bytes((sender_id + i) & 0xFF for i in range(max(0, packet_size - 8)))
        packet[8:] = payload_tail
        per_sender_pps = max(1.0, float(target_pps) / float(sender_threads))
        period_s = 0.01
        burst = max(1, int(round(per_sender_pps * period_s)))
        seq = 0
        barrier.wait()
        next_tick = time.perf_counter()
        deadline = next_tick + duration_s
        while time.perf_counter() < deadline:
            for _ in range(burst):
                struct.pack_into("!II", packet, 0, sender_id, seq & 0xFFFFFFFF)
                try:
                    sock.sendto(packet, ("127.0.0.1", port))
                    sent_counts[sender_id] += 1
                    sent_bytes[sender_id] += packet_size
                except OSError:
                    pass
                seq += 1
            next_tick += period_s
            delay = next_tick - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            else:
                next_tick = time.perf_counter()
        sock.close()

    receiver_thread = threading.Thread(target=receiver, daemon=True)
    sender_thread_list = [threading.Thread(target=sender, args=(i,), daemon=True) for i in range(sender_threads)]

    monitor = Monitor(logical_cpus)
    receiver_thread.start()
    for thread in sender_thread_list:
        thread.start()
    monitor.start()
    barrier.wait()
    for thread in sender_thread_list:
        thread.join(timeout=duration_s + 5.0)
    time.sleep(0.35)
    stop_event.set()
    receiver_thread.join(timeout=2.0)
    bind_sock.close()
    m = monitor.stop()

    sent = sum(sent_counts)
    sent_b = sum(sent_bytes)
    received = int(recv_stats["received"])
    recv_b = int(recv_stats["bytes"])
    inferred_loss = max(0, sent - received)
    gap_loss = int(recv_stats["gap_loss"])
    m.update(
        {
            "target_pps": target_pps,
            "packet_size_bytes": packet_size,
            "sender_threads": sender_threads,
            "socket_recv_buffer_requested": recv_buffer_bytes,
            "socket_recv_buffer_actual": actual_recv_buffer,
            "sent_packets": sent,
            "received_packets": received,
            "inferred_lost_packets": inferred_loss,
            "gap_lost_packets": gap_loss,
            "duplicates_or_reorder": int(recv_stats["duplicates_or_reorder"]),
            "send_pps": sent / max(1e-9, m["wall_s"]),
            "recv_pps": received / max(1e-9, m["wall_s"]),
            "send_mbps_payload": (sent_b * 8.0 / 1_000_000.0) / max(1e-9, m["wall_s"]),
            "recv_mbps_payload": (recv_b * 8.0 / 1_000_000.0) / max(1e-9, m["wall_s"]),
            "loss_pct_sent_minus_received": (inferred_loss / float(sent) * 100.0) if sent else 0.0,
            "gap_loss_pct": (gap_loss / float(sent) * 100.0) if sent else 0.0,
        }
    )
    return m


def benchmark_udp_ingest(
    logical_cpus: int,
    duration_s: float,
    recv_buffer_bytes: int,
) -> List[Dict[str, Any]]:
    print("[5/6] UDP localhost packet ingest pressure")
    scenarios = [
        (24000, LOGICAL_PACKET_BYTES, 1),
        (48000, LOGICAL_PACKET_BYTES, 2),
        (96000, LOGICAL_PACKET_BYTES, 4),
        (144000, LOGICAL_PACKET_BYTES, 4),
        (48000, 1300, 2),
        (96000, 1300, 4),
    ]
    results: List[Dict[str, Any]] = []
    for target_pps, packet_size, sender_threads in scenarios:
        metrics = udp_stress_once(
            logical_cpus,
            target_pps=target_pps,
            packet_size=packet_size,
            sender_threads=sender_threads,
            duration_s=duration_s,
            recv_buffer_bytes=recv_buffer_bytes,
        )
        scenario = f"{target_pps}_pps_{packet_size}_bytes_{sender_threads}_senders"
        metrics["iterations"] = metrics["received_packets"]
        results.append(
            make_result(
                "udp_loopback_ingest",
                scenario,
                metrics,
                None,
                "Python localhost UDP sender/receiver with sequence headers; validates host/socket pressure independently of Qt UI.",
            )
        )
        print(
            f"      target={target_pps:<6} size={packet_size:<4} recv={metrics['recv_pps']:.0f}pps "
            f"loss={metrics['loss_pct_sent_minus_received']:.3f}%"
        )
    return results


def benchmark_end_to_end_mailbox(
    repo: Path,
    model_path: Path,
    logical_cpus: int,
    duration_s: float,
    inference_threads: int,
    target_producer_fps: int = 60,
) -> List[Dict[str, Any]]:
    print("[6/6] End-to-end mailbox simulation")
    session = build_cpu_session(model_path, inference_threads)
    input_name, shape = get_input_shape(session)
    _, _, input_h, input_w = shape
    source_565 = make_rgb565_source(FRAME_WIDTH, FRAME_HEIGHT)
    line_masks = build_line_loss_masks(FRAME_HEIGHT, 0.01)
    latest_lock = threading.Lock()
    stop_event = threading.Event()
    latest: Dict[str, Any] = {"frame": None, "seq": -1, "submitted": 0}
    producer_latencies: List[float] = []
    inference_latencies: List[float] = []
    decoded_counts: List[int] = []
    producer_count = {"value": 0}
    inferred_sequences: List[int] = []

    def producer() -> None:
        next_tick = time.perf_counter()
        frame_period = 1.0 / float(target_producer_fps)
        idx = 0
        while not stop_event.is_set():
            t0 = time.perf_counter()
            data = recover_missing_lines(source_565, line_masks[idx % len(line_masks)])
            rgb = rgb565_to_rgb888(data)
            processed = apply_display_processing(rgb, 56, 20, 20, 20, False, False)
            t1 = time.perf_counter()
            with latest_lock:
                latest["frame"] = processed
                latest["seq"] = idx
                latest["submitted"] += 1
            producer_latencies.append((t1 - t0) * 1000.0)
            producer_count["value"] += 1
            idx += 1
            next_tick += frame_period
            delay = next_tick - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            else:
                next_tick = time.perf_counter()

    def inference_worker() -> None:
        last_seq = -1
        while not stop_event.is_set():
            with latest_lock:
                frame = latest["frame"]
                seq = latest["seq"]
            if frame is None or seq == last_seq:
                time.sleep(0.001)
                continue
            last_seq = seq
            t0 = time.perf_counter()
            blob = preprocess_frame(frame, (input_h, input_w))
            outputs = session.run(None, {input_name: blob})
            decoded_counts.append(decode_yolo_outputs(outputs, frame.shape[1], frame.shape[0], input_w))
            inference_latencies.append((time.perf_counter() - t0) * 1000.0)
            inferred_sequences.append(seq)

    monitor = Monitor(logical_cpus)
    producer_thread = threading.Thread(target=producer, daemon=True)
    inference_thread = threading.Thread(target=inference_worker, daemon=True)
    monitor.start()
    producer_thread.start()
    inference_thread.start()
    time.sleep(duration_s)
    stop_event.set()
    producer_thread.join(timeout=5.0)
    inference_thread.join(timeout=10.0)
    m = monitor.stop()

    produced = producer_count["value"]
    inferred = len(inference_latencies)
    dropped_or_overwritten = max(0, produced - inferred)
    sequence_gap = 0
    if inferred_sequences:
        last = inferred_sequences[0]
        for seq in inferred_sequences[1:]:
            if seq > last + 1:
                sequence_gap += seq - last - 1
            last = seq

    m.update(
        {
            "iterations": inferred,
            "produced_frames": produced,
            "inferred_frames": inferred,
            "producer_target_fps": target_producer_fps,
            "producer_actual_fps": produced / max(1e-9, m["wall_s"]),
            "inference_actual_fps": inferred / max(1e-9, m["wall_s"]),
            "mailbox_overwritten_frames": dropped_or_overwritten,
            "mailbox_sequence_gap_frames": sequence_gap,
            "ort_intra_threads": inference_threads,
            "provider": "CPUExecutionProvider",
            "producer_mean_ms": summarize_latencies_ms(producer_latencies)["mean_ms"],
            "producer_p95_ms": summarize_latencies_ms(producer_latencies)["p95_ms"],
            "detections_mean": statistics.fmean(decoded_counts) if decoded_counts else 0.0,
        }
    )
    result = make_result(
        "end_to_end_mailbox",
        f"producer_{target_producer_fps}fps_intra_{inference_threads}",
        m,
        inference_latencies,
        "Simulates current latest-frame mailbox: frame reconstruction at fixed cadence, one CPU inference worker consumes newest frame.",
    )
    print(
        f"      produced={produced} inferred={inferred} infer_fps={m['inference_actual_fps']:.2f} "
        f"overwritten={dropped_or_overwritten}"
    )
    return [result]


def choose_thread_counts(logical_cpus: int, suite: str) -> List[int]:
    if suite == "smoke":
        return [1, min(4, logical_cpus)]
    candidates = [1, 2, 4, 8, 12, 16, logical_cpus]
    result: List[int] = []
    for value in candidates:
        value = max(1, min(logical_cpus, value))
        if value not in result:
            result.append(value)
    return result


def flatten_for_csv(results: List[Dict[str, Any]]) -> List[str]:
    preferred = [
        "suite",
        "scenario",
        "provider",
        "iterations",
        "throughput_fps",
        "samples",
        "mean_ms",
        "median_ms",
        "p95_ms",
        "p99_ms",
        "max_ms",
        "wall_s",
        "process_cpu_s",
        "cpu_pct_one_core",
        "cpu_pct_machine",
        "rss_peak_mb",
        "ort_intra_threads",
        "concurrency",
        "target_pps",
        "recv_pps",
        "send_pps",
        "loss_pct_sent_minus_received",
        "gap_loss_pct",
        "frame_width",
        "frame_height",
        "line_loss_rate",
        "input_payload_mb_s",
        "producer_actual_fps",
        "inference_actual_fps",
        "mailbox_overwritten_frames",
        "notes",
    ]
    keys = set()
    for item in results:
        keys.update(item.keys())
    ordered = [key for key in preferred if key in keys]
    ordered.extend(sorted(keys - set(ordered)))
    return ordered


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def json_safe_args(args: argparse.Namespace) -> Dict[str, Any]:
    safe: Dict[str, Any] = {}
    for key, value in vars(args).items():
        if isinstance(value, Path):
            safe[key] = str(value)
        else:
            safe[key] = value
    return safe


def write_csv(path: Path, results: List[Dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = flatten_for_csv(results)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in results:
            writer.writerow(row)


def table(headers: List[str], rows: List[List[Any]]) -> str:
    lines = []
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("| " + " | ".join(["---"] * len(headers)) + " |")
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def best_by(results: List[Dict[str, Any]], suite: str, key: str, reverse: bool = True) -> Optional[Dict[str, Any]]:
    candidates = [item for item in results if item.get("suite") == suite]
    if not candidates:
        return None
    return sorted(candidates, key=lambda item: safe_float(item.get(key)), reverse=reverse)[0]


def write_markdown_report(path: Path, system: Dict[str, Any], results: List[Dict[str, Any]], args: argparse.Namespace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    best_runtime = best_by(results, "onnx_runtime_only", "throughput_fps", True)
    best_pipeline = best_by(results, "onnx_full_pipeline", "throughput_fps", True)
    best_frame_400 = sorted(
        [r for r in results if r.get("suite") == "frame_processing" and r.get("frame_width") == 400],
        key=lambda r: safe_float(r.get("throughput_fps")),
        reverse=True,
    )
    udp_rows = [r for r in results if r.get("suite") == "udp_loopback_ingest"]
    e2e = best_by(results, "end_to_end_mailbox", "inference_actual_fps", True)

    lines: List[str] = []
    lines.append("# CPU 性能压测报告")
    lines.append("")
    lines.append(f"- 生成时间: {system.get('timestamp')}")
    lines.append(f"- 项目路径: `{system.get('repo')}`")
    lines.append(f"- Git: branch=`{system.get('git_branch')}`, commit=`{system.get('git_commit')}`, dirty=`{system.get('git_dirty')}`")
    lines.append(f"- CPU: {system.get('cpu_name')} ({system.get('cpu_cores_logical')} cores/logical)")
    lines.append(f"- OS: {system.get('os')}")
    lines.append(f"- Memory: {system.get('memory')}")
    lines.append(f"- Python: `{system.get('python')}`")
    lines.append(f"- NumPy/ONNX Runtime: `{system.get('numpy_version')}` / `{system.get('onnxruntime_version')}`")
    lines.append(f"- ONNX Runtime available providers: `{system.get('onnxruntime_available_providers')}`")
    lines.append("")
    lines.append("## 结论摘要")
    lines.append("")
    lines.append("本报告强制使用 `CPUExecutionProvider`，没有使用当前机器可用的 DirectML provider，因此数值反映 CPU 端压力。")
    if best_runtime:
        lines.append(
            f"- 模型纯 forward 最优场景: `{best_runtime['scenario']}`，吞吐 {fmt_num(best_runtime.get('throughput_fps'))} FPS，"
            f"P95 {fmt_num(best_runtime.get('p95_ms'))} ms。"
        )
    if best_pipeline:
        lines.append(
            f"- 完整 AI 链路最优场景: `{best_pipeline['scenario']}`，吞吐 {fmt_num(best_pipeline.get('throughput_fps'))} FPS，"
            f"P95 {fmt_num(best_pipeline.get('p95_ms'))} ms。"
        )
    if best_frame_400:
        lines.append(
            f"- 当前代码常量为 400x400；400x400 帧处理最快 {fmt_num(best_frame_400[0].get('throughput_fps'))} FPS，"
            f"最重 400x400 场景约 {fmt_num(best_frame_400[-1].get('throughput_fps'))} FPS。"
        )
    if udp_rows:
        worst_udp = sorted(udp_rows, key=lambda r: safe_float(r.get("loss_pct_sent_minus_received")), reverse=True)[0]
        best_udp = sorted(udp_rows, key=lambda r: safe_float(r.get("loss_pct_sent_minus_received")))[0]
        lines.append(
            f"- UDP localhost 压力最低丢包场景: `{best_udp['scenario']}`，接收 {fmt_num(best_udp.get('recv_pps'), 0)} pps，"
            f"丢包 {fmt_num(best_udp.get('loss_pct_sent_minus_received'), 4)}%。"
        )
        lines.append(
            f"- UDP localhost 压力最高丢包场景: `{worst_udp['scenario']}`，接收 {fmt_num(worst_udp.get('recv_pps'), 0)} pps，"
            f"丢包 {fmt_num(worst_udp.get('loss_pct_sent_minus_received'), 4)}%。"
        )
    if e2e:
        lines.append(
            f"- 端到端 mailbox 模拟: 生产 {fmt_num(e2e.get('producer_actual_fps'))} FPS，CPU 推理 {fmt_num(e2e.get('inference_actual_fps'))} FPS，"
            f"被覆盖/跳过帧 {fmt_int(e2e.get('mailbox_overwritten_frames'))}。"
        )
    lines.append("")
    lines.append("## 测试边界与方法")
    lines.append("")
    lines.append("- 网络测试使用 localhost UDP，验证主机 UDP socket 和 CPU 包处理压力，不等价于 FPGA 真实链路的网卡/驱动/交换机丢包。")
    lines.append("- 帧处理测试复刻当前 worker 的关键语义: 丢线恢复、RGB565 到 RGB888、亮度/gamma LUT、降噪、锐化、翻转。OpenCV GaussianBlur 在脚本中用依赖更少的 NumPy separable blur 近似。")
    lines.append("- AI 测试复刻 `YoloProcessor` / `onnx_helper.py` 的输入尺寸、NCHW float32、输出 reshape 与 NMS 语义，并强制 CPU provider。")
    lines.append("- 当前源码 `UdpFramePipelineWorker` 的活跃分辨率常量是 400x400；报告额外包含 800x800 projection，用来评估 README 中旧目标分辨率的扩展风险。")
    lines.append("")
    lines.append("## 模型信息")
    lines.append("")
    model = system.get("model", {})
    lines.append(f"- 模型: `{model.get('path')}`")
    lines.append(f"- 大小: {fmt_num(model.get('size_mb'))} MB")
    lines.append(f"- SHA256: `{model.get('sha256')}`")
    if "graph_inputs" in model:
        lines.append(f"- 输入: `{model.get('graph_inputs')}`")
    if "graph_outputs" in model:
        lines.append(f"- 输出: `{model.get('graph_outputs')}`")
    lines.append(f"- 节点数: `{model.get('graph_nodes', 'unknown')}`, opset: `{model.get('opsets', 'unknown')}`")
    lines.append("")

    runtime_rows = [
        [
            r.get("scenario"),
            fmt_int(r.get("ort_intra_threads")),
            fmt_num(r.get("throughput_fps")),
            fmt_num(r.get("mean_ms")),
            fmt_num(r.get("p95_ms")),
            fmt_num(r.get("p99_ms")),
            fmt_num(r.get("cpu_pct_one_core")),
            fmt_num(r.get("cpu_pct_machine")),
            fmt_num(r.get("rss_peak_mb")),
        ]
        for r in results
        if r.get("suite") == "onnx_runtime_only"
    ]
    if runtime_rows:
        lines.append("## AI 模型纯推理 CPU 压测")
        lines.append("")
        lines.append(table(["场景", "intra", "FPS", "Mean ms", "P95 ms", "P99 ms", "CPU 单核%", "CPU整机%", "RSS峰值MB"], runtime_rows))
        lines.append("")

    pipeline_rows = [
        [
            r.get("scenario"),
            fmt_int(r.get("ort_intra_threads")),
            fmt_num(r.get("throughput_fps")),
            fmt_num(r.get("mean_ms")),
            fmt_num(r.get("p95_ms")),
            fmt_num(r.get("p99_ms")),
            fmt_num(r.get("detections_mean")),
            fmt_num(r.get("cpu_pct_one_core")),
            fmt_num(r.get("rss_peak_mb")),
        ]
        for r in results
        if r.get("suite") == "onnx_full_pipeline"
    ]
    if pipeline_rows:
        lines.append("## AI 完整链路 CPU 压测")
        lines.append("")
        lines.append(table(["场景", "intra", "FPS", "Mean ms", "P95 ms", "P99 ms", "平均检测数", "CPU 单核%", "RSS峰值MB"], pipeline_rows))
        lines.append("")

    concurrent_rows = [
        [
            r.get("scenario"),
            fmt_int(r.get("concurrency")),
            fmt_int(r.get("ort_intra_threads")),
            fmt_num(r.get("throughput_fps")),
            fmt_num(r.get("mean_ms")),
            fmt_num(r.get("p95_ms")),
            fmt_num(r.get("p99_ms")),
            fmt_num(r.get("cpu_pct_one_core")),
            fmt_num(r.get("rss_peak_mb")),
            f"{fmt_int(r.get('worker_min_iterations'))}/{fmt_int(r.get('worker_max_iterations'))}",
        ]
        for r in results
        if r.get("suite") == "onnx_concurrent_sessions"
    ]
    if concurrent_rows:
        lines.append("## 并发 AI 推理 CPU 压测")
        lines.append("")
        lines.append(table(["场景", "并发session", "每session intra", "总FPS", "Mean ms", "P95 ms", "P99 ms", "CPU 单核%", "RSS峰值MB", "worker迭代min/max"], concurrent_rows))
        lines.append("")

    frame_rows = [
        [
            r.get("scenario"),
            f"{fmt_int(r.get('frame_width'))}x{fmt_int(r.get('frame_height'))}",
            fmt_num(safe_float(r.get("line_loss_rate")) * 100.0, 2),
            fmt_num(r.get("throughput_fps")),
            fmt_num(r.get("mean_ms")),
            fmt_num(r.get("p95_ms")),
            fmt_num(r.get("p99_ms")),
            fmt_num(r.get("input_payload_mb_s")),
            fmt_num(r.get("cpu_pct_one_core")),
            fmt_num(r.get("rss_peak_mb")),
        ]
        for r in results
        if r.get("suite") == "frame_processing"
    ]
    if frame_rows:
        lines.append("## 帧重建与图像处理 CPU 压测")
        lines.append("")
        lines.append(table(["场景", "分辨率", "丢线%", "FPS", "Mean ms", "P95 ms", "P99 ms", "输入MB/s", "CPU 单核%", "RSS峰值MB"], frame_rows))
        lines.append("")

    udp_table_rows = [
        [
            r.get("scenario"),
            fmt_int(r.get("target_pps")),
            fmt_int(r.get("packet_size_bytes")),
            fmt_int(r.get("sender_threads")),
            fmt_num(r.get("send_pps"), 0),
            fmt_num(r.get("recv_pps"), 0),
            fmt_num(r.get("recv_mbps_payload")),
            fmt_num(r.get("loss_pct_sent_minus_received"), 4),
            fmt_num(r.get("gap_loss_pct"), 4),
            fmt_num(r.get("cpu_pct_one_core")),
        ]
        for r in results
        if r.get("suite") == "udp_loopback_ingest"
    ]
    if udp_table_rows:
        lines.append("## UDP 并发吞吐压测")
        lines.append("")
        lines.append(table(["场景", "目标pps", "包大小", "发送线程", "发送pps", "接收pps", "接收Mbps", "推断丢包%", "序号gap%", "CPU 单核%"], udp_table_rows))
        lines.append("")

    e2e_rows = [
        [
            r.get("scenario"),
            fmt_num(r.get("producer_actual_fps")),
            fmt_num(r.get("inference_actual_fps")),
            fmt_num(r.get("producer_mean_ms")),
            fmt_num(r.get("producer_p95_ms")),
            fmt_num(r.get("mean_ms")),
            fmt_num(r.get("p95_ms")),
            fmt_int(r.get("mailbox_overwritten_frames")),
            fmt_num(r.get("cpu_pct_one_core")),
            fmt_num(r.get("rss_peak_mb")),
        ]
        for r in results
        if r.get("suite") == "end_to_end_mailbox"
    ]
    if e2e_rows:
        lines.append("## 端到端 mailbox 模拟")
        lines.append("")
        lines.append(table(["场景", "生产FPS", "推理FPS", "生产Mean ms", "生产P95 ms", "推理Mean ms", "推理P95 ms", "覆盖帧", "CPU 单核%", "RSS峰值MB"], e2e_rows))
        lines.append("")

    lines.append("## 风险判断")
    lines.append("")
    lines.append("- 如果 CPU-only AI P95 明显高于 16.7 ms，则 60 FPS 输入下不能做到逐帧 CPU 推理，必须继续保持 latest-frame mailbox/drop stale 策略。")
    lines.append("- 如果 UDP localhost 在 96k pps 以上出现丢包，真实硬件链路还需要分离验证: 网卡中断合并、接收缓冲、Npcap/QUdpSocket 差异、FPGA 发送 pacing。")
    lines.append("- 400x400 与 800x800 的帧处理耗时差异用于提示分辨率扩展成本；若恢复 README 的 800x800 目标，应先把 C++ 热路径做 native micro-benchmark。")
    lines.append("- 当前 AI fallback helper 默认会优先选择 DirectML；本报告为了 CPU 侧结论强制 CPU provider，实际 UI 开启 AI 时可能比本报告更快。")
    lines.append("")
    lines.append("## 优化建议")
    lines.append("")
    lines.append("1. AI CPU 路径: 以本报告中吞吐最高且 P95 稳定的 intra thread 数作为 CPU fallback 默认值，避免盲目使用所有逻辑核造成抢占。")
    lines.append("2. AI 调度: 保持单 worker/latest-frame mailbox，不建议在当前 CPU-only 配置下开多个并发推理 session，除非并发表显示总 FPS 随 session 数线性上升且 P95 可接受。")
    lines.append("3. UDP 接收: 真实硬件测试时同时记录 app stats 中 `pkts/s`、`dropped pkts/s`、`queue max` 与 Windows 网卡计数器，定位丢包发生在网卡、socket 还是 worker 队列。")
    lines.append("4. 帧处理: 重图像处理参数打开时，优先优化 OpenCV filter 调用和 flip 的内存访问；默认显示路径已经主要受 RGB565 扩展和内存带宽影响。")
    lines.append("5. 报告复跑: 改模型、改分辨率、改 OpenMP 或 ONNX Runtime 线程配置后，使用同一脚本复跑，比较 JSON/CSV 中同名场景。")
    lines.append("")
    lines.append("## 复现命令")
    lines.append("")
    lines.append("```powershell")
    lines.append(f".\\.local\\py310-yolo\\Scripts\\python.exe scripts\\cpu_stress_bench.py --suite {args.suite}")
    lines.append("```")
    lines.append("")
    lines.append("## 原始输出")
    lines.append("")
    lines.append(f"- JSON: `{path.with_suffix('.json')}`")
    lines.append(f"- CSV: `{path.with_suffix('.csv')}`")
    lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    repo = repo_root_from_script()
    default_output = repo / "docs" / "reports" / f"cpu_stress_report_{now_stamp()}.md"
    parser = argparse.ArgumentParser(description="CPU stress benchmark suite for this repo.")
    parser.add_argument("--suite", choices=["smoke", "full"], default="full", help="smoke is for script validation; full is the report run.")
    parser.add_argument("--repo", type=Path, default=repo)
    parser.add_argument("--model", type=Path, default=repo / "models" / "best.onnx")
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--inference-duration", type=float, default=None)
    parser.add_argument("--frame-duration", type=float, default=None)
    parser.add_argument("--udp-duration", type=float, default=None)
    parser.add_argument("--e2e-duration", type=float, default=None)
    parser.add_argument("--warmup-iters", type=int, default=None)
    parser.add_argument("--udp-recv-buffer", type=int, default=16 * 1024 * 1024)
    parser.add_argument("--concurrency-intra", type=int, default=2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    model_path = args.model.resolve()
    output_md = args.output.resolve()
    output_json = output_md.with_suffix(".json")
    output_csv = output_md.with_suffix(".csv")

    if not model_path.exists():
        raise FileNotFoundError(f"model not found: {model_path}")

    logical_cpus = os.cpu_count() or 1
    if args.suite == "smoke":
        inference_duration = args.inference_duration if args.inference_duration is not None else 1.0
        frame_duration = args.frame_duration if args.frame_duration is not None else 1.0
        udp_duration = args.udp_duration if args.udp_duration is not None else 1.0
        e2e_duration = args.e2e_duration if args.e2e_duration is not None else 2.0
        warmup_iters = args.warmup_iters if args.warmup_iters is not None else 1
        concurrency_levels = [1, 2]
    else:
        inference_duration = args.inference_duration if args.inference_duration is not None else 8.0
        frame_duration = args.frame_duration if args.frame_duration is not None else 6.0
        udp_duration = args.udp_duration if args.udp_duration is not None else 6.0
        e2e_duration = args.e2e_duration if args.e2e_duration is not None else 15.0
        warmup_iters = args.warmup_iters if args.warmup_iters is not None else 3
        concurrency_levels = [1, 2, 4, 8]

    thread_counts = choose_thread_counts(logical_cpus, args.suite)
    pipeline_thread_counts = [value for value in thread_counts if value in {1, 2, 4, 8, 16, logical_cpus}]
    if logical_cpus not in pipeline_thread_counts:
        pipeline_thread_counts.append(logical_cpus)

    print("CPU stress benchmark starting")
    print(f"  repo:  {repo}")
    print(f"  model: {model_path}")
    print(f"  suite: {args.suite}")
    print(f"  output:{output_md}")
    print(f"  logical CPUs: {logical_cpus}")
    print(f"  ORT providers available: {ort.get_available_providers()}")
    print("  forced provider: CPUExecutionProvider")

    system = collect_system_info(repo, model_path)
    results: List[Dict[str, Any]] = []
    started = time.perf_counter()

    results.extend(
        benchmark_inference_thread_sweep(
            repo,
            model_path,
            logical_cpus,
            duration_s=inference_duration,
            warmup_iters=warmup_iters,
            thread_counts=thread_counts,
        )
    )
    results.extend(
        benchmark_inference_pipeline(
            repo,
            model_path,
            logical_cpus,
            duration_s=inference_duration,
            warmup_iters=warmup_iters,
            thread_counts=pipeline_thread_counts,
        )
    )
    results.extend(
        benchmark_inference_concurrency(
            repo,
            model_path,
            logical_cpus,
            duration_s=inference_duration,
            warmup_iters=warmup_iters,
            concurrency_levels=concurrency_levels,
            intra_threads_per_session=max(1, int(args.concurrency_intra)),
        )
    )
    results.extend(
        benchmark_frame_processing(
            logical_cpus,
            duration_s=frame_duration,
            warmup_iters=warmup_iters,
        )
    )
    results.extend(
        benchmark_udp_ingest(
            logical_cpus,
            duration_s=udp_duration,
            recv_buffer_bytes=int(args.udp_recv_buffer),
        )
    )
    best_pipeline = best_by(results, "onnx_full_pipeline", "throughput_fps", True)
    e2e_threads = int(best_pipeline.get("ort_intra_threads")) if best_pipeline else min(8, logical_cpus)
    results.extend(
        benchmark_end_to_end_mailbox(
            repo,
            model_path,
            logical_cpus,
            duration_s=e2e_duration,
            inference_threads=e2e_threads,
            target_producer_fps=60,
        )
    )

    elapsed = time.perf_counter() - started
    payload = {
        "system": system,
        "args": json_safe_args(args),
        "elapsed_s": elapsed,
        "results": results,
    }
    write_json(output_json, payload)
    write_csv(output_csv, results)
    write_markdown_report(output_md, system, results, args)

    print("Benchmark complete")
    print(f"  elapsed: {elapsed:.1f}s")
    print(f"  report:  {output_md}")
    print(f"  json:    {output_json}")
    print(f"  csv:     {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
