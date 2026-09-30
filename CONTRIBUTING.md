# Contributing

[English](CONTRIBUTING.md) · [中文说明](README.zh-CN.md)

Base changes on `main`, which includes the latest `Post-Train` development. Follow the setup in
[BUILDING.md](docs/BUILDING.md), then run `ctest --test-dir build --output-on-failure`
and `python -m unittest discover -s tests -p 'test_*.py' -v`.

Keep Qt widgets in `src/frontend`, stream/inference workers in `src/backend`, and
application wiring in `src/app`. Put headers beside implementations. Preserve the
sender's wire format; document protocol changes before implementation. Comment
thread ownership, queue limits and buffer lifetimes where they affect correctness.

Add a regression check for changed parsing, concurrency or packaging behavior.
UI changes need a screenshot from the compiled application. Keep both READMEs in
sync. Do not commit generated builds, recordings, captures, virtual environments,
IDE machine settings or credentials.

Pull requests should describe the observable change, validation and remaining
hardware limitations. Contributions to project code use the [MIT license](LICENSE).
