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
const ssimu2_impl = @import("ssimulacra2/ssimulacra2.zig");

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
};

pub const Colorspace = enum {
    srgb,
};

pub const Image = struct {
    data: []const u8,
    width: u32,
    height: u32,
    stride: u32,
    format: PixelFormat = .rgb_uint8,
    colorspace: Colorspace = .srgb,

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

    fn asCImage(image: *const Image) c.FmetricsImg {
        return .{
            .data = image.data.ptr,
            .width = image.width,
            .height = image.height,
            .stride = image.stride,
            .format = c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = c.FMETRICS_COLORSPACE_SRGB,
        };
    }
};

pub const ButteraugliOptions = struct {
    intensity_target: f32 = 203.0,
    pnorm: i32 = 3,
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
    if (image.format != .rgb_uint8 or image.colorspace != .srgb)
        return error.UnsupportedFormat;

    const row_bytes = std.math.mul(usize, image.width, 3) catch
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

pub fn iwssim(reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_iwssim_cmp(&ref, &dist, &result));
    return result;
}

pub fn msssim(reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_msssim_cmp(&ref, &dist, &result));
    return result;
}

const PackedImage = struct {
    data: []const u8,
    owned: ?[]u8,
};

fn packedImage(allocator: std.mem.Allocator, image: Image) !PackedImage {
    const row_bytes = std.math.mul(usize, image.width, 3) catch
        return error.InvalidArgument;
    const len = std.math.mul(usize, row_bytes, image.height) catch
        return error.InvalidArgument;
    if (image.stride == row_bytes)
        return .{ .data = image.data[0..len], .owned = null };

    const data = allocator.alloc(u8, len) catch return error.OutOfMemory;
    errdefer allocator.free(data);
    for (0..image.height) |y| {
        const src_offset = y * image.stride;
        const dst_offset = y * row_bytes;
        @memcpy(data[dst_offset..][0..row_bytes], image.data[src_offset..][0..row_bytes]);
    }
    return .{ .data = data, .owned = data };
}

pub fn ssimu2(reference: Image, distorted: Image) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_ssimu2_cmp(&ref, &dist, &result));
    return result;
}

pub fn ssimu2WithAllocator(allocator: std.mem.Allocator, reference: Image, distorted: Image) !f64 {
    return ssimu2WithAllocatorAndMap(allocator, reference, distorted, null);
}

pub fn ssimu2WithAllocatorAndMap(allocator: std.mem.Allocator, reference: Image, distorted: Image, error_map: ?[]u32) !f64 {
    try validatePair(reference, distorted);
    if (error_map) |map| try validateMap(reference, map);

    const ref = packedImage(allocator, reference) catch |err| return err;
    defer if (ref.owned) |data| allocator.free(data);
    const dist = packedImage(allocator, distorted) catch |err| return err;
    defer if (dist.owned) |data| allocator.free(data);

    return ssimu2_impl.computeSsimu2(
        allocator,
        ref.data,
        dist.data,
        reference.width,
        reference.height,
        3,
        error_map,
    ) catch |err| switch (err) {
        error.InvalidChannelCount => error.InvalidArgument,
        error.OutOfMemory => error.OutOfMemory,
    };
}

pub fn butteraugli(reference: Image, distorted: Image, options: ButteraugliOptions) !f64 {
    try validatePair(reference, distorted);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var c_options = c.FmetricsButteraugliOptions{
        .intensity_target = options.intensity_target,
        .pnorm = options.pnorm,
    };
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_butteraugli_cmp(
        &ref,
        &dist,
        &c_options,
        &result,
    ));
    return result;
}

pub fn ssimu2Map(reference: Image, distorted: Image, error_map: []u32) !f64 {
    try validatePair(reference, distorted);
    try validateMap(reference, error_map);
    var ref = reference.asCImage();
    var dist = distorted.asCImage();
    var result: f64 = undefined;
    try errorFromC(c.fmetrics_ssimu2_cmp_map(
        &ref,
        &dist,
        &result,
        error_map.ptr,
    ));
    return result;
}

pub fn butteraugliMap(reference: Image, distorted: Image, options: ButteraugliOptions, error_map: []u32) !f64 {
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
