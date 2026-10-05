# fmetrics

Fast image & video fidelity metrics in C & Zig.

Read the [wiki](https://github.com/halidecx/fmetrics/wiki) for comprehensive
documentation (library usage, speed testing, MOS correlation, etc).

## Usage

Compilation requires [Zig](https://ziglang.org/) ≥0.16.0. macOS, Linux, and
Windows are supported; Windows needs one extra step, described below. To
compile, run:

```sh
zig build --release=fast
```

You may add `-Dflto=true` for FLTO, and `-Dstrip=true` to strip the binary.

By default this builds the `fmetrics` binary and a static library. To also
build an installable shared library, pass `-Dshared=true`:

```sh
zig build --release=fast -Dshared=true
```

On Windows, `fcvvdp` needs a small portability fix before the build will
succeed. It is carried as a patch in [`patches/`](patches/):

```sh
zig build # populates zig-pkg/
git apply --directory="zig-pkg/fcvvdp-<version>" \
        patches/0001-windows-portability.patch
zig build --release=fast -Dshared=true
```

`--directory` is needed because Zig extracts dependencies without their `.git`
directory, so running `git apply` from inside `zig-pkg/fcvvdp-*/` reports
`Skipped patch`.

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
