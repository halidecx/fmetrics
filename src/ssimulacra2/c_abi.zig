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
const ssimu2 = @import("ssimulacra2.zig");
const c = @cImport({
    @cInclude("src/fmetrics.h");
});

fn validate(
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
) c.FmetricsErr {
    const ref = reference orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const dist = distorted orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    if (ref.data == null or dist.data == null)
        return c.FMETRICS_ERR_INVALID_ARGUMENT;
    if (ref.format != c.FMETRICS_PIX_FMT_RGB_UINT8 or
        dist.format != c.FMETRICS_PIX_FMT_RGB_UINT8 or
        ref.colorspace != c.FMETRICS_COLORSPACE_SRGB or
        dist.colorspace != c.FMETRICS_COLORSPACE_SRGB)
    {
        return c.FMETRICS_ERR_UNSUPPORTED_FORMAT;
    }
    if (ref.width != dist.width or ref.height != dist.height)
        return c.FMETRICS_ERR_DIMENSION_MISMATCH;
    if (ref.width == 0 or ref.height == 0)
        return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const row_bytes = @as(usize, ref.width) * 3;
    if (ref.stride < row_bytes or dist.stride < row_bytes)
        return c.FMETRICS_ERR_INVALID_ARGUMENT;
    return c.FMETRICS_OK;
}

fn packedImage(
    allocator: std.mem.Allocator,
    img: *const c.FmetricsImg,
    owned: *?[]u8,
) ![]const u8 {
    const row_bytes = @as(usize, img.width) * 3;
    const pixels = @as(usize, img.width) * @as(usize, img.height);
    const expected_len = pixels * 3;
    const src = img.data[0..(@as(usize, img.stride) * img.height)];
    if (img.stride == row_bytes) return src[0..expected_len];

    const buf = try allocator.alloc(u8, expected_len);
    owned.* = buf;
    var y: usize = 0;
    while (y < img.height) : (y += 1) {
        const src_off = y * @as(usize, img.stride);
        const dst_off = y * row_bytes;
        @memcpy(buf[dst_off..][0..row_bytes], src[src_off..][0..row_bytes]);
    }
    return buf;
}

fn toFmetricsErr(err: ssimu2.Ssimu2Error) c.FmetricsErr {
    return switch (err) {
        error.InvalidChannelCount => c.FMETRICS_ERR_INVALID_ARGUMENT,
        error.OutOfMemory => c.FMETRICS_ERR_OUT_OF_MEMORY,
    };
}

fn compute(
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
    error_map: ?[*]u32,
) c.FmetricsErr {
    const out = result orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const valid = validate(reference, distorted);
    if (valid != c.FMETRICS_OK) return valid;

    const ref = reference.?;
    const dist = distorted.?;
    const allocator = std.heap.c_allocator;
    const pixels = @as(usize, ref.width) * @as(usize, ref.height);

    var ref_owned: ?[]u8 = null;
    defer if (ref_owned) |buf| allocator.free(buf);
    var dist_owned: ?[]u8 = null;
    defer if (dist_owned) |buf| allocator.free(buf);

    const ref_slice = packedImage(allocator, ref, &ref_owned) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const dist_slice = packedImage(allocator, dist, &dist_owned) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const map_slice = if (error_map) |map| map[0..pixels] else null;

    const score = ssimu2.computeSsimu2(
        allocator,
        ref_slice,
        dist_slice,
        ref.width,
        ref.height,
        3,
        map_slice,
    ) catch |err| return toFmetricsErr(err);

    out.* = score;
    return c.FMETRICS_OK;
}

export fn fmetrics_ssimu2_cmp(
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
) callconv(.c) c.FmetricsErr {
    return compute(reference, distorted, result, null);
}

export fn fmetrics_ssimu2_cmp_map(
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
    error_map: ?[*]u32,
) callconv(.c) c.FmetricsErr {
    if (error_map == null) return c.FMETRICS_ERR_INVALID_ARGUMENT;
    return compute(reference, distorted, result, error_map);
}
