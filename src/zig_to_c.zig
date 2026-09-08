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
const ssimu2 = @import("ssimulacra2/ssimulacra2.zig");
const c = @cImport({
    @cInclude("src/fmetrics.h");
});

extern fn fmetrics_workspace_reserve_data(
    workspace: ?*c.FmetricsWorkspace,
    size: usize,
) ?*anyopaque;

fn validate(
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
) c.FmetricsErr {
    const ref = reference orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const dist = distorted orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    if (ref.data == null or dist.data == null)
        return c.FMETRICS_ERR_INVALID_ARGUMENT;
    for ([_]*const c.FmetricsImg{ ref, dist }) |img| {
        if (!((img.format == c.FMETRICS_PIX_FMT_RGB_UINT8 and
            img.colorspace == c.FMETRICS_COLORSPACE_SRGB) or
            (img.format == c.FMETRICS_PIX_FMT_RGB_FLOAT and
                img.colorspace == c.FMETRICS_COLORSPACE_LINEAR_SRGB)))
            return c.FMETRICS_ERR_UNSUPPORTED_FORMAT;
        const row_bytes = @as(usize, img.width) *
            @as(usize, if (img.format == c.FMETRICS_PIX_FMT_RGB_FLOAT) 12 else 3);
        if (img.stride < row_bytes) return c.FMETRICS_ERR_INVALID_ARGUMENT;
    }
    if (ref.width != dist.width or ref.height != dist.height)
        return c.FMETRICS_ERR_DIMENSION_MISMATCH;
    if (ref.width == 0 or ref.height == 0)
        return c.FMETRICS_ERR_INVALID_ARGUMENT;
    return c.FMETRICS_OK;
}

fn packedImage(img: *const c.FmetricsImg, buffer: []u8) []const u8 {
    const row_bytes = @as(usize, img.width) * 3;
    const pixels = @as(usize, img.width) * @as(usize, img.height);
    const expected_len = pixels * 3;
    const data: [*]const u8 = @ptrCast(img.data.?);
    const src = data[0..(@as(usize, img.stride) * img.height)];
    if (img.stride == row_bytes) return src[0..expected_len];

    if (img.stride == row_bytes) return src[0..expected_len];
    var y: usize = 0;
    while (y < img.height) : (y += 1) {
        const src_off = y * @as(usize, img.stride);
        const dst_off = y * row_bytes;
        @memcpy(buffer[dst_off..][0..row_bytes], src[src_off..][0..row_bytes]);
    }
    return buffer[0..expected_len];
}

fn toFmetricsErr(err: ssimu2.Ssimu2Error) c.FmetricsErr {
    return switch (err) {
        error.InvalidChannelCount => c.FMETRICS_ERR_INVALID_ARGUMENT,
        error.OutOfMemory => c.FMETRICS_ERR_OUT_OF_MEMORY,
    };
}

fn compute(
    workspace: ?*c.FmetricsWorkspace,
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
    error_map: ?[*]u32,
) c.FmetricsErr {
    const out = result orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const ws = workspace orelse return c.FMETRICS_ERR_INVALID_ARGUMENT;
    const valid = validate(reference, distorted);
    if (valid != c.FMETRICS_OK) return valid;

    const ref = reference.?;
    const dist = distorted.?;
    const pixels = @as(usize, ref.width) * @as(usize, ref.height);
    const packed_len = std.math.mul(usize, pixels, 3) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const planes_len = std.math.mul(usize, pixels, 6) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const temp_len = std.math.mul(usize, pixels, 18) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const scratch_len = std.math.add(usize, ref.width, 64) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const planes_bytes = std.math.mul(usize, planes_len, @sizeOf(f16)) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const temp_off = std.mem.alignForward(usize, planes_bytes, @alignOf(f32));
    const temp_bytes = std.math.mul(usize, temp_len, @sizeOf(f32)) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const scratch_off = std.math.add(usize, temp_off, temp_bytes) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const scratch_bytes = std.math.mul(usize, scratch_len, @sizeOf(f32)) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const ref_off = std.math.add(usize, scratch_off, scratch_bytes) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const dist_off = std.math.add(usize, ref_off, packed_len) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const total = std.math.add(usize, dist_off, packed_len) catch
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const data = fmetrics_workspace_reserve_data(ws, total) orelse
        return c.FMETRICS_ERR_OUT_OF_MEMORY;
    const bytes: [*]u8 = @ptrCast(data);
    const planes = @as([*]f16, @ptrCast(@alignCast(bytes)))[0..planes_len];
    const temp = @as([*]f32, @ptrCast(@alignCast(bytes + temp_off)))[0..temp_len];
    const scratch = @as([*]f32, @ptrCast(@alignCast(bytes + scratch_off)))[0..scratch_len];
    if (ref.format == c.FMETRICS_PIX_FMT_RGB_FLOAT or
        dist.format == c.FMETRICS_PIX_FMT_RGB_FLOAT)
    {
        const float_planes = temp[0 .. pixels * 6];
        for ([_]*const c.FmetricsImg{ ref, dist }, 0..) |img, image_index| {
            const src: [*]const u8 = @ptrCast(img.data.?);
            for (0..img.height) |y| {
                for (0..img.width) |x| {
                    for (0..3) |ch| {
                        const value = if (img.format == c.FMETRICS_PIX_FMT_RGB_FLOAT) blk: {
                            const offset = y * img.stride + (x * 3 + ch) * 4;
                            const v: f32 = @bitCast(std.mem.readInt(u32, src[offset..][0..4], @import("builtin").cpu.arch.endian()));
                            if (!std.math.isFinite(v)) return c.FMETRICS_ERR_INVALID_ARGUMENT;
                            break :blk v;
                        } else blk: {
                            const v: f32 = @as(f32, @floatFromInt(src[y * img.stride + x * 3 + ch])) / 255.0;
                            break :blk if (v <= 0.04045) v / 12.92 else std.math.pow(f32, (v + 0.055) / 1.055, 2.4);
                        };
                        float_planes[(image_index * 3 + ch) * pixels + y * img.width + x] = value;
                    }
                }
            }
        }
        const rp: [3][]const f32 = .{ float_planes[0..pixels], float_planes[pixels .. pixels * 2], float_planes[pixels * 2 .. pixels * 3] };
        const dp: [3][]const f32 = .{ float_planes[pixels * 3 .. pixels * 4], float_planes[pixels * 4 .. pixels * 5], float_planes[pixels * 5 .. pixels * 6] };
        out.* = ssimu2.processWithScratch(rp, dp, ref.width, ref.width, ref.height, if (error_map) |map| map[0..pixels] else null, temp, scratch);
        return c.FMETRICS_OK;
    }
    const ref_slice = packedImage(ref, (bytes + ref_off)[0..packed_len]);
    const dist_slice = packedImage(dist, (bytes + dist_off)[0..packed_len]);
    const map_slice = if (error_map) |map| map[0..pixels] else null;

    const score = ssimu2.computeSsimu2WithBuffers(
        ref_slice,
        dist_slice,
        ref.width,
        ref.height,
        3,
        map_slice,
        planes,
        temp,
        scratch,
    ) catch |err| return toFmetricsErr(err);

    out.* = score;
    return c.FMETRICS_OK;
}

export fn fmetrics_ssimu2_cmp(
    workspace: ?*c.FmetricsWorkspace,
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
) callconv(.c) c.FmetricsErr {
    return compute(workspace, reference, distorted, result, null);
}

export fn fmetrics_ssimu2_cmp_map(
    workspace: ?*c.FmetricsWorkspace,
    reference: ?*const c.FmetricsImg,
    distorted: ?*const c.FmetricsImg,
    result: ?*f64,
    error_map: ?[*]u32,
) callconv(.c) c.FmetricsErr {
    if (error_map == null) return c.FMETRICS_ERR_INVALID_ARGUMENT;
    return compute(workspace, reference, distorted, result, error_map);
}
