"""Build a relocatable Windows x64 ZIP; run with a Python that has pip installed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--name", default="udp-vision-windows-x64")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows packaging requires a native Windows Python")
    if Path(args.name).name != args.name or args.name in ("", ".", ".."):
        parser.error("--name must be a single directory name")
    stage = ROOT / "dist" / args.name
    # Refuse stale packages instead of deleting a caller-provided directory.
    stage.mkdir(parents=True, exist_ok=False)
    run("cmake", "--install", args.build_dir, "--config", "Release", "--prefix", stage)
    qmake = shutil.which("qmake-qt5") or shutil.which("qmake")
    if not qmake:
        raise RuntimeError("Qt 5 qmake (qmake-qt5 on MSYS2) must be on PATH")
    plugin_root = Path(subprocess.check_output([qmake, "-query", "QT_INSTALL_PLUGINS"], text=True).strip())
    # Copy explicit plugins: old Qt deployment tools can misclassify release DLLs
    # with debug symbols. No plugin heuristics or unrelated QML modules needed.
    for relative in ["platforms/qwindows.dll", "platforms/qoffscreen.dll",
                     "imageformats/qjpeg.dll", "imageformats/qgif.dll", "imageformats/qico.dll"]:
        destination = stage / relative
        destination.parent.mkdir(exist_ok=True)
        shutil.copy2(plugin_root / relative, destination)
    (stage / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n")
    # OpenCV 3.x loads its FFmpeg plugin dynamically, outside the import table.
    for directory in os.environ["PATH"].split(os.pathsep):
        if directory and Path(directory).is_dir():
            for dll in Path(directory).glob("*opencv*ffmpeg*.dll"):
                shutil.copy2(dll, stage / dll.name)
    run("cmake", f"-DSTAGE={stage.as_posix()}", "-P", ROOT / "cmake/BundleRuntime.cmake")

    runtime = stage / "runtime/python"
    runtime.mkdir(parents=True)
    archive = ROOT / ".local/python-3.13.7-embed-amd64.zip"
    archive.parent.mkdir(exist_ok=True)
    url = "https://www.python.org/ftp/python/3.13.7/python-3.13.7-embed-amd64.zip"
    if not archive.exists():
        urllib.request.urlretrieve(url, archive)
    expected = (ROOT / "scripts/python-embed.sha256").read_text().strip().split()[0]
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise RuntimeError("Embedded Python checksum mismatch")
    with zipfile.ZipFile(archive) as source:
        source.extractall(runtime)
    (runtime / "python313._pth").write_text("python313.zip\n.\nLib/site-packages\nimport site\n")
    run(sys.executable, "-m", "pip", "install", "--disable-pip-version-check",
        "--only-binary=:all:", "--platform", "win_amd64", "--python-version", "3.13",
        "--implementation", "cp", "--abi", "cp313", "--no-compile",
        "--target", runtime / "Lib/site-packages", "-r", ROOT / "requirements-inference.txt")
    licenses = stage / "licenses"
    licenses.mkdir(exist_ok=True)
    shutil.copytree(ROOT / "licenses", licenses, dirs_exist_ok=True)
    # MSYS2 supplies exact license texts for the dependency distribution used in CI.
    if qmake:
        installed_licenses = Path(qmake).parent.parent / "share/licenses"
        if installed_licenses.is_dir():
            shutil.copytree(installed_licenses, licenses / "toolchain", dirs_exist_ok=True)
    if (ROOT / "dependencies-windows.txt").exists():
        shutil.copy2(ROOT / "dependencies-windows.txt", stage)

    # Validate relocation: run outside the checkout, without compiler/Qt/OpenCV PATH.
    env = os.environ.copy()
    system_root = os.environ["SystemRoot"]
    env["PATH"] = os.pathsep.join([str(stage), str(Path(system_root) / "System32"), system_root])
    env.pop("UDP_VISION_PYTHON", None)
    env["OMP_NUM_THREADS"] = "2"
    env["QT_QPA_PLATFORM"] = "offscreen"
    run(stage / "udp-vision.exe", "--demo", "--smoke-test", cwd=stage, env=env, timeout=60)
    # Real model inference, including consecutive binary requests, using ONLY bundled Python.
    raw = bytes([32, 180, 96]) * 400 * 400
    header = {"width": 400, "height": 400, "stride": 1200, "image_rgb24_bytes": len(raw)}
    result = subprocess.run([str(runtime / "python.exe"), "-I",
        str(stage / "src/backend/inference/onnx_helper.py"), str(stage / "models/best.onnx")],
        input=(json.dumps(header).encode() + b"\n" + raw) * 2,
        capture_output=True, check=True, timeout=60, cwd=stage, env=env)
    replies = [json.loads(line) for line in result.stdout.splitlines()]
    if len(replies) != 3 or not all(reply.get("ok") for reply in replies):
        raise RuntimeError(f"Packaged inference failed: {replies}")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))
    (stage / "BUILD-INFO.json").write_text(json.dumps({"commit": commit, "dirty": dirty, "python": "3.13.7",
        "inference": replies[0], "validation": "relocated desktop demo and two model requests passed"}, indent=2))
    files = sorted(path for path in stage.rglob("*") if path.is_file())
    (stage / "SHA256SUMS.txt").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(stage).as_posix()}\n" for path in files))
    archive_path = shutil.make_archive(str(stage), "zip", stage.parent, stage.name)
    Path(archive_path + ".sha256").write_text(
        hashlib.sha256(Path(archive_path).read_bytes()).hexdigest() + "  " + Path(archive_path).name + "\n")
    print(archive_path)


if __name__ == "__main__":
    main()
