# Third-party notices

The [MIT license](LICENSE) applies to project code. It does not replace licenses
of dependencies or establish rights to third-party datasets, models or images.

| Component | License/source |
| --- | --- |
| Qt 5 runtime | LGPL-3.0 / GPL options; [Qt source](https://download.qt.io/archive/qt/); license texts in `licenses/` |
| OpenCV 3.4.8 | BSD-3-Clause; [matching source](https://github.com/opencv/opencv/tree/3.4.8) |
| OpenCV 4.x | Apache-2.0 since 4.5; [source](https://github.com/opencv/opencv) |
| MinGW/GCC runtime | GPL with GCC Runtime Library Exception and component-specific licenses; `licenses/` |
| Python 3.13.7 | PSF license included in embedded runtime; [matching source](https://www.python.org/downloads/release/python-3137/) |
| ONNX Runtime | MIT; license included in installed wheel |
| NumPy, Pillow and Python transitive dependencies | Respective wheel license files retained under `runtime/python/Lib/site-packages` |

Qt/OpenCV are dynamically linked. The Windows distribution keeps their DLLs and
plugins replaceable; users may replace compatible builds and debug modifications.
Source/build instructions for this application are public in this repository.
MSYS2 CI packages include the toolchain's installed license files and exact package
version inventory. MSYS2 dependency build recipes and source references are available
from [MINGW-packages](https://github.com/msys2/MINGW-packages).

Some OpenCV builds include an FFmpeg plugin. Its applicable license and codec
availability depend on the supplied OpenCV build. The locally available legacy
3.4.8 build and current MSYS2 packages are distinct distributions; review their
upstream terms before redistributing modified binaries.

## Existing repository assets

`models/best.onnx` and `assets/demo_reference.png` were already present on
`Post-Train` at commit `59cfbd584cf6fda920ea42fdc519d1c89734bbe4`; this cleanup
preserves their bytes. Training data, model authorship and separate redistribution
terms are not established by that commit. Do not infer a separate asset license
or clinical performance claim from the project's MIT code license.

The two original GitHub-hosted README illustrations retain their original URLs.
`docs/images/console-demo.png` is a new screenshot of the application using the
existing reference image. It demonstrates software behavior on loopback traffic.
