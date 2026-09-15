# fmetrics

Fast image & video fidelity metrics in C & Zig.

Read the [wiki](https://github.com/halidecx/fmetrics/wiki) for comprehensive
documentation (library usage, speed testing, MOS correlation, etc).

## Python

Install the Python bindings from PyPI:

```sh
python -m pip install "image-fmetrics[numpy]"
```

NumPy is optional; install `image-fmetrics` without the extra when using other
buffer-protocol objects.

The package accepts interleaved `HxWx3` buffer objects, including NumPy
arrays. `uint8` data is interpreted as sRGB and `float32` data as linear sRGB.
CVVDP additionally accepts `uint16` sRGB data.

```python
import fmetrics
import numpy as np

reference = np.zeros((512, 512, 3), dtype=np.uint8)
distorted = reference.copy()
score = fmetrics.ssimu2(reference, distorted)
```

Available functions are `iwssim`, `msssim`, `ssimu2`, `butteraugli`, and
`cvvdp`. CVVDP returns `(jod, quality)`. The `ssimu2_map` and
`butteraugli_map` functions return `(score, error_map)`, where `error_map` is a
two-dimensional `memoryview` of unsigned 32-bit values.

Use a workspace to reuse scratch memory across calls:

```python
workspace = fmetrics.Workspace()
score = workspace.ssimu2(reference, distorted)
```

Select a CVVDP display with `fmetrics.DisplayModel`, for example
`fmetrics.DisplayModel.MACBOOK_PRO_16`.

Python 3.11 or newer is required. Binary wheels are provided for Linux and
macOS on x86-64 and ARM64. Building from source requires the dependencies in
`pyproject.toml`; the isolated build environment supplies Zig 0.16.0.

## Publishing to PyPI

The distribution name is `image-fmetrics`. Configure pending trusted
publishers on PyPI and TestPyPI for `.github/workflows/release.yml`. Use the
GitHub environments `pypi` and `testpypi`, and require approval for `pypi`.

Run the release workflow manually to publish a candidate to TestPyPI. For a
production release, update the version in `build.zig.zon`, merge the change,
and push its matching tag:

```sh
git tag v0.0.3
git push origin v0.0.3
```

The workflow rejects tags that do not match the version and publishes through
short-lived OpenID Connect credentials. No PyPI API token is required.

## Usage

Compilation requires [Zig](https://ziglang.org/) ≥0.16.0 & a macOS, Linux, or
Unix-like operating system. To compile, run:

```sh
zig build --release=fast
```

You may add `-Dflto=true` for FLTO, and `-Dstrip=true` to strip the binary.

`fmetrics` binary usage:

```
fmetrics by Halide Compression, LLC | [version]

usage: fmetrics <metric> [options] <reference> <distorted>

compare two images/videos using various perceptual quality metrics

metrics:  iwssim, msssim, ssimu2, butter, cvvdp

run `fmetrics <metric> --help` for metric-specific help

options:
  -h, --help
      show this help message

sRGB PNG, PNM/PAM, QOI, or Y4M input expected
```

## Credits

fmetrics is under the [Apache 2.0 License](LICENSE). fmetrics is developed by
[Halide Compression](https://halide.cx).

Special thanks to [Vship](https://codeberg.org/Line-fr/Vship), which has
inspired parts of fmetrics. Vship is under the
[MIT NON-AI license](https://codeberg.org/Line-fr/Vship/src/branch/main/LICENSE).
