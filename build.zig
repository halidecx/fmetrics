// Copyright © 2026, Halide Compression, LLC.
// All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const strip = b.option(bool, "strip", "strip symbols from the binary, defaults to false") orelse false;
    const flto = b.option(bool, "flto", "enable Link Time Optimization, defaults to false") orelse false;
    const options = b.addOptions();

    // simpleimgio
    const simpleimgio_dep = b.dependency("simpleimgio", .{
        .target = target,
        .optimize = optimize,
    });
    const simpleimgio = simpleimgio_dep.module("simpleimgio");

    // libspng
    const spng_dep = b.dependency("spng", .{
        .target = target,
        .optimize = optimize,
    });
    const spng = spng_dep.artifact("spng");
    spng.lto = if (flto) .thin else null;

    // fcvvdp
    const fcvvdp_dep = b.dependency("fcvvdp", .{
        .target = target,
        .optimize = optimize,
        .strip = strip,
        .flto = flto,
    });
    const cvvdp = fcvvdp_dep.artifact("cvvdp");
    cvvdp.lto = if (flto) .full else null;

    // fmetrics
    const fmetrics_module = b.addModule("fmetrics", .{
        .root_source_file = b.path("src/fmetrics.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    fmetrics_module.addIncludePath(b.path("src"));

    const translate_c = b.addTranslateC(.{
        .root_source_file = b.path("c_imports.h"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    translate_c.addIncludePath(b.path("."));
    translate_c.addIncludePath(spng.getEmittedIncludeTree());
    translate_c.addIncludePath(cvvdp.getEmittedIncludeTree());
    const c_module = translate_c.createModule();

    // 'libfmetrics.a' static lib
    const lib = b.addLibrary(.{
        .name = "libfmetrics",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/zig_to_c.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .strip = strip,
        }),
    });
    const lib_sources = [_][]const u8{
        "src/fmetrics.c",
        "src/iwssim/iwssim.c",
        "src/msssim/msssim.c",
        "src/butteraugli/butteraugli.c",
    };
    const lib_flags = [_][]const u8{
        "-std=c23",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-O3",
        "-ffast-math",
    };
    lib.root_module.addCSourceFiles(.{
        .files = &lib_sources,
        .flags = &lib_flags,
    });
    lib.lto = if (flto) .full else null;
    lib.root_module.addIncludePath(b.path("."));
    lib.root_module.linkLibrary(spng);
    lib.root_module.linkLibrary(cvvdp);
    b.installArtifact(lib);

    // fmetrics.h
    lib.installHeader(b.path("src/fmetrics.h"), "fmetrics.h");

    // 'fmetrics' executable
    const bin = b.addExecutable(.{
        .name = "fmetrics",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .strip = strip,
        }),
        .use_llvm = true,
    });
    bin.root_module.addOptions("build_opts", options);
    bin.root_module.addImport("c", c_module);
    bin.root_module.addImport("fmetrics", fmetrics_module);
    bin.root_module.addImport("simpleimgio", simpleimgio);
    bin.root_module.addIncludePath(b.path("."));
    bin.root_module.linkLibrary(lib);
    bin.root_module.linkLibrary(spng);
    bin.root_module.linkLibrary(cvvdp);
    b.installArtifact(bin);
}
