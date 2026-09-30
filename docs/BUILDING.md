# Build and run / 构建与运行

Use `Post-Train`. The only supported build entry point is root `CMakeLists.txt`;
files under `legacy/` are historical references.

## Dependencies

| Component | Requirement | Purpose |
| --- | --- | --- |
| CMake | 3.21+ | Configure, build, CTest, install |
| C++ compiler | C++14, x86/x64 | SSE2 frame processing; x64 recommended |
| Qt | 5.14+ within Qt 5; Core, Gui, Widgets, Network, Test | Desktop and tests |
| OpenCV | 3.4.8+; core, dnn, imgproc, imgcodecs, videoio | Processing, inference and video |
| OpenMP | Optional, compiler-provided | Row processing parallelism |
| Python | 3.13 in CI and portable runtime | Optional ONNX Runtime fallback |

`requirements-inference.txt` pins direct and transitive Python runtime dependencies.
Qt/OpenCV come from the native toolchain; CI records exact installed package versions.
MSYS2 is rolling, so the CI environment is reproducible from commands plus version
receipts, not a frozen binary toolchain. Qt 6, ARM and macOS are not validated targets.

## Windows: MSYS2 (same route as CI)

Install [MSYS2](https://www.msys2.org/), open its **MINGW64** shell, update with
`pacman -Syu` (restart the shell if requested), then:

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-qt5-base mingw-w64-x86_64-opencv
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/udp-vision.exe --demo
```

For existing Qt/MinGW installations, use the matching MinGW compiler and add its
`bin`, Qt `bin`, OpenCV `bin` and CMake `bin` to the current shell's PATH. Then:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64 `
  -DOpenCV_DIR=C:/opencv/x64/mingw/lib
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
.\build\udp-vision.exe --demo
```

Replace example paths with actual installations. Do not mix MSVC OpenCV with MinGW Qt.
Local validation also covers Qt 5.14.2 / GCC 7.3 / OpenCV 3.4.8.

## Linux: Ubuntu 22.04

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build qtbase5-dev libopencv-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/udp-vision --demo
```

CTest uses Qt's offscreen platform. Running the application normally requires a
desktop display. CI Linux ZIPs are install trees; keep system Qt/OpenCV installed.
They are not universal Linux binaries or AppImages.

## Optional AI runtime

```bash
python -m venv .venv
# Windows: .venv\Scripts\activate; Linux: source .venv/bin/activate
python -m pip install -r requirements-inference.txt
python -m pip check
python -m unittest discover -s tests -p 'test_*.py' -v
```

The app first tries OpenCV DNN. If model import fails, it resolves Python in this
order: `UDP_VISION_PYTHON`, bundled `runtime/python/python.exe`, local `.venv`,
then the historical `.local/py310-yolo` environment. The helper script and model
are resolved relative to the executable and checkout. Set `UDP_VISION_PYTHON` to
an absolute interpreter path when using another environment.

The bundled runtime is CPU-only. Existing DirectML/CUDA/OpenVINO environments may
be selected through `UDP_VISION_PYTHON`; those providers are not installed or
validated by this CI. OpenCV 3.4.8 is known to require the Python fallback for the
included model. Enable detection in the **AI** page after startup.

| Environment variable | Meaning |
| --- | --- |
| `UDP_VISION_PYTHON` | Explicit helper interpreter path |
| `OMP_NUM_THREADS` | OpenMP thread limit; CI uses 2 |
| `POST_TRAIN_DEMO=1` | Same as `--demo` |
| `POST_TRAIN_ORT_PROVIDER=cpu` | Request CPU provider for the helper |
| `POST_TRAIN_ORT_INTRA_THREADS` | ONNX Runtime intra-op thread count |
| `POST_TRAIN_ORT_WARMUP=0` | Disable helper warmup |
| `POST_TRAIN_CPP_PREPROCESS=0` | Resize in Python instead of C++ |
| `TSHARK_PATH` | Optional explicit tshark executable; otherwise use PATH |

`--capture-bootstrap` enables tshark; capture files go to the application's local
data directory under `captures/`. Npcap and FTDI are optional Windows drivers and
are never installed by the application or CI.

## Windows portable packaging

Build Release first. In a Windows shell with `cmake`, `qmake`,
`objdump`, compiler/OpenCV DLL directories on PATH and a native Python with pip:

```powershell
python scripts/package_windows.py --build-dir build
```

The script installs to `dist/udp-vision-windows-x64`, deploys Qt, collects recursive
DLL dependencies, adds Python 3.13.7 (SHA-256 verified) and pinned inference wheels,
then starts the demo and real model from the installed directory with a clean PATH.
It creates `BUILD-INFO.json`, a per-file hash manifest, ZIP and archive checksum.
Existing output directories are refused; use `--name another-build` for a repeat.
Build/install archives and logs are ignored by Git.

Python itself and dependency licenses remain inside the package. See
[third-party notices](../THIRD_PARTY_NOTICES.md) for source and licensing information.

## Verification commands

```bash
ctest --test-dir build --output-on-failure --output-junit test-results.xml
python -m unittest discover -s tests -p 'test_*.py' -v
python -m pip check
```

For a current Windows screenshot, run `build/udp-vision.exe --demo --smoke-test
--screenshot docs/images/console-demo.png` with the normal Windows Qt platform.
The app exits after five seconds and fails if it has not presented multiple frames.
Some older Qt offscreen builds cannot render system fonts; use the normal platform
for README screenshots. The smoke test is a startup check, not a sustained load test.

中文提示：安装与编译命令如上。Windows 便携包应完整解压；测试使用本地回环，不代表 FPGA
硬件验收。遇到 DLL 缺失时优先核对 Qt/OpenCV/编译器架构，不要从不明网站单独下载 DLL。
