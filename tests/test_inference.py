"""Run with: python -m unittest discover -s tests -p 'test_*.py'."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "src/backend/inference/onnx_helper.py"
spec = importlib.util.spec_from_file_location("onnx_helper", HELPER)
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class InferenceTests(unittest.TestCase):
    def test_binary_framing_and_rejection(self):
        request = {"width": 2, "height": 1, "stride": 8, "image_rgb24_bytes": 8}
        stream = io.BytesIO(b"123456xxNEXT")
        self.assertEqual(helper.decode_frame(request, stream)[0], b"123456xx")
        self.assertEqual(stream.read(), b"NEXT")
        with self.assertRaises(EOFError):
            helper.decode_frame(request, io.BytesIO(b"short"))
        for count in (-1, 0, 7, 9):
            with self.assertRaises(ValueError):
                helper.decode_frame(dict(request, image_rgb24_bytes=count), io.BytesIO())

    def test_nms_matches_reference(self):
        boxes = np.array([[0, 0, 20, 20], [1, 1, 20, 20], [80, 80, 5, 5]])
        scores = np.array([0.9, 0.8, 0.7])
        self.assertEqual(helper.InferenceEngine.nms_numpy(boxes, scores, 0.3, 0.5),
                         helper.nms(boxes.tolist(), scores.tolist(), 0.3, 0.5))

    def test_real_model_binary_protocol(self):
        raw = bytes([32, 180, 96]) * 400 * 400
        request = {"width": 400, "height": 400, "stride": 1200,
                   "image_rgb24_bytes": len(raw), "input_size": 416}
        wire = json.dumps(request).encode() + b"\n" + raw
        result = subprocess.run([sys.executable, "-I", str(HELPER), str(ROOT / "models/best.onnx")],
                                input=wire * 2, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(replies), 3)
        for reply in replies:
            self.assertTrue(reply["ok"], reply)
        self.assertEqual(replies[0]["request_protocol"], "json-header+rgb24-binary-v1")
        self.assertEqual(replies[1]["boxes"], replies[2]["boxes"])


if __name__ == "__main__":
    unittest.main()
