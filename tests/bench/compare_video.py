#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.14"
# dependencies = [
#     "image-fmetrics",
#     "numpy",
#     "vapoursynth>=77",
#     "vapoursynth-vszip==23.0.0",
# ]
# [tool.uv.sources]
# image-fmetrics = { path = "../.." }
# ///

import argparse
import csv
from importlib import metadata
import math
import os
import queue
from pathlib import Path
import statistics
import sys
import time


def arguments():
    parser = argparse.ArgumentParser(
        description="Compare fmetrics and vszip SSIMULACRA2 on SDR video.",
        epilog="Requires image-fmetrics, numpy, VapourSynth, vszip, and a source plugin.",
    )
    parser.add_argument("reference", type=Path)
    parser.add_argument("distorted", type=Path)
    parser.add_argument("--csv", type=Path, default=Path("ssimu2-comparison.csv"))
    parser.add_argument("--source", choices=["ffms2", "bestsource"], default="ffms2")
    parser.add_argument("--load-plugin", action="append", default=[], type=Path)
    parser.add_argument("--threads", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--dist-start", type=int)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--step", type=int, default=1)
    parser.add_argument("--matrix", default="709", help="YUV matrix fallback (default: 709)")
    parser.add_argument("--range", choices=["auto", "limited", "full"], default="auto")
    args = parser.parse_args()
    if min(args.threads, args.runs, args.step) < 1:
        parser.error("--threads, --runs, and --step must be positive")
    if args.start < 0 or (args.dist_start is not None and args.dist_start < 0):
        parser.error("frame offsets cannot be negative")
    if args.frames is not None and args.frames < 1:
        parser.error("--frames must be positive")
    for path in [args.reference, args.distorted, *args.load_plugin]:
        if not path.is_file():
            parser.error(f"file does not exist: {path}")
    if args.csv.resolve() in {args.reference.resolve(), args.distorted.resolve()}:
        parser.error("CSV output must differ from the input files")
    return args


def main():
    args = arguments()
    try:
        import fmetrics
        import numpy as np
        import vapoursynth as vs
    except ImportError as error:
        raise RuntimeError(
            f"{error}. Run this script with uv and install the VapourSynth "
            "runtime plus a source plugin."
        ) from error

    core = vs.core
    core.num_threads = args.threads
    for path in args.load_plugin:
        core.std.LoadPlugin(str(path.resolve()))
    if not hasattr(core, "vszip"):
        try:
            package = metadata.distribution("vapoursynth-vszip")
            for path in package.files or []:
                if path.name in {"libvszip.dylib", "libvszip.so", "vszip.dll"}:
                    core.std.LoadPlugin(str(package.locate_file(path)))
                    break
        except metadata.PackageNotFoundError:
            pass
    if not hasattr(core, "vszip"):
        raise RuntimeError("vszip is missing; install it or pass --load-plugin PATH")
    namespace = "ffms2" if args.source == "ffms2" else "bs"
    if not hasattr(core, namespace):
        raise RuntimeError(f"{namespace} is missing; install it or pass --load-plugin PATH")

    def source(path):
        if args.source == "ffms2":
            clip = core.ffms2.Source(str(path.resolve()), cache=False)
        else:
            clip = core.bs.VideoSource(str(path.resolve()))
        core.std.SetVideoCache(clip, mode=0)
        return clip

    ref, dist = source(args.reference), source(args.distorted)
    if not ref.format or not dist.format:
        raise ValueError("variable-format video is not supported")
    if (ref.width, ref.height) != (dist.width, dist.height):
        raise ValueError("clips must have the same dimensions")
    if ref.fps_num * dist.fps_den != dist.fps_num * ref.fps_den:
        raise ValueError("clips must have the same frame rate")
    dist_start = args.start if args.dist_start is None else args.dist_start
    lengths = [ref.num_frames - args.start, dist.num_frames - dist_start]
    if args.frames is None and lengths[0] != lengths[1]:
        raise ValueError("clip lengths differ; select a common span with --frames")
    span = min(lengths) if args.frames is None else (args.frames - 1) * args.step + 1
    if span < 1 or span > min(lengths):
        raise ValueError("requested frame selection exceeds the clips")
    ref = ref[args.start:args.start + span:args.step]
    dist = dist[dist_start:dist_start + span:args.step]

    def linear_rgb(clip):
        if args.range != "auto":
            clip = core.std.SetFrameProps(clip, _ColorRange=int(args.range == "limited"))
        options = {"matrix_in_s": args.matrix} if clip.format.color_family == vs.YUV else {}
        encoded = core.resize.Bicubic(clip, format=vs.RGBS, **options)
        rgb = core.std.Expr(encoded, expr="x 0 max 1 min")
        linear = core.resize.Bicubic(rgb, transfer_in_s="srgb", transfer_s="linear")
        for node in [clip, encoded, rgb, linear]:
            core.std.SetVideoCache(node, mode=0)
        return linear

    ref, dist = linear_rgb(ref), linear_rgb(dist)
    workspaces = queue.SimpleQueue()

    def compare(n, f):
        try:
            workspace, rgb = workspaces.get_nowait()
        except queue.Empty:
            workspace = fmetrics.Workspace()
            rgb = [np.empty((ref.height, ref.width, 3), np.float32) for _ in range(2)]
        try:
            for image, frame in zip(rgb, f):
                for channel in range(3):
                    image[:, :, channel] = np.asarray(frame[channel])
            score = workspace.ssimu2(*rgb)
            out = f[0].copy()
            out.props["FMETRICS_SSIMU2"] = score
            return out
        finally:
            workspaces.put((workspace, rgb))

    clips = {
        "fmetrics": (core.std.ModifyFrame(ref, clips=[ref, dist], selector=compare),
                     "FMETRICS_SSIMU2"),
        "vszip": (core.vszip.SSIMULACRA2(ref, dist), "SSIMULACRA2"),
    }
    for clip, _ in clips.values():
        core.std.SetVideoCache(clip, mode=0)
    print(f"fmetrics {fmetrics.__version__}; vszip {core.vszip.version}")
    print(f"vszip plugin: {core.vszip.plugin_path}")
    print(f"{ref.width}x{ref.height}, {ref.num_frames} frames, {args.threads} threads")
    print("Shared linear RGB; timing includes decoding, conversion, and Python packing.")
    elapsed = {name: [] for name in clips}
    scores = {}
    for run in range(args.runs):
        order = list(clips) if run % 2 == 0 else list(reversed(clips))
        for name in order:
            clip, prop = clips[name]
            for frame in clip[:min(3, ref.num_frames)].frames(close=True):
                pass
            start = time.perf_counter()
            values = [float(frame.props[prop]) for frame in
                      clip.frames(prefetch=args.threads, close=True)]
            duration = time.perf_counter() - start
            if not all(map(math.isfinite, values)):
                raise ValueError(f"{name} returned a non-finite score")
            if name in scores and values != scores[name]:
                raise RuntimeError(f"{name} scores changed between runs")
            scores[name] = values
            elapsed[name].append(duration)
            print(f"Run {run + 1}: {name:8} {duration:.3f} s, "
                  f"{len(values) / duration:.2f} fps, mean {statistics.mean(values):.6f}",
                  flush=True)

    deltas = [a - b for a, b in zip(scores["fmetrics"], scores["vszip"])]
    with args.csv.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["reference_frame", "distorted_frame", "fmetrics", "vszip", "delta"])
        for i, (a, b, delta) in enumerate(zip(scores["fmetrics"], scores["vszip"], deltas)):
            writer.writerow([args.start + i * args.step, dist_start + i * args.step, a, b, delta])
    print(f"Delta (fmetrics - vszip): mean {statistics.mean(deltas):+.6f}, "
          f"MAE {statistics.mean(map(abs, deltas)):.6f}, max abs {max(map(abs, deltas)):.6f}")
    for name in clips:
        duration = statistics.median(elapsed[name])
        print(f"{name:8} median: {duration:.3f} s, {ref.num_frames / duration:.2f} fps")
    ratio = statistics.median(elapsed["vszip"]) / statistics.median(elapsed["fmetrics"])
    print(f"Throughput ratio fmetrics/vszip: {ratio:.3f}x")
    print(f"Per-frame scores: {args.csv.resolve()}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError) as error:
        sys.exit(f"Error: {error}")
