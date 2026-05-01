import base64
import io
import json
import os
import sys
import time

import numpy as np
import onnxruntime as ort
from PIL import Image


DEFAULT_INPUT_SIZE = 416


def iou(box_a, box_b):
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
    inter_area = inter_w * inter_h
    if inter_area <= 0:
        return 0.0

    area_a = aw * ah
    area_b = bw * bh
    denom = float(area_a + area_b - inter_area)
    if denom <= 0.0:
        return 0.0
    return inter_area / denom


def nms(boxes, scores, score_threshold, iou_threshold):
    order = [i for i, s in enumerate(scores) if s >= score_threshold]
    order.sort(key=lambda idx: scores[idx], reverse=True)
    keep = []
    while order:
        current = order.pop(0)
        keep.append(current)
        remaining = []
        for idx in order:
            if iou(boxes[current], boxes[idx]) <= iou_threshold:
                remaining.append(idx)
        order = remaining
    return keep


def emit(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def read_int_env(name, default_value):
    value = os.environ.get(name, "").strip()
    if not value:
        return default_value
    try:
        return int(value)
    except ValueError:
        return default_value


def resolve_provider_order(available):
    forced = os.environ.get("POST_TRAIN_ORT_PROVIDER", "").strip()
    aliases = {
        "cpu": "CPUExecutionProvider",
        "dml": "DmlExecutionProvider",
        "directml": "DmlExecutionProvider",
        "cuda": "CUDAExecutionProvider",
        "openvino": "OpenVINOExecutionProvider",
    }
    if forced:
        provider = aliases.get(forced.lower(), forced)
        if provider in available:
            return [provider]

    preferred = [
        "DmlExecutionProvider",
        "CUDAExecutionProvider",
        "OpenVINOExecutionProvider",
        "CPUExecutionProvider",
    ]
    providers = [name for name in preferred if name in available]
    return providers or ["CPUExecutionProvider"]


def build_session(model_path):
    session_options = ort.SessionOptions()
    session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    session_options.log_severity_level = 3
    cpu_count = os.cpu_count() or 4
    default_threads = max(1, min(4, cpu_count - 1))
    session_options.intra_op_num_threads = max(1, read_int_env("POST_TRAIN_ORT_INTRA_THREADS", default_threads))
    session_options.inter_op_num_threads = 1
    session_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    session_options.enable_cpu_mem_arena = True
    session_options.enable_mem_pattern = True

    available = ort.get_available_providers()
    providers = resolve_provider_order(available)

    session = ort.InferenceSession(model_path, sess_options=session_options, providers=providers)
    return session, providers[0], available, session_options.intra_op_num_threads


def get_model_input_hw(session):
    shape = session.get_inputs()[0].shape
    if len(shape) == 4 and isinstance(shape[2], int) and isinstance(shape[3], int):
        return int(shape[2]), int(shape[3])
    return DEFAULT_INPUT_SIZE, DEFAULT_INPUT_SIZE


def read_binary_payload(input_stream, byte_count):
    payload = input_stream.read(byte_count)
    if len(payload) != byte_count:
        raise EOFError(f"expected {byte_count} raw bytes, got {len(payload)}")
    return payload


def decode_frame(req, input_stream):
    if "image_rgb24_bytes" in req:
        width = int(req["width"])
        height = int(req["height"])
        stride = int(req.get("stride", width * 3))
        raw = read_binary_payload(input_stream, int(req["image_rgb24_bytes"]))
        return raw, width, height, stride, "rgb24-binary"

    if "image_rgb24" in req:
        width = int(req["width"])
        height = int(req["height"])
        stride = int(req.get("stride", width * 3))
        raw = base64.b64decode(req["image_rgb24"])
        return raw, width, height, stride, "rgb24-base64"

    raw = base64.b64decode(req["image"])
    image = Image.open(io.BytesIO(raw)).convert("RGB")
    return image.tobytes(), image.width, image.height, image.width * 3, "encoded-image"


class InferenceEngine:
    def __init__(self, session, input_name, output_names, default_input_hw):
        self.session = session
        self.input_name = input_name
        self.output_names = output_names
        self.default_input_hw = default_input_hw
        self.blob_cache = {}

    def blob_for_size(self, input_h, input_w):
        key = (input_h, input_w)
        blob = self.blob_cache.get(key)
        if blob is None:
            blob = np.empty((1, 3, input_h, input_w), dtype=np.float32)
            self.blob_cache[key] = blob
        return blob

    def preprocess_rgb24(self, raw, width, height, stride, input_size):
        if input_size <= 0:
            input_h, input_w = self.default_input_hw
        else:
            input_h = input_size
            input_w = input_size

        image = Image.frombuffer("RGB", (width, height), raw, "raw", "RGB", stride, 1)
        if image.size != (input_w, input_h):
            image = image.resize((input_w, input_h), Image.BILINEAR)

        resized_u8 = np.asarray(image, dtype=np.uint8)
        blob = self.blob_for_size(input_h, input_w)
        np.multiply(np.transpose(resized_u8, (2, 0, 1)), 1.0 / 255.0, out=blob[0], casting="unsafe")
        return blob, input_w

    def run(self, blob):
        return self.session.run(self.output_names, {self.input_name: blob})

    @staticmethod
    def nms_numpy(boxes, scores, score_threshold, iou_threshold):
        if boxes.size == 0:
            return []

        order = np.flatnonzero(scores >= score_threshold)
        if order.size == 0:
            return []

        order = order[np.argsort(scores[order])[::-1]]
        x1 = boxes[:, 0].astype(np.float32)
        y1 = boxes[:, 1].astype(np.float32)
        w = boxes[:, 2].astype(np.float32)
        h = boxes[:, 3].astype(np.float32)
        x2 = x1 + w
        y2 = y1 + h
        areas = np.maximum(0.0, w) * np.maximum(0.0, h)

        keep = []
        while order.size > 0:
            current = int(order[0])
            keep.append(current)
            if order.size == 1:
                break

            rest = order[1:]
            inter_x1 = np.maximum(x1[current], x1[rest])
            inter_y1 = np.maximum(y1[current], y1[rest])
            inter_x2 = np.minimum(x2[current], x2[rest])
            inter_y2 = np.minimum(y2[current], y2[rest])
            inter_w = np.maximum(0.0, inter_x2 - inter_x1)
            inter_h = np.maximum(0.0, inter_y2 - inter_y1)
            inter_area = inter_w * inter_h
            denom = areas[current] + areas[rest] - inter_area
            iou_values = np.divide(inter_area, denom, out=np.zeros_like(inter_area), where=denom > 0.0)
            order = rest[iou_values <= iou_threshold]
        return keep

    def decode_outputs(self, outputs, frame_width, frame_height, input_size, confidence, nms_score, nms_threshold):
        if not outputs:
            return []

        output = outputs[0].reshape(5, -1).T
        scores = output[:, 4]
        candidate_mask = scores >= confidence
        if not np.any(candidate_mask):
            return []

        rows = output[candidate_mask]
        candidate_scores = scores[candidate_mask].astype(np.float32, copy=False)
        scale_x = frame_width / float(input_size)
        scale_y = frame_height / float(input_size)

        cx = rows[:, 0].astype(np.float32, copy=False)
        cy = rows[:, 1].astype(np.float32, copy=False)
        width = rows[:, 2].astype(np.float32, copy=False)
        height = rows[:, 3].astype(np.float32, copy=False)

        x1 = np.clip((cx - (width * 0.5)) * scale_x, 0, frame_width).astype(np.int32)
        y1 = np.clip((cy - (height * 0.5)) * scale_y, 0, frame_height).astype(np.int32)
        x2 = np.clip((cx + (width * 0.5)) * scale_x, 0, frame_width).astype(np.int32)
        y2 = np.clip((cy + (height * 0.5)) * scale_y, 0, frame_height).astype(np.int32)

        boxes = np.column_stack((x1, y1, np.maximum(0, x2 - x1), np.maximum(0, y2 - y1))).astype(np.int32, copy=False)
        valid = (boxes[:, 2] > 0) & (boxes[:, 3] > 0)
        if not np.any(valid):
            return []

        boxes = boxes[valid]
        candidate_scores = candidate_scores[valid]
        keep = self.nms_numpy(boxes, candidate_scores, nms_score, nms_threshold)
        return boxes[keep].tolist()


def main():
    if len(sys.argv) < 2:
        emit({"ok": False, "error": "missing model path"})
        return 1

    model_path = sys.argv[1]

    try:
        session, active_provider, available_providers, intra_threads = build_session(model_path)
        input_name = session.get_inputs()[0].name
        output_names = [output.name for output in session.get_outputs()]
        input_hw = get_model_input_hw(session)
        engine = InferenceEngine(session, input_name, output_names, input_hw)

        warmup_ms = 0
        if read_int_env("POST_TRAIN_ORT_WARMUP", 1) > 0:
            warmup_blob = np.zeros((1, 3, input_hw[0], input_hw[1]), dtype=np.float32)
            warmup_start = time.perf_counter()
            session.run(output_names, {input_name: warmup_blob})
            warmup_ms = int((time.perf_counter() - warmup_start) * 1000.0)

        emit({
            "ok": True,
            "backend": "onnxruntime",
            "provider": active_provider,
            "providers": available_providers,
            "intra_threads": intra_threads,
            "model": model_path,
            "input": input_name,
            "outputs": output_names,
            "input_hw": input_hw,
            "request_protocol": "json-header+rgb24-binary-v1",
            "warmup_ms": warmup_ms,
        })
    except Exception as exc:
        emit({"ok": False, "error": f"session init failed: {exc}"})
        return 2

    input_stream = sys.stdin.buffer
    while True:
        line = input_stream.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue

        try:
            total_start = time.perf_counter()
            req = json.loads(line)
            raw, width, height, stride, protocol = decode_frame(req, input_stream)
            decode_done = time.perf_counter()

            input_size = int(req.get("input_size", DEFAULT_INPUT_SIZE))
            confidence = float(req.get("confidence", 0.85))
            nms_score = float(req.get("nms_score", 0.3))
            nms_threshold = float(req.get("nms_threshold", 0.5))

            blob, effective_input_size = engine.preprocess_rgb24(raw, width, height, stride, input_size)
            preprocess_done = time.perf_counter()

            outputs = engine.run(blob)
            inference_done = time.perf_counter()

            packed = engine.decode_outputs(outputs, width, height, effective_input_size, confidence, nms_score, nms_threshold)
            postprocess_done = time.perf_counter()

            emit({
                "ok": True,
                "boxes": packed,
                "inference_ms": int((inference_done - preprocess_done) * 1000.0),
                "decode_ms": int((decode_done - total_start) * 1000.0),
                "preprocess_ms": int((preprocess_done - decode_done) * 1000.0),
                "postprocess_ms": int((postprocess_done - inference_done) * 1000.0),
                "total_ms": int((postprocess_done - total_start) * 1000.0),
                "protocol": protocol,
            })
        except Exception as exc:
            emit({"ok": False, "error": str(exc)})

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
