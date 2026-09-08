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
const c = @cImport({
    @cInclude("fmetrics.h");
});

pub const version = "0.0.2";

pub const Error = error{
    InvalidArgument,
    UnsupportedFormat,
    DimensionMismatch,
    OutOfMemory,
    IwssimImageTooSmall,
    Internal,
};

pub const PixelFormat = enum {
    rgb_uint8,
    rgb_float,
};

pub const Colorspace = enum {
    srgb,
    linear_srgb,
};

pub const Image = struct {
    data: []const u8,
    width: u32,
    height: u32,
    stride: u32,
    format: PixelFormat = .rgb_uint8,
    colorspace: Colorspace = .srgb,
    hdr: bool = false,

    pub fn init(data: []const u8, width: u32, height: u32) !Image {
        const stride = std.math.mul(u32, width, 3) catch
            return error.InvalidArgument;
        return initWithStride(data, width, height, stride);
    }

    pub fn initWithStride(data: []const u8, width: u32, height: u32, stride: u32) !Image {
        const row_bytes = std.math.mul(usize, width, 3) catch
            return error.InvalidArgument;
        if (stride < row_bytes) return error.InvalidArgument;
        const len = std.math.mul(usize, stride, height) catch
            return error.InvalidArgument;
        if (data.len < len or width == 0 or height == 0)
            return error.InvalidArgument;
        return .{
            .data = data,
            .width = width,
            .height = height,
            .stride = stride,
        };
    }

    pub fn initLinear(data: []const f32, width: u32, height: u32) !Image {
        const stride = std.math.mul(u32, width, 12) catch
            return error.InvalidArgument;
        const image: Image = .{
            .data = std.mem.sliceAsBytes(data),
            .width = width,
            .height = height,
            .stride = stride,
            .format = .rgb_float,
            .colorspace = .linear_srgb,
        };
        try validateImage(image);
        return image;
    }

    fn asCImage(image: *const Image) c.FmetricsImg {
        return .{
            .data = image.data.ptr,
            .hdr = image.hdr,
            .width = image.width,
            .height = image.height,
            .stride = image.stride,
            .format = if (image.format == .rgb_float) c.FMETRICS_PIX_FMT_RGB_FLOAT else c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = if (image.colorspace == .linear_srgb) c.FMETRICS_COLORSPACE_LINEAR_SRGB else c.FMETRICS_COLORSPACE_SRGB,
        };
    }
};

pub const ButteraugliOptions = struct {
    intensity_target: f32 = 203.0,
    pnorm: i32 = 3,
};

pub const Workspace = struct {
    handle: *c.FmetricsWorkspace,

    pub fn init() !Workspace {
        return .{ .handle = c.fmetrics_workspace_create() orelse return error.OutOfMemory };
    }

    pub fn deinit(self: *Workspace) void {
        c.fmetrics_workspace_destroy(self.handle);
        self.* = undefined;
    }
};

fn errorFromC(err: c.FmetricsErr) !void {
    if (err == c.FMETRICS_OK) return;
    return switch (err) {
        c.FMETRICS_ERR_INVALID_ARGUMENT => error.InvalidArgument,
        c.FMETRICS_ERR_UNSUPPORTED_FORMAT => error.UnsupportedFormat,
        c.FMETRICS_ERR_DIMENSION_MISMATCH => error.DimensionMismatch,
        c.FMETRICS_ERR_OUT_OF_MEMORY => error.OutOfMemory,
        c.FMETRICS_ERR_IWSSIM_IMG_TOO_SMALL => error.IwssimImageTooSmall,
        c.FMETRICS_ERR_INTERNAL => error.Internal,
        else => error.Internal,
    };
}

fn validateImage(image: Image) !void {
    if (image.width == 0 or image.height == 0)
        return error.InvalidArgument;
    if (!((image.format == .rgb_uint8 and image.colorspace == .srgb) or
        (image.format == .rgb_float and image.colorspace == .linear_srgb)))
        return error.UnsupportedFormat;

    const row_bytes = std.math.mul(usize, image.width, if (image.format == .rgb_float) 12 else 3) catch
        return error.InvalidArgument;
    if (image.stride < row_bytes) return error.InvalidArgument;
    const len = std.math.mul(usize, image.stride, image.height) catch
        return error.InvalidArgument;
    if (image.data.len < len) return error.InvalidArgument;
}

fn validatePair(reference: Image, distorted: Image) !void {
    try validateImage(reference);
    try validateImage(distorted);
    if (reference.width != distorted.width or
        reference.height != distorted.height)
        return error.DimensionMismatch;
}

fn validateMap(image: Image, map: []u32) !void {
    const pixels = std.math.mul(usize, image.width, image.height) catch
        return error.InvalidArgument;
    if (map.len < pixels) return error.InvalidArgument;
}

pub fn iwssim(workspace: *Workspace, reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_iwssim_cmp(
        workspace.handle,
        &ref,
        &dist,
        &result,
    ));
    return result;
}

pub fn msssim(workspace: *Workspace, reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_msssim_cmp(
        workspace.handle,
        &ref,
        &dist,
        &result,
    ));
    return result;
}

pub fn ssimu2(workspace: *Workspace, reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_ssimu2_cmp(
        workspace.handle,
        &ref,
        &dist,
        &result,
    ));
    return result;
}

pub fn butteraugli(workspace: *Workspace, reference: Image, distorted: Image, options: ButteraugliOptions) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var c_options = c.FmetricsButteraugliOptions{
        .intensity_target = options.intensity_target,
        .pnorm = options.pnorm,
    };
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_butteraugli_cmp(
        workspace.handle,
        &ref,
        &dist,
        &c_options,
        &result,
    ));
    return result;
}

pub fn ssimu2Map(workspace: *Workspace, reference: Image, distorted: Image, error_map: []u32) !f64 {
    try validatePair(reference, distorted);
    try validateMap(reference, error_map);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_ssimu2_cmp_map(
        workspace.handle,
        &ref,
        &dist,
        &result,
        error_map.ptr,
    ));
    return result;
}

pub fn butteraugliMap(workspace: *Workspace, reference: Image, distorted: Image, options: ButteraugliOptions, error_map: []u32) !f64 {
    try validatePair(reference, distorted);
    try validateMap(reference, error_map);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var c_options = c.FmetricsButteraugliOptions{
        .intensity_target = options.intensity_target,
        .pnorm = options.pnorm,
    };
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_butteraugli_cmp_map(
        workspace.handle,
        &ref,
        &dist,
        &c_options,
        &result,
        error_map.ptr,
    ));
    return result;
}

pub fn errorString(err: Error) []const u8 {
    return switch (err) {
        error.InvalidArgument => "invalid argument",
        error.UnsupportedFormat => "unsupported pixel format or colorspace",
        error.DimensionMismatch => "image dimensions do not match",
        error.OutOfMemory => "out of memory",
        error.IwssimImageTooSmall => "image too small for five-scale IW-SSIM",
        error.Internal => "internal error",
    };
}
