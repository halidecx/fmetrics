# SSIMULACRA2 validation

The production implementation is C with compiler vectorization.

```sh
zig build --release=fast
```

Each active scale consumes linear RGB rows, sends 2x2 averages to the next
scale, converts only the required XYB channels, and retains nine filtered
rows. No full-frame linear RGB or XYB planes are allocated. Score-only scratch
space is bounded by `1024 * width + 4096` bytes, independent of height. Error
maps additionally retain error planes for the first three scales.

SSIM uses the blurred squared pixel difference to compute the variance of the
difference directly. This avoids subtracting nearly equal second moments;
identical inputs score exactly 100. Fourth-power norms use float row reductions
and double accumulation across rows. sRGB input is decoded through a float LUT
rather than quantized to half precision.

## Upstream comparison

`ssimulacra2.c` compares the installed library with the unmodified
[vapoursynth-zip 23.0.0 filter](https://github.com/dnjulek/vapoursynth-zip/blob/23.0.0/src/filters/ssimulacra2.zig).
Both consume the same deterministic linear RGB samples: interleaved for the
fmetrics API and planar for upstream. Inputs are shared read-only across
workers; each worker has its own reusable scratch allocation. The timed region
excludes input generation, allocation, and three warm-up frames. fmetrics input
validation remains included. This compares filter implementations directly,
without VapourSynth scheduling, decoding, or CLI I/O.

To reproduce on macOS, from the repository root:

```sh
zig build --release=fast
work=$(mktemp -d)
mkdir "$work/filters"
for name in ssimulacra2 simd srgb; do
    gh api "repos/dnjulek/vapoursynth-zip/contents/src/filters/$name.zig?ref=23.0.0" \
        -H 'Accept: application/vnd.github.raw+json' > "$work/filters/$name.zig"
done
gh api 'repos/dnjulek/vapoursynth-zip/contents/src/vcl.zig?ref=23.0.0' \
    -H 'Accept: application/vnd.github.raw+json' > "$work/vcl.zig"
cp tests/bench/vszip.zig "$work/bench.zig"
zig build-lib -dynamic -OReleaseFast -lc "$work/bench.zig" \
    -femit-bin="$work/libvszip.dylib"
cc -O3 -ffast-math -I . tests/bench/ssimulacra2.c \
    zig-out/lib/libfmetrics.a "$work/libvszip.dylib" -lpthread -lm \
    -o "$work/bench"
"$work/bench" 1920 1080 1 60
"$work/bench" 1920 1080 8 20
"$work/bench" 1920 1080 16 12
"$work/bench" 3840 2160 1 15
```

On Linux, change `.dylib` to `.so`. Output columns are implementation, width,
height, workers, frames/second, scratch bytes/worker, and score.

Measured on an 18-core Apple M5 Pro, native Zig 0.17 ReleaseFast, macOS arm64:

| Size | Workers | fmetrics fps | upstream fps | Speedup |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 1 | 47.07 | 40.37 | 1.17x |
| 1920x1080 | 8 | 322.86 | 273.37 | 1.18x |
| 1920x1080 | 16 | 555.80 | 463.74 | 1.20x |
| 3840x2160 | 1 | 11.75 | 9.97 | 1.18x |

Scratch at 1080p is 1,970,176 bytes versus 13,909,760 upstream; at 4K it is
3,936,256 bytes versus 52,655,360. These results do not establish x86 throughput.

The existing 3840x2160 PAM CLI pair takes 92.1 ms versus 173.1 ms before the
change (10 runs, two warm-ups); process peak memory falls from 714.3 MiB to
116.0 MiB. Scores are 70.5839 versus 70.5898.

## Subjective correlation

All 4,341 pairs in `../mos-correlation/assets/CID22_validation_set.csv` were
scored with the before/after CLIs against the same decoded PNGs. Correlations
use the CSV's `MCOS` values and SciPy's Kendall, Spearman, and Pearson functions,
matching `../mos-correlation/mos.py`. Every pair produced a score, including
negative scores. The baseline was rebuilt from `e8bf3cf` with only the build
and C-import adaptations required by Zig 0.17.

| Implementation | KRCC | SRCC | PCC |
| --- | ---: | ---: | ---: |
| Previous fmetrics | 0.708176 | 0.891111 | 0.873170 |
| Streaming C | 0.708025 | 0.890985 | 0.873013 |

Reproduce with SciPy installed, saving every prediction as well as correlations:

```sh
python3 tests/bench/correlation.py zig-out/bin/fmetrics \
    --output /tmp/fmetrics-cid22.json
```

Run the same command with the baseline executable to compare paired predictions.

Each coefficient changes by less than 0.0002. These measurements cover CID22;
correlation on other subjective datasets has not been measured.

## Video clip comparison

The executable script uses uv inline metadata to install its Python
dependencies, including vszip 23.0.0 and fmetrics built from this checkout:

```sh
./tests/bench/compare_video.py reference.mkv distorted.mkv \
    --threads 16 --runs 3 --csv scores.csv
```

Install uv, the VapourSynth system runtime, and a source plugin separately.
FFMS2 is the default decoder; its VapourSynth plugin must be installed.
`--source bestsource` uses `core.bs.VideoSource` instead. Plugins outside the
autoload path can be supplied with repeated `--load-plugin /path/to/plugin`
arguments. The script also finds the plugin in an installed `vapoursynth-vszip`
wheel when autoloading is unavailable. It prints the loaded plugin version and
path.

Both metrics receive identical float linear RGB frames. YUV is converted with
VapourSynth's Bicubic resizer, using frame color metadata and `--matrix 709` as
the matrix fallback. RGB samples are clamped to [0, 1] before linearization.
This comparison targets SDR video. `--range limited` or `--range full` overrides
the input range; the default uses source metadata. No frame resizing occurs.

The CSV records both scores and their signed delta for every frame. The summary
includes means, mean absolute delta, largest absolute delta, and median
throughput. Timing includes decoding, shared RGB preprocessing, and the Python
packing needed by the fmetrics bindings. It is a video-pipeline comparison;
use the C harness above for direct filter throughput. Metric output caches are
disabled, each pass warms up three frames, and pass order alternates between
runs. Decoder and operating-system caches can still affect timing.

To sample 100 frames starting at frame 50, every fifth frame:

```sh
./tests/bench/compare_video.py reference.mkv distorted.mkv \
    --start 50 --frames 100 --step 5 --csv sample.csv
```

`--frames` counts sampled frames. `--dist-start` selects a different starting
frame in the distorted clip when alignment requires it. Dimensions and frame
rates must match. Different lengths require an explicit common `--frames` span.
