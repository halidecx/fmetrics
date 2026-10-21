# fmetrics

Fast image & video fidelity metrics in C & Zig.

Read the [wiki](https://github.com/halidecx/fmetrics/wiki) for comprehensive
documentation (library usage, speed testing, MOS correlation, etc).

## Usage

Compilation requires [Zig](https://ziglang.org/) ≥0.17.0 & a macOS, Linux, or
Unix-like operating system. To compile, run:

```sh
zig build --release=fast
```

You may add `-Dflto=true` for FLTO on ELF targets, and `-Dstrip=true` to strip
the binary. Zig 0.17 does not support this LTO build on macOS.

Run `zig build test --release=fast` for SSIMULACRA2 reference comparisons.
See [the benchmark notes](tests/bench/README.md) for performance and CID22
correlation results.

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
