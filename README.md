# fmetrics

Fast image & video fidelity metrics in C & Zig.

## Usage

Compilation requires [Zig](https://ziglang.org/) ≥0.16.0 & a macOS, Linux, or
Unix-like operating system. To compile, run:

```sh
zig build --release=fast
```

You may add `-Dflto=true` for FLTO, and `-Dstrip=true` to strip the binary.

Compilation emits:

```tree
zig-out
├── bin
│   ├── fmetrics
├── include
│   └── fmetrics.h
└── lib
    └── libfmetrics.a
```

`fmetrics` binary usage:

```
fmetrics by Halide Compression, LLC | 0.0.1

usage: fmetrics <metric> [options] <reference> <distorted>

compare two images/videos using various perceptual quality metrics

metrics:  iwssim, msssim, ssimu2, butter, cvvdp

run `fmetrics <metric> --help` for metric-specific help

options:
  -h, --help
      show this help message

sRGB PNG, PNM/PAM, QOI, or Y4M input expected
```

Usage is different per-metric; some metrics support outputting visual error maps
via `--err-map`, and some support additional configuration options. I/O is the
same for all metrics, and is provided by
[simpleimgio](https://github.com/gianni-rosato/simpleimgio).

## Library Usage

`libfmetrics.a` exposes a C API declared in `fmetrics.h`. To use it as a Zig
dependency, add it to your `build.zig.zon` by running:

```sh
zig fetch --save git+https://github.com/halidecx/fmetrics.git
```

This should show something like this in `build.zig.zon`:

```zig
.dependencies = .{
    .fmetrics = .{
        .url = "git+https://github.com/halidecx/fmetrics.git#<commit>",
        .hash = "fmetrics-0.0.1-<hash>",
    },
},
```

Then you can link it from your `build.zig`:

```zig
const fmetrics_dep = b.dependency("fmetrics", .{
    .target = target,
    .optimize = optimize,
});
const fmetrics = fmetrics_dep.artifact("fmetrics");
exe.root_module.linkLibrary(fmetrics);
exe.root_module.addIncludePath(fmetrics.getEmittedIncludeTree());
```

Now you can use the API from your Zig or C code. See [`fmetrics.h`](src/fmetrics.h) for the full
API.

## Reference Comparison

Reference metric implementations tested include:

- Butteraugli: [libjxl](https://github.com/libjxl/libjxl)'s `butteraugli_main`
- CVVDP: Our [fcvvdp](https://github.com/halidecx/fcvvdp)
- IW-SSIM: A [fork](https://github.com/gianni-rosato/Python-IW-SSIM) of
  [Python IW-SSIM](https://github.com/Jack-guo-xy/Python-IW-SSIM)
- MS-SSIM: [libvmaf](https://github.com/netflix/vmaf)'s MS-SSIM filter via
  `ffmpeg`.
- SSIMULACRA2:
  [Cloudinary's `ssimulacra2`](https://github.com/cloudinary/ssimulacra2)

### MOS Correlation

MOS correlation is how closely a metric correlates with subjective human
ratings.

Tested using `mos.py` via
[mos-correlation](https://github.com/gianni-rosato/mos-correlation), on CID22.
For our purposes, these tests don't determine which metrics we think are better
than others, but rather how effective our implementations are relative to their
references. Here, we just report the Spearman Rank Correlation Coefficient
(SRCC), where higher is better.

| metric                 | srcc (reference) | srcc (fmetrics) | difference (%) |
| ---------------------- | ---------------- | --------------- | -------------- |
| butteraugli (p3 i203)* | 0.7929           | 0.7863          | -0.83%         |
| fcvvdp**               | 0.8274           | 0.8286          | +0.15%         |
| iw_ssim                | n/a              | 0.7925          | +0.00%         |
| ms_ssim                | 0.7845           | 0.8044          | +2.54%         |
| ssimulacra2            | 0.8916           | 0.8910          | -0.07%         |

> \*Note: Because Butteraugli is a smaller-is-better metric, the signs are
> flipped for the SRCCs reported above.

> \*\*Note: fmetrics uses the fcvvdp library (as a Zig module) with different
> I/O, so the underlying metric implementation is the same.

### Speed & Memory Usage

Testing was done on a stock Core i7-13700k with 3840x2160 source & distorted PAM
images
([Drive link](https://drive.google.com/drive/folders/1Kxzmw-jMWtbh8elPuQidY5MM1pTXkF0L?usp=sharing),
lossless JPEG-XL sources; run `djxl <*.pam.jxl> <*.pam>` to decompress).

#### Speed (ms)

| metric                | ms (reference) | ms (fmetrics) | difference (%) |
| --------------------- | -------------- | ------------- | -------------- |
| butteraugli (p3 i203) | 4110           | 3130          | 31.5% faster   |
| fcvvdp*               | 1390           | 1390          | 0.00%          |
| iw_ssim               | 3020           | 228           | 1228.5% faster |
| ms_ssim**             | 1110           | 114           | 866.7% faster  |
| ssimulacra2           | 722            | 232           | 211.4% faster  |

#### RAM Usage (MB)

| metric                | MB (reference) | MB (fmetrics) | difference (%) |
| --------------------- | -------------- | ------------- | -------------- |
| butteraugli (p3 i203) | 2440           | 1670          | -31.6%         |
| fcvvdp*               | 1600           | 1600          | 0.00%          |
| iw_ssim               | 2660           | 551           | -79.2%         |
| ms_ssim**             | 841            | 376           | -55.3%         |
| ssimulacra2           | 1370           | 741           | -45.8%         |

> \*Note: fmetrics uses the fcvvdp library (as a Zig module) with different I/O,
> so the underlying metric implementation is the same.

> \*\*Note: MS-SSIM comparison isn't fair, as libvmaf has to compute other
> metrics in the filterchain alongside MS-SSIM.

## Credits

fmetrics is under the [Apache 2.0 License](LICENSE). fmetrics is developed by
[Halide Compression](https://halide.cx).

Special thanks to [Vship](https://codeberg.org/Line-fr/Vship), which has
inspired parts of fmetrics. Vship is under the
[MIT NON-AI license](https://codeberg.org/Line-fr/Vship/src/branch/main/LICENSE).
