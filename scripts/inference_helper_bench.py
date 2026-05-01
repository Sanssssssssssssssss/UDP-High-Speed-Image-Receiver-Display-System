#!/usr/bin/env python3
"""Benchmark the Python ONNX helper request path.

This focuses on the real app fallback boundary: a long-lived helper process
fed with either the legacy JSON/base64 payload or the optimized binary RGB24
payload. It reports round-trip latency as seen by the caller plus helper-side
decode/preprocess/inference/postprocess timings.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List

from PIL import Image


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[1]


def percentile(values: List[float], pct: float) -> float:
    if not values:
        return 0.0
    values = sorted(values)
    rank = (len(values) - 1) * (pct / 100.0)
    low = int(rank)
    high = min(len(values) - 1, low + 1)
    if low == high:
        return values[low]
    weight = rank - low
    return (values[low] * (1.0 - weight)) + (values[high] * weight)


def summarize(values: List[float]) -> Dict[str, float]:
    if not values:
        return {"mean_ms": 0.0, "p95_ms": 0.0, "min_ms": 0.0, "max_ms": 0.0}
    return {
        "mean_ms": statistics.fmean(values),
        "p95_ms": percentile(values, 95),
        "min_ms": min(values),
        "max_ms": max(values),
    }


def load_frame(repo: Path, width: int, height: int) -> bytes:
    candidates = [
        repo / "assets" / "demo_reference.png",
        repo / "assets" / "demo_reference.jpg",
        repo / "assets" / "demo_reference.jpeg",
        repo / "assets" / "demo_reference.bmp",
    ]
    for candidate in candidates:
        if candidate.exists():
            return Image.open(candidate).convert("RGB").resize((width, height), Image.BILINEAR).tobytes()
    return Image.new("RGB", (width, height), (32, 180, 96)).tobytes()


def start_helper(args: argparse.Namespace) -> subprocess.Popen:
    env = os.environ.copy()
    if args.provider:
        env["POST_TRAIN_ORT_PROVIDER"] = args.provider
    if args.intra_threads:
        env["POST_TRAIN_ORT_INTRA_THREADS"] = str(args.intra_threads)
    env["POST_TRAIN_ORT_WARMUP"] = "1" if args.warmup else "0"

    return subprocess.Popen(
        [str(args.python), "scripts/onnx_helper.py", str(args.model)],
        cwd=str(args.repo),
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )


def run_protocol(args: argparse.Namespace, protocol: str, raw: bytes) -> Dict[str, Any]:
    proc = start_helper(args)
    assert proc.stdin is not None
    assert proc.stdout is not None

    ready_line = proc.stdout.readline()
    ready = json.loads(ready_line)
    if not ready.get("ok"):
        stderr = proc.stderr.read().decode("utf-8", errors="replace") if proc.stderr else ""
        raise RuntimeError(f"helper failed to start: {ready} {stderr}")

    base_request = {
        "width": args.width,
        "height": args.height,
        "stride": args.width * 3,
        "input_size": args.input_size,
        "confidence": 0.85,
        "nms_score": 0.3,
        "nms_threshold": 0.5,
    }

    round_trip: List[float] = []
    helper_total: List[float] = []
    decode_ms: List[float] = []
    preprocess_ms: List[float] = []
    inference_ms: List[float] = []
    postprocess_ms: List[float] = []
    boxes = []

    for index in range(args.iterations + args.warmup_iterations):
        request = dict(base_request)
        started = time.perf_counter()
        if protocol == "binary":
            request["protocol"] = "rgb24-binary-v1"
            request["image_rgb24_bytes"] = len(raw)
            proc.stdin.write(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")
            proc.stdin.write(raw)
        else:
            request["image_rgb24"] = base64.b64encode(raw).decode("ascii")
            proc.stdin.write(json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n")

        proc.stdin.flush()
        response = json.loads(proc.stdout.readline())
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        if not response.get("ok"):
            raise RuntimeError(response)

        if index >= args.warmup_iterations:
            round_trip.append(elapsed_ms)
            helper_total.append(float(response.get("total_ms", 0.0)))
            decode_ms.append(float(response.get("decode_ms", 0.0)))
            preprocess_ms.append(float(response.get("preprocess_ms", 0.0)))
            inference_ms.append(float(response.get("inference_ms", 0.0)))
            postprocess_ms.append(float(response.get("postprocess_ms", 0.0)))
            boxes = response.get("boxes", [])

    proc.stdin.close()
    proc.terminate()
    proc.wait(timeout=5)

    result = {
        "protocol": protocol,
        "provider": ready.get("provider"),
        "intra_threads": ready.get("intra_threads"),
        "warmup_ms": ready.get("warmup_ms"),
        "boxes": len(boxes),
        "round_trip": summarize(round_trip),
        "helper_total": summarize(helper_total),
        "decode": summarize(decode_ms),
        "preprocess": summarize(preprocess_ms),
        "inference": summarize(inference_ms),
        "postprocess": summarize(postprocess_ms),
    }
    return result


def parse_args() -> argparse.Namespace:
    repo = repo_root_from_script()
    parser = argparse.ArgumentParser(description="Benchmark optimized ONNX helper request protocols.")
    parser.add_argument("--repo", type=Path, default=repo)
    parser.add_argument("--python", type=Path, default=repo / ".local" / "py310-yolo" / "Scripts" / "python.exe")
    parser.add_argument("--model", type=Path, default=repo / "models" / "best.onnx")
    parser.add_argument("--provider", default="", help="Provider override, e.g. CPUExecutionProvider or DmlExecutionProvider.")
    parser.add_argument("--intra-threads", type=int, default=4)
    parser.add_argument("--width", type=int, default=400)
    parser.add_argument("--height", type=int, default=400)
    parser.add_argument("--input-size", type=int, default=416)
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--warmup-iterations", type=int, default=10)
    parser.add_argument("--warmup", action="store_true", default=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.repo = args.repo.resolve()
    args.python = args.python.resolve()
    args.model = args.model.resolve()
    raw = load_frame(args.repo, args.width, args.height)

    results = [
        run_protocol(args, "base64", raw),
        run_protocol(args, "binary", raw),
    ]
    print(json.dumps(results, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
