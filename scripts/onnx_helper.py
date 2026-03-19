import base64
import io
import json
import sys
import time

import numpy as np
import onnxruntime as ort
from PIL import Image


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


def main():
    if len(sys.argv) < 2:
        emit({"ok": False, "error": "missing model path"})
        return 1

    model_path = sys.argv[1]

    try:
        session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        input_name = session.get_inputs()[0].name
        emit({"ok": True, "backend": "onnxruntime", "model": model_path, "input": input_name})
    except Exception as exc:
        emit({"ok": False, "error": f"session init failed: {exc}"})
        return 2

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            req = json.loads(line)
            raw = base64.b64decode(req["image"])
            frame_rgb = np.array(Image.open(io.BytesIO(raw)).convert("RGB"))

            input_size = int(req.get("input_size", 416))
            confidence = float(req.get("confidence", 0.85))
            nms_score = float(req.get("nms_score", 0.3))
            nms_threshold = float(req.get("nms_threshold", 0.5))

            resized = Image.fromarray(frame_rgb).resize((input_size, input_size), Image.BILINEAR)
            blob = np.asarray(resized, dtype=np.float32) / 255.0
            blob = np.transpose(blob, (2, 0, 1))[None, ...]

            start = time.perf_counter()
            outputs = session.run(None, {input_name: blob})
            inference_ms = int((time.perf_counter() - start) * 1000.0)

            output = outputs[0].reshape(5, -1).T
            boxes = []
            scores = []
            scale_x = frame_rgb.shape[1] / float(input_size)
            scale_y = frame_rgb.shape[0] / float(input_size)

            for row in output:
                cx, cy, w, h, score = row[:5]
                if float(score) < confidence:
                    continue

                x1 = max(0, min(frame_rgb.shape[1], int((cx - (w / 2.0)) * scale_x)))
                y1 = max(0, min(frame_rgb.shape[0], int((cy - (h / 2.0)) * scale_y)))
                x2 = max(0, min(frame_rgb.shape[1], int((cx + (w / 2.0)) * scale_x)))
                y2 = max(0, min(frame_rgb.shape[0], int((cy + (h / 2.0)) * scale_y)))

                boxes.append([x1, y1, max(0, x2 - x1), max(0, y2 - y1)])
                scores.append(float(score))

            packed = [boxes[idx] for idx in nms(boxes, scores, nms_score, nms_threshold)]

            emit({"ok": True, "boxes": packed, "inference_ms": inference_ms})
        except Exception as exc:
            emit({"ok": False, "error": str(exc)})

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
