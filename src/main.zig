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
const c = @import("c");
const imgio = @import("simpleimgio");

const print = std.debug.print;

const VideoStats = struct {
    frames: usize,
    avg: f64,
    stddev: f64,
    median: f64,
    p5: f64,
    p95: f64,
    min: f64,
    max: f64,
};

const Metric = enum {
    iwssim,
    msssim,
    ssimu2,
    butteraugli,
    cvvdp,
};

fn parseMetric(name: []const u8) ?Metric {
    if (std.ascii.eqlIgnoreCase(name, "iwssim") or std.ascii.eqlIgnoreCase(name, "iw-ssim"))
        return .iwssim;
    if (std.ascii.eqlIgnoreCase(name, "msssim") or std.ascii.eqlIgnoreCase(name, "ms-ssim"))
        return .msssim;
    if (std.ascii.eqlIgnoreCase(name, "ssimu2") or std.ascii.eqlIgnoreCase(name, "ssimulacra2"))
        return .ssimu2;
    if (std.ascii.eqlIgnoreCase(name, "butteraugli") or
        std.ascii.eqlIgnoreCase(name, "butter"))
    {
        return .butteraugli;
    }
    if (std.ascii.eqlIgnoreCase(name, "cvvdp") or std.ascii.eqlIgnoreCase(name, "jod"))
        return .cvvdp;
    return null;
}

fn parseDisplayModel(name: []const u8) ?c.FcvvdpDisplayModel {
    const models = [_]struct { []const u8, c.FcvvdpDisplayModel }{
        .{ "fhd", c.CVVDP_DISPLAY_STANDARD_FHD },
        .{ "standard_fhd", c.CVVDP_DISPLAY_STANDARD_FHD },
        .{ "4k", c.CVVDP_DISPLAY_STANDARD_4K },
        .{ "standard_4k", c.CVVDP_DISPLAY_STANDARD_4K },
        .{ "hdr_pq", c.CVVDP_DISPLAY_STANDARD_HDR_PQ },
        .{ "standard_hdr_pq", c.CVVDP_DISPLAY_STANDARD_HDR_PQ },
        .{ "hdr_hlg", c.CVVDP_DISPLAY_STANDARD_HDR_HLG },
        .{ "standard_hdr_hlg", c.CVVDP_DISPLAY_STANDARD_HDR_HLG },
        .{ "hdr_linear", c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR },
        .{ "standard_hdr_linear", c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR },
        .{ "hdr_dark", c.CVVDP_DISPLAY_STANDARD_HDR_DARK },
        .{ "standard_hdr_linear_dark", c.CVVDP_DISPLAY_STANDARD_HDR_DARK },
        .{ "hdr_zoom", c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR_ZOOM },
        .{ "standard_hdr_linear_zoom", c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR_ZOOM },
        .{ "standard_hmd", c.CVVDP_DISPLAY_STANDARD_HMD },
        .{ "standard_phone", c.CVVDP_DISPLAY_STANDARD_PHONE },
        .{ "sdr_4k_30", c.CVVDP_DISPLAY_SDR_4K_30 },
        .{ "sdr_fhd_24", c.CVVDP_DISPLAY_SDR_FHD_24 },
        .{ "htc_vive_pro", c.CVVDP_DISPLAY_HTC_VIVE_PRO },
        .{ "iphone_12_pro", c.CVVDP_DISPLAY_IPHONE_12_PRO },
        .{ "iphone_14_pro", c.CVVDP_DISPLAY_IPHONE_14_PRO },
        .{ "iphone_14_pro_vert", c.CVVDP_DISPLAY_IPHONE_14_PRO_VERT },
        .{ "iphone_14_pro_hdr", c.CVVDP_DISPLAY_IPHONE_14_PRO_HDR },
        .{ "iphone_14_pro_hdr_vert", c.CVVDP_DISPLAY_IPHONE_14_PRO_HDR_VERT },
        .{ "ipad_pro_12_9", c.CVVDP_DISPLAY_IPAD_PRO_12_9 },
        .{ "macbook_pro_16", c.CVVDP_DISPLAY_MACBOOK_PRO_16 },
        .{ "lg_oled_2017_sdr", c.CVVDP_DISPLAY_LG_OLED_2017_SDR },
        .{ "lg_oled_2017_hdr", c.CVVDP_DISPLAY_LG_OLED_2017_HDR },
        .{ "eizo_cg3146", c.CVVDP_DISPLAY_EIZO_CG3146 },
        .{ "65inch_hdr_pq_4knit", c.CVVDP_DISPLAY_65INCH_HDR_PQ_4KNIT },
        .{ "65inch_hdr_pq_2knit", c.CVVDP_DISPLAY_65INCH_HDR_PQ_2KNIT },
        .{ "65inch_hdr_pq_1knit", c.CVVDP_DISPLAY_65INCH_HDR_PQ_1KNIT },
        .{ "lg_oled_2026_hdr_pq", c.CVVDP_DISPLAY_LG_OLED_2026_HDR_PQ },
        .{ "mcos", c.CVVDP_DISPLAY_CID22_MCOS },
        .{ "cid22_mcos", c.CVVDP_DISPLAY_CID22_MCOS },
    };

    inline for (models) |model| {
        if (std.ascii.eqlIgnoreCase(name, model[0])) return model[1];
    }
    return null;
}

fn displayModelName(model: c.FcvvdpDisplayModel) []const u8 {
    return switch (model) {
        c.CVVDP_DISPLAY_STANDARD_FHD => "standard_fhd",
        c.CVVDP_DISPLAY_STANDARD_4K => "standard_4k",
        c.CVVDP_DISPLAY_STANDARD_HDR_PQ => "standard_hdr_pq",
        c.CVVDP_DISPLAY_STANDARD_HDR_HLG => "standard_hdr_hlg",
        c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR => "standard_hdr_linear",
        c.CVVDP_DISPLAY_STANDARD_HDR_DARK => "standard_hdr_dark",
        c.CVVDP_DISPLAY_STANDARD_HDR_LINEAR_ZOOM => "standard_hdr_linear_zoom",
        c.CVVDP_DISPLAY_STANDARD_HMD => "standard_hmd",
        c.CVVDP_DISPLAY_STANDARD_PHONE => "standard_phone",
        c.CVVDP_DISPLAY_SDR_4K_30 => "sdr_4k_30",
        c.CVVDP_DISPLAY_SDR_FHD_24 => "sdr_fhd_24",
        c.CVVDP_DISPLAY_HTC_VIVE_PRO => "htc_vive_pro",
        c.CVVDP_DISPLAY_IPHONE_12_PRO => "iphone_12_pro",
        c.CVVDP_DISPLAY_IPHONE_14_PRO => "iphone_14_pro",
        c.CVVDP_DISPLAY_IPHONE_14_PRO_VERT => "iphone_14_pro_vert",
        c.CVVDP_DISPLAY_IPHONE_14_PRO_HDR => "iphone_14_pro_hdr",
        c.CVVDP_DISPLAY_IPHONE_14_PRO_HDR_VERT => "iphone_14_pro_hdr_vert",
        c.CVVDP_DISPLAY_IPAD_PRO_12_9 => "ipad_pro_12_9",
        c.CVVDP_DISPLAY_MACBOOK_PRO_16 => "macbook_pro_16",
        c.CVVDP_DISPLAY_LG_OLED_2017_SDR => "lg_oled_2017_sdr",
        c.CVVDP_DISPLAY_LG_OLED_2017_HDR => "lg_oled_2017_hdr",
        c.CVVDP_DISPLAY_EIZO_CG3146 => "eizo_cg3146",
        c.CVVDP_DISPLAY_65INCH_HDR_PQ_4KNIT => "65inch_hdr_pq_4knit",
        c.CVVDP_DISPLAY_65INCH_HDR_PQ_2KNIT => "65inch_hdr_pq_2knit",
        c.CVVDP_DISPLAY_65INCH_HDR_PQ_1KNIT => "65inch_hdr_pq_1knit",
        c.CVVDP_DISPLAY_LG_OLED_2026_HDR_PQ => "lg_oled_2026_hdr_pq",
        c.CVVDP_DISPLAY_CID22_MCOS => "cid22_mcos",
        else => "unknown",
    };
}

fn metricName(metric: Metric) []const u8 {
    return switch (metric) {
        .iwssim => "iwssim",
        .msssim => "msssim",
        .ssimu2 => "ssimu2",
        .butteraugli => "butteraugli",
        .cvvdp => "cvvdp",
    };
}

fn percentile(sorted: []const f64, percent: f64) f64 {
    if (sorted.len == 1) return sorted[0];

    const max_index: f64 = @floatFromInt(sorted.len - 1);
    const pos = (percent / 100.0) * max_index;
    const lower_float = @floor(pos);
    const lower: usize = @intFromFloat(lower_float);
    const upper: usize = @min(lower + 1, sorted.len - 1);
    const fraction = pos - lower_float;

    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

fn computeVideoStats(allocator: std.mem.Allocator, scores: []const f64) !VideoStats {
    if (scores.len == 0) return error.EmptyY4M;

    var sum: f64 = 0.0;
    for (scores) |score| sum += score;

    const avg = sum / @as(f64, @floatFromInt(scores.len));

    var variance_sum: f64 = 0.0;
    for (scores) |score| {
        const delta = score - avg;
        variance_sum += delta * delta;
    }

    const sorted = try allocator.dupe(f64, scores);
    defer allocator.free(sorted);
    std.mem.sort(f64, sorted, {}, std.sort.asc(f64));

    return .{
        .frames = scores.len,
        .avg = avg,
        .stddev = @sqrt(variance_sum / @as(f64, @floatFromInt(scores.len))),
        .median = percentile(sorted, 50.0),
        .p5 = percentile(sorted, 5.0),
        .p95 = percentile(sorted, 95.0),
        .min = sorted[0],
        .max = sorted[sorted.len - 1],
    };
}

fn workerCount(requested: c_uint) usize {
    if (requested > 0) return @max(1, @as(usize, @intCast(requested)));
    return @max(1, std.Thread.getCpuCount() catch 1);
}

const QueueSlot = struct {
    ref_frame: ?imgio.YuvFrame = null,
    dis_frame: ?imgio.YuvFrame = null,
    is_end: bool = false,
};

const VideoQueue = struct {
    allocator: std.mem.Allocator,
    io: std.Io,
    mutex: std.Io.Mutex = .init,
    not_empty: std.Io.Condition = .init,
    not_full: std.Io.Condition = .init,
    buf: []QueueSlot,
    head: usize = 0,
    tail: usize = 0,
    count: usize = 0,
    seen_end: bool = false,
    stopped: bool = false,

    fn init(allocator: std.mem.Allocator, io: std.Io, capacity: usize) !VideoQueue {
        const buf = try allocator.alloc(QueueSlot, capacity);
        @memset(buf, .{});
        return .{
            .allocator = allocator,
            .io = io,
            .buf = buf,
        };
    }

    fn deinit(self: *VideoQueue) void {
        for (self.buf) |*slot| {
            if (slot.ref_frame) |*frame| frame.deinit(self.allocator);
            if (slot.dis_frame) |*frame| frame.deinit(self.allocator);
        }
        self.allocator.free(self.buf);
        self.* = undefined;
    }

    fn discardSlot(self: *VideoQueue, slot: QueueSlot) void {
        if (slot.ref_frame) |frame| {
            var mutable_frame = frame;
            mutable_frame.deinit(self.allocator);
        }
        if (slot.dis_frame) |frame| {
            var mutable_frame = frame;
            mutable_frame.deinit(self.allocator);
        }
    }

    fn close(self: *VideoQueue) void {
        self.mutex.lockUncancelable(self.io);
        defer self.mutex.unlock(self.io);
        self.stopped = true;
        self.not_empty.broadcast(self.io);
        self.not_full.broadcast(self.io);
    }

    fn push(self: *VideoQueue, slot: QueueSlot) bool {
        self.mutex.lockUncancelable(self.io);
        defer self.mutex.unlock(self.io);

        while (self.count == self.buf.len) {
            if (self.stopped) {
                self.discardSlot(slot);
                return false;
            }
            self.not_full.waitUncancelable(self.io, &self.mutex);
        }

        if (self.stopped) {
            self.discardSlot(slot);
            return false;
        }

        self.buf[self.tail] = slot;
        self.tail = (self.tail + 1) % self.buf.len;
        self.count += 1;
        self.not_empty.signal(self.io);
        return true;
    }

    fn pop(self: *VideoQueue) ?QueueSlot {
        self.mutex.lockUncancelable(self.io);
        defer self.mutex.unlock(self.io);

        while (self.count == 0) {
            if (self.stopped) return null;
            if (self.seen_end) return null;
            self.not_empty.waitUncancelable(self.io, &self.mutex);
        }

        if (self.stopped) return null;

        const slot = self.buf[self.head];
        self.buf[self.head] = .{};
        self.head = (self.head + 1) % self.buf.len;
        self.count -= 1;

        if (slot.is_end) {
            self.seen_end = true;
            self.not_empty.broadcast(self.io);
        }

        self.not_full.signal(self.io);
        return slot;
    }
};

const ScoreSink = struct {
    io: std.Io,
    mutex: std.Io.Mutex = .init,
    scores: std.ArrayList(f64) = .empty,

    fn deinit(self: *ScoreSink, allocator: std.mem.Allocator) void {
        self.scores.deinit(allocator);
    }

    fn append(self: *ScoreSink, allocator: std.mem.Allocator, score: f64) !void {
        self.mutex.lockUncancelable(self.io);
        defer self.mutex.unlock(self.io);
        try self.scores.append(allocator, score);
    }
};

const VideoWorker = struct {
    allocator: std.mem.Allocator,
    queue: ?*VideoQueue,
    scores: *ScoreSink,
    metric: Metric,
    width: usize,
    height: usize,
    ref_rgb: []u8,
    dis_rgb: []u8,
    butteraugli_options: c.FmetricsButteraugliOptions,
    err: ?anyerror = null,

    fn init(
        allocator: std.mem.Allocator,
        queue: ?*VideoQueue,
        scores: *ScoreSink,
        metric: Metric,
        width: usize,
        height: usize,
        butteraugli_options: c.FmetricsButteraugliOptions,
    ) !VideoWorker {
        const pixels = try std.math.mul(usize, width, height);
        return .{
            .allocator = allocator,
            .queue = queue,
            .scores = scores,
            .metric = metric,
            .width = width,
            .height = height,
            .ref_rgb = try allocator.alloc(u8, pixels * 3),
            .dis_rgb = try allocator.alloc(u8, pixels * 3),
            .butteraugli_options = butteraugli_options,
        };
    }

    fn deinit(self: *VideoWorker) void {
        self.allocator.free(self.ref_rgb);
        self.allocator.free(self.dis_rgb);
    }

    fn processFrames(self: *VideoWorker, ref_frame: imgio.YuvFrame, dis_frame: imgio.YuvFrame) !void {
        try yuv420ToRgb8Into(self.allocator, self.ref_rgb, ref_frame);
        try yuv420ToRgb8Into(self.allocator, self.dis_rgb, dis_frame);

        var ref = c.FmetricsImg{
            .data = self.ref_rgb.ptr,
            .width = @intCast(self.width),
            .height = @intCast(self.height),
            .stride = @intCast(self.width * 3),
            .format = c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = c.FMETRICS_COLORSPACE_SRGB,
        };

        var dis = c.FmetricsImg{
            .data = self.dis_rgb.ptr,
            .width = @intCast(self.width),
            .height = @intCast(self.height),
            .stride = @intCast(self.width * 3),
            .format = c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = c.FMETRICS_COLORSPACE_SRGB,
        };

        var result: f64 = undefined;
        const err = switch (self.metric) {
            .iwssim => c.fmetrics_iwssim_cmp(&ref, &dis, &result),
            .msssim => c.fmetrics_msssim_cmp(&ref, &dis, &result),
            .ssimu2 => c.fmetrics_ssimu2_cmp(&ref, &dis, &result),
            .butteraugli => c.fmetrics_butteraugli_cmp(&ref, &dis, &self.butteraugli_options, &result),
            .cvvdp => unreachable,
        };
        if (err != c.FMETRICS_OK) {
            print("Error: {s} frame processing failed: {s}\n", .{ metricName(self.metric), c.fmetrics_error_str(err) });
            return error.MetricError;
        }

        try self.scores.append(self.allocator, result);
    }

    fn processSlot(self: *VideoWorker, slot: QueueSlot) !void {
        if (slot.is_end) return;

        var ref_frame = slot.ref_frame.?;
        defer ref_frame.deinit(self.allocator);
        var dis_frame = slot.dis_frame.?;
        defer dis_frame.deinit(self.allocator);

        try self.processFrames(ref_frame, dis_frame);
    }

    fn worker(self: *VideoWorker) void {
        while (true) {
            const slot = self.queue.?.pop() orelse break;
            self.processSlot(slot) catch |err| {
                self.err = err;
                self.queue.?.close();
                break;
            };
        }
    }
};

pub fn loadPNG(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !imgio.Image {
    const file = try std.Io.Dir.cwd().openFile(io, path, .{});
    defer file.close(io);
    const size = try file.length(io);
    const buf = try allocator.alloc(u8, size);
    defer allocator.free(buf);
    _ = try file.readPositionalAll(io, buf, 0);

    const ctx = c.spng_ctx_new(0);
    if (ctx == null) return error.FailedCreateContext;
    defer c.spng_ctx_free(ctx);

    if (c.spng_set_png_buffer(ctx, buf.ptr, buf.len) != 0)
        return error.SetBufferFailed;

    var ihdr: c.struct_spng_ihdr = undefined;
    if (c.spng_get_ihdr(ctx, &ihdr) != 0)
        return error.GetHeaderFailed;

    // always decode to RGBA8
    const fmt: c_int = c.SPNG_FMT_RGBA8;
    var out_size: usize = 0;
    if (c.spng_decoded_image_size(ctx, fmt, &out_size) != 0) return error.ImageSizeFailed;

    const out_buf = try allocator.alloc(u8, out_size);
    errdefer allocator.free(out_buf);

    if (c.spng_decode_image(ctx, out_buf.ptr, out_size, fmt, 0) != 0) return error.DecodeFailed;

    return .{
        .width = ihdr.width,
        .height = ihdr.height,
        .depth = 4,
        .maxval = 255,
        .kind = .rgba,
        .data = out_buf,
    };
}

fn hasExtension(path: []const u8, ext: []const u8) bool {
    if (path.len < ext.len) return false;
    const tail = path[path.len - ext.len ..];
    return std.ascii.eqlIgnoreCase(tail, ext);
}

fn yuv420ToRgb8Into(allocator: std.mem.Allocator, rgb: []u8, frame: imgio.YuvFrame) !void {
    if (frame.chroma != .yuv420) return error.UnsupportedY4MChroma;

    var eight = try frame.to8Bit(allocator);
    defer eight.deinit(allocator);

    const width = eight.width;
    const height = eight.height;
    if (rgb.len < width * height * 3) return error.BadImageData;

    // Convert to RGB8 using a simple full-range BT.601-like YUV->RGB.
    // This is intended for metric input, not broadcast-accurate color management.
    const cw = (width + 1) / 2;

    const clampU8 = struct {
        fn f(x: i32) u8 {
            if (x < 0) return 0;
            if (x > 255) return 255;
            return @intCast(x);
        }
    }.f;

    for (0..height) |yy| {
        for (0..width) |xx| {
            const yv: i32 = eight.y[yy * width + xx];
            const uv: i32 = eight.u[(yy / 2) * cw + (xx / 2)];
            const vv: i32 = eight.v[(yy / 2) * cw + (xx / 2)];

            const u_off = uv - 128;
            const v_off = vv - 128;

            const r = yv + ((359 * v_off) >> 8);
            const g = yv - ((88 * u_off + 183 * v_off) >> 8);
            const b = yv + ((454 * u_off) >> 8);

            const i = (yy * width + xx) * 3;
            rgb[i + 0] = clampU8(r);
            rgb[i + 1] = clampU8(g);
            rgb[i + 2] = clampU8(b);
        }
    }
}

pub fn toRGB8(allocator: std.mem.Allocator, img: imgio.Image) ![]u8 {
    const pixels = img.width * img.height;
    const rgb = try allocator.alloc(u8, pixels * 3);

    switch (img.kind) {
        .rgb => {
            if (img.data.len != pixels * 3) return error.BadImageData;
            @memcpy(rgb, img.data);
        },
        .rgba => {
            if (img.data.len != pixels * 4) return error.BadImageData;
            for (0..pixels) |i| {
                rgb[i * 3 + 0] = img.data[i * 4 + 0];
                rgb[i * 3 + 1] = img.data[i * 4 + 1];
                rgb[i * 3 + 2] = img.data[i * 4 + 2];
            }
        },
        .bitmap, .grayscale => {
            if (img.data.len != pixels) return error.BadImageData;
            for (0..pixels) |i| {
                const g = img.data[i];
                rgb[i * 3 + 0] = g;
                rgb[i * 3 + 1] = g;
                rgb[i * 3 + 2] = g;
            }
        },
        .grayscale_alpha => {
            if (img.data.len != pixels * 2) return error.BadImageData;
            for (0..pixels) |i| {
                const g = img.data[i * 2];
                rgb[i * 3 + 0] = g;
                rgb[i * 3 + 1] = g;
                rgb[i * 3 + 2] = g;
            }
        },
        .pam => return error.UnsupportedPamDepth,
    }
    return rgb;
}

fn cvvdpImageFromRgb(rgb: []const u8, width: usize, height: usize) c.FcvvdpImage {
    return .{
        .data = rgb.ptr,
        .width = @intCast(width),
        .height = @intCast(height),
        .stride = @intCast(width * 3),
        .format = c.CVVDP_PIXEL_FORMAT_RGB_UINT8,
        .colorspace = c.CVVDP_COLORSPACE_SRGB,
    };
}

fn printUsage(metric: ?Metric) void {
    print("\n", .{});
    const common_opts_str =
        \\  -v, --verbose
        \\      show verbose output
        \\  -j, --json
        \\      output result as JSON
        \\  -h, --help
        \\      show this help message
    ;
    const frame_thread_str =
        \\  -t, --threads u8
        \\      frame thread count; default 0 (auto)
        \\
    ;

    if (metric) |m| {
        print("usage: fmetrics {s} [options] <reference> <distorted>\n\n", .{metricName(m)});
        print("compare two images/videos using the {s} perceptual quality metric\n\n", .{metricName(m)});
        print("options:\n", .{});
        switch (m) {
            .ssimu2 => {
                print(
                    \\  -e, --err-map <out.pam|out.tga>
                    \\      save SSIMULACRA2 error map for image inputs
                    \\
                , .{});
                print(frame_thread_str, .{});
            },
            .iwssim, .msssim => {
                print(frame_thread_str, .{});
            },
            .butteraugli => {
                print(
                    \\  -i, --intensity-target f32
                    \\      viewing-condition screen nits; default 203
                    \\  -p, --pnorm i32
                    \\      p-norm used to pool the distance map; default 2
                    \\  -e, --err-map <out.pam|out.tga>
                    \\      save Butteraugli distance map for image inputs
                    \\
                , .{});
                print(frame_thread_str, .{});
            },
            .cvvdp => {
                print(
                    \\  -m, --model <name>
                    \\      CVVDP display model; default fhd
                    \\  -t, --threads u8
                    \\      task thread count; default 0 (auto)
                    \\
                , .{});
            },
        }
        print(common_opts_str, .{});
        print("\n\n\x1b[37msRGB PNG, PNM/PAM, QOI, or Y4M input expected\x1b[0m\n", .{});
        return;
    }

    print(
        \\usage: fmetrics <metric> [options] <reference> <distorted>
        \\
        \\compare two images/videos using various perceptual quality metrics
        \\
        \\metrics:  iwssim, msssim, ssimu2, butter, cvvdp
        \\
        \\run `fmetrics <metric> --help` for metric-specific help
        \\
        \\options:
        \\  -h, --help
        \\      show this help message
    , .{});
    print("\n\n\x1b[37msRGB PNG, PNM/PAM, QOI, or Y4M input expected\x1b[0m\n", .{});
}

fn loadImage(allocator: std.mem.Allocator, io: std.Io, path: []const u8) !imgio.Image {
    if (hasExtension(path, ".png"))
        return loadPNG(allocator, io, path);

    var decoded = if (hasExtension(path, ".qoi"))
        try imgio.decodeQoiFile(io, allocator, path)
    else if (hasExtension(path, ".pbm") or
        hasExtension(path, ".pgm") or
        hasExtension(path, ".ppm") or
        hasExtension(path, ".pnm") or
        hasExtension(path, ".pam"))
        try imgio.decodePnmFile(io, allocator, path)
    else
        return error.UnsupportedFileFormat;
    defer decoded.deinit(allocator);

    return decoded.to8BitOwned(allocator);
}

fn writeErrorMapPAM(allocator: std.mem.Allocator, io: std.Io, path: []const u8, data: []const u32, width: usize, height: usize) !void {
    const pixels = try std.math.mul(usize, width, height);
    if (data.len < pixels) return error.InvalidErrorMap;

    var file = try std.Io.Dir.cwd().createFile(io, path, .{});
    defer file.close(io);

    var header_buf: [256]u8 = undefined;
    const header = try std.fmt.bufPrint(
        &header_buf,
        "P7\nWIDTH {d}\nHEIGHT {d}\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n",
        .{ width, height },
    );
    try file.writeStreamingAll(io, header);

    const bytes = try allocator.alloc(u8, pixels * 4);
    defer allocator.free(bytes);
    for (data[0..pixels], 0..) |px, i| {
        bytes[i * 4 + 0] = @truncate(px);
        bytes[i * 4 + 1] = @truncate(px >> 8);
        bytes[i * 4 + 2] = @truncate(px >> 16);
        bytes[i * 4 + 3] = @truncate(px >> 24);
    }
    try file.writeStreamingAll(io, bytes);
}

fn writeErrorMapTGA(allocator: std.mem.Allocator, io: std.Io, path: []const u8, data: []const u32, width: usize, height: usize) !void {
    const pixels = try std.math.mul(usize, width, height);
    if (data.len < pixels) return error.InvalidErrorMap;
    if (width > std.math.maxInt(u16) or height > std.math.maxInt(u16)) return error.InvalidErrorMap;

    var file = try std.Io.Dir.cwd().createFile(io, path, .{});
    defer file.close(io);

    var header = [_]u8{0} ** 18;
    header[2] = 2;
    header[12] = @truncate(width);
    header[13] = @truncate(width >> 8);
    header[14] = @truncate(height);
    header[15] = @truncate(height >> 8);
    header[16] = 32;
    header[17] = 0x28;
    try file.writeStreamingAll(io, &header);

    const bytes = try allocator.alloc(u8, pixels * 4);
    defer allocator.free(bytes);
    for (data[0..pixels], 0..) |px, i| {
        bytes[i * 4 + 0] = @truncate(px >> 16);
        bytes[i * 4 + 1] = @truncate(px >> 8);
        bytes[i * 4 + 2] = @truncate(px);
        bytes[i * 4 + 3] = @truncate(px >> 24);
    }
    try file.writeStreamingAll(io, bytes);
}

fn writeErrorMap(allocator: std.mem.Allocator, io: std.Io, path: []const u8, data: []const u32, width: usize, height: usize) !void {
    if (hasExtension(path, ".tga"))
        return writeErrorMapTGA(allocator, io, path, data, width, height);
    return writeErrorMapPAM(allocator, io, path, data, width, height);
}

pub fn main(init: std.process.Init) !void {
    const allocator = init.gpa;
    const io = init.io;
    print("\x1b[38;5;117mfmetrics\x1b[0m by Halide Compression, LLC | {s}\n", .{c.fmetrics_version_str()});

    var args = std.process.Args.iterate(init.minimal.args);
    _ = args.next();

    const metric_arg = args.next() orelse {
        printUsage(null);
        return error.MissingMetric;
    };
    if (std.mem.eql(u8, metric_arg, "-h") or std.mem.eql(u8, metric_arg, "--help")) {
        printUsage(null);
        return;
    }

    const metric = parseMetric(metric_arg) orelse {
        print("Error: Unknown metric '{s}'\n", .{metric_arg});
        printUsage(null);
        return error.InvalidArguments;
    };

    var ref_filename: ?[]const u8 = null;
    var dis_filename: ?[]const u8 = null;
    var display_model: c.FcvvdpDisplayModel = c.CVVDP_DISPLAY_STANDARD_FHD;
    var threads: c_uint = 0;
    var verbose = false;
    var json_output = false;
    var error_map_path: ?[]const u8 = null;
    var butteraugli_options = c.FmetricsButteraugliOptions{
        .intensity_target = 203.0,
        .pnorm = 2,
    };

    while (args.next()) |arg| {
        if (std.mem.eql(u8, arg, "-h") or std.mem.eql(u8, arg, "--help")) {
            printUsage(metric);
            return;
        } else if (std.mem.eql(u8, arg, "-v") or std.mem.eql(u8, arg, "--verbose"))
            verbose = true
        else if (std.mem.eql(u8, arg, "-j") or std.mem.eql(u8, arg, "--json"))
            json_output = true
        else if (std.mem.eql(u8, arg, "--err-map") or
            std.mem.eql(u8, arg, "-e"))
        {
            if (metric != .ssimu2 and metric != .butteraugli) {
                print("Error: --err-map is only valid for ssimu2 or butteraugli\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
            if (args.next()) |map_arg| {
                error_map_path = map_arg;
            } else {
                print("Error: Missing argument for -e / --err-map\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
        } else if (std.mem.eql(u8, arg, "--intensity-target") or
            std.mem.eql(u8, arg, "-i"))
        {
            if (metric != .butteraugli) {
                print("Error: --intensity-target is only valid for butteraugli\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
            if (args.next()) |intensity_arg| {
                const intensity = try std.fmt.parseFloat(f32, intensity_arg);
                if (intensity <= 0.0 or std.math.isNan(intensity) or
                    std.math.isInf(intensity))
                {
                    print("Error: --intensity-target must be finite and positive\n", .{});
                    return error.InvalidArguments;
                }
                butteraugli_options.intensity_target = intensity;
            } else {
                print("Error: Missing argument for --intensity-target\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
        } else if (std.mem.eql(u8, arg, "--pnorm") or
            std.mem.eql(u8, arg, "-p"))
        {
            if (metric != .butteraugli) {
                print("Error: --pnorm is only valid for butteraugli\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
            if (args.next()) |pnorm_arg| {
                const pnorm = try std.fmt.parseInt(c_int, pnorm_arg, 10);
                if (pnorm < 1) {
                    print("Error: --pnorm must be at least 1\n", .{});
                    return error.InvalidArguments;
                }
                butteraugli_options.pnorm = pnorm;
            } else {
                print("Error: Missing argument for --pnorm\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
        } else if (std.mem.eql(u8, arg, "-m") or std.mem.eql(u8, arg, "--model")) {
            if (metric != .cvvdp) {
                print("Error: --model is only valid for cvvdp\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
            if (args.next()) |model_arg| {
                display_model = parseDisplayModel(model_arg) orelse {
                    print("Error: Unknown CVVDP display model '{s}'\n", .{model_arg});
                    printUsage(metric);
                    return error.InvalidArguments;
                };
            } else {
                print("Error: Missing argument for --model\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
        } else if ((std.mem.eql(u8, arg, "-t") or std.mem.eql(u8, arg, "--threads"))) {
            if (args.next()) |thread_arg|
                threads = try std.fmt.parseInt(c_uint, thread_arg, 10)
            else {
                print("Error: Missing argument for --threads\n", .{});
                printUsage(metric);
                return error.InvalidArguments;
            }
        } else if (arg[0] != '-') {
            if (ref_filename == null)
                ref_filename = arg
            else if (dis_filename == null)
                dis_filename = arg
            else {
                print("Error: Too many input files specified\n", .{});
                return error.TooManyFiles;
            }
        } else {
            print("Error: Unknown option '{s}'\n", .{arg});
            printUsage(metric);
            return error.UnknownOption;
        }
    }

    if (ref_filename == null or dis_filename == null) {
        print("Error: Two input image files are required\n", .{});
        printUsage(metric);
        return error.MissingFiles;
    }

    const ref_is_y4m = hasExtension(ref_filename.?, ".y4m");
    const dis_is_y4m = hasExtension(dis_filename.?, ".y4m");

    if (ref_is_y4m != dis_is_y4m) {
        print("Error: Both inputs must be Y4M if one is\n", .{});
        return error.MismatchedInputTypes;
    }
    if (ref_is_y4m and error_map_path != null) {
        print("Error: -e / --err-map is only supported for image inputs\n", .{});
        return error.InvalidArguments;
    }

    if (!ref_is_y4m) {
        var ref_img = try loadImage(allocator, io, ref_filename.?);
        defer ref_img.deinit(allocator);

        var dis_img = try loadImage(allocator, io, dis_filename.?);
        defer dis_img.deinit(allocator);

        if (ref_img.width != dis_img.width or ref_img.height != dis_img.height) {
            print("Error: Image dimensions do not match\n", .{});
            print("  Reference: {d}x{d}\n", .{ ref_img.width, ref_img.height });
            print("  Distorted: {d}x{d}\n", .{ dis_img.width, dis_img.height });
            return error.DimensionMismatch;
        }

        const ref_rgb = try toRGB8(allocator, ref_img);
        defer allocator.free(ref_rgb);

        const dis_rgb = try toRGB8(allocator, dis_img);
        defer allocator.free(dis_rgb);

        if (metric == .cvvdp) {
            var ref = cvvdpImageFromRgb(ref_rgb, ref_img.width, ref_img.height);
            var dis = cvvdpImageFromRgb(dis_rgb, dis_img.width, dis_img.height);
            var result: c.FcvvdpResult = undefined;
            const err = c.cvvdp_compare_images(&ref, &dis, display_model, 1, null, &result);

            if (err != c.CVVDP_OK) {
                print("Error: CVVDP comparison failed: {s}\n", .{c.cvvdp_error_string(err)});
                return error.CVVDPError;
            }

            if (json_output) {
                print(
                    \\{{
                    \\  "jod": {d:.6},
                    \\  "quality": {d:.6},
                    \\  "display_model": "{s}",
                    \\  "reference": "{s}",
                    \\  "distorted": "{s}",
                    \\  "width": {d},
                    \\  "height": {d}
                    \\}}
                , .{ result.jod, result.quality, displayModelName(display_model), ref_filename.?, dis_filename.?, ref.width, ref.height });
            } else {
                print("JOD: {d:.4}\n", .{result.jod});
                if (verbose) {
                    print("quality: {d:.6}\n", .{result.quality});
                    print("model:   {s}\n", .{displayModelName(display_model)});
                    print("width:   {d}\n", .{ref.width});
                    print("height:  {d}\n", .{ref.height});
                }
            }
            return;
        }

        var ref = c.FmetricsImg{
            .data = ref_rgb.ptr,
            .width = @intCast(ref_img.width),
            .height = @intCast(ref_img.height),
            .stride = @intCast(ref_img.width * 3),
            .format = c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = c.FMETRICS_COLORSPACE_SRGB,
        };

        var dis = c.FmetricsImg{
            .data = dis_rgb.ptr,
            .width = @intCast(dis_img.width),
            .height = @intCast(dis_img.height),
            .stride = @intCast(dis_img.width * 3),
            .format = c.FMETRICS_PIX_FMT_RGB_UINT8,
            .colorspace = c.FMETRICS_COLORSPACE_SRGB,
        };

        var result: f64 = undefined;
        var error_map: ?[]u32 = null;
        defer if (error_map) |map| allocator.free(map);
        if (error_map_path != null) {
            const pixels = try std.math.mul(usize, ref_img.width, ref_img.height);
            error_map = try allocator.alloc(u32, pixels);
        }
        const err = switch (metric) {
            .iwssim => c.fmetrics_iwssim_cmp(&ref, &dis, &result),
            .msssim => c.fmetrics_msssim_cmp(&ref, &dis, &result),
            .ssimu2 => if (error_map) |map|
                c.fmetrics_ssimu2_cmp_map(&ref, &dis, &result, map.ptr)
            else
                c.fmetrics_ssimu2_cmp(&ref, &dis, &result),
            .butteraugli => if (error_map) |map|
                c.fmetrics_butteraugli_cmp_map(&ref, &dis, &butteraugli_options, &result, map.ptr)
            else
                c.fmetrics_butteraugli_cmp(&ref, &dis, &butteraugli_options, &result),
            .cvvdp => unreachable,
        };

        if (err != c.FMETRICS_OK) {
            print("Error: {s} comparison failed: {s}\n", .{ metricName(metric), c.fmetrics_error_str(err) });
            return error.MetricError;
        }

        if (error_map_path) |path| {
            if (error_map) |map| {
                try writeErrorMap(allocator, io, path, map, ref_img.width, ref_img.height);
                if (!json_output and verbose)
                    print("error_map: {s}\n", .{path});
            }
        }

        if (json_output) {
            print(
                \\{{
                \\  "{s}": {d:.6},
                \\  "reference": "{s}",
                \\  "distorted": "{s}",
                \\  "width": {d},
                \\  "height": {d}
                \\}}
            , .{ metricName(metric), result, ref_filename.?, dis_filename.?, ref.width, ref.height });
        } else {
            print("{s}: {d:.6}\n", .{ metricName(metric), result });
            if (verbose) {
                print("width:   {d}\n", .{ref.width});
                print("height:  {d}\n", .{ref.height});
                if (metric == .butteraugli) {
                    print("nits:    {d:.4}\n", .{butteraugli_options.intensity_target});
                    print("pnorm:   {d}\n", .{butteraugli_options.pnorm});
                }
            }
        }
        return;
    }

    var ref_dec = try imgio.y4m.decodeFile(io, allocator, ref_filename.?);
    defer ref_dec.deinit();

    var dis_dec = try imgio.y4m.decodeFile(io, allocator, dis_filename.?);
    defer dis_dec.deinit();

    if (ref_dec.header.width != dis_dec.header.width or ref_dec.header.height != dis_dec.header.height) {
        print("Error: Video dimensions do not match\n", .{});
        print("  Reference: {d}x{d}\n", .{ ref_dec.header.width, ref_dec.header.height });
        print("  Distorted: {d}x{d}\n", .{ dis_dec.header.width, dis_dec.header.height });
        return error.DimensionMismatch;
    }

    if (metric == .cvvdp) {
        const fps: f32 = if (ref_dec.header.fps_num != 0)
            @as(f32, @floatFromInt(ref_dec.header.fps_num)) / @as(f32, @floatFromInt(ref_dec.header.fps_den))
        else
            0.0;

        var ctx_ptr: ?*c.FcvvdpCtx = null;
        const create_err = c.cvvdp_create(
            @intCast(ref_dec.header.width),
            @intCast(ref_dec.header.height),
            fps,
            display_model,
            threads,
            null,
            &ctx_ptr,
        );
        if (create_err != c.CVVDP_OK or ctx_ptr == null) {
            print("Error: CVVDP context creation failed: {s}\n", .{c.cvvdp_error_string(create_err)});
            return error.CVVDPError;
        }
        defer c.cvvdp_destroy(ctx_ptr.?);

        const pixels = try std.math.mul(usize, ref_dec.header.width, ref_dec.header.height);
        const ref_rgb = try allocator.alloc(u8, pixels * 3);
        defer allocator.free(ref_rgb);
        const dis_rgb = try allocator.alloc(u8, pixels * 3);
        defer allocator.free(dis_rgb);

        var frame_index: usize = 0;
        var result: c.FcvvdpResult = undefined;

        while (true) {
            const ref_frame_opt = try ref_dec.readFrame();
            const dis_frame_opt = try dis_dec.readFrame();

            if (ref_frame_opt == null and dis_frame_opt == null) break;
            if (ref_frame_opt == null or dis_frame_opt == null) {
                if (ref_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                if (dis_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                print("Error: Video frame count does not match\n", .{});
                return error.FrameCountMismatch;
            }

            {
                var ref_frame = ref_frame_opt.?;
                defer ref_frame.deinit(allocator);
                var dis_frame = dis_frame_opt.?;
                defer dis_frame.deinit(allocator);

                try yuv420ToRgb8Into(allocator, ref_rgb, ref_frame);
                try yuv420ToRgb8Into(allocator, dis_rgb, dis_frame);

                var ref = cvvdpImageFromRgb(ref_rgb, ref_frame.width, ref_frame.height);
                var dis = cvvdpImageFromRgb(dis_rgb, dis_frame.width, dis_frame.height);
                const proc_err = c.cvvdp_process_frame(ctx_ptr.?, &ref, &dis, &result);
                if (proc_err != c.CVVDP_OK) {
                    print("Error: CVVDP frame processing failed at frame {d}: {s}\n", .{ frame_index, c.cvvdp_error_string(proc_err) });
                    return error.CVVDPError;
                }
            }

            frame_index += 1;
        }

        if (frame_index == 0) return error.EmptyY4M;

        if (json_output) {
            print(
                \\{{
                \\  "jod": {d:.6},
                \\  "quality": {d:.6},
                \\  "display_model": "{s}",
                \\  "reference": "{s}",
                \\  "distorted": "{s}",
                \\  "width": {d},
                \\  "height": {d},
                \\  "frames": {d}
                \\}}
            , .{ result.jod, result.quality, displayModelName(display_model), ref_filename.?, dis_filename.?, ref_dec.header.width, ref_dec.header.height, frame_index });
        } else {
            print("JOD: {d:.4}\n", .{result.jod});
            if (verbose) {
                print("quality: {d:.6}\n", .{result.quality});
                print("model:   {s}\n", .{displayModelName(display_model)});
                print("frames:  {d}\n", .{frame_index});
            }
        }
        return;
    }

    const parallelism = workerCount(threads);
    var score_sink = ScoreSink{ .io = io };
    defer score_sink.deinit(allocator);
    var frame_index: usize = 0;

    if (parallelism == 1) {
        var worker = try VideoWorker.init(
            allocator,
            null,
            &score_sink,
            metric,
            ref_dec.header.width,
            ref_dec.header.height,
            butteraugli_options,
        );
        defer worker.deinit();

        while (true) {
            const ref_frame_opt = try ref_dec.readFrame();
            const dis_frame_opt = try dis_dec.readFrame();

            if (ref_frame_opt == null and dis_frame_opt == null) break;
            if (ref_frame_opt == null or dis_frame_opt == null) {
                if (ref_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                if (dis_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                print("Error: Video frame count does not match\n", .{});
                return error.FrameCountMismatch;
            }

            {
                var ref_frame = ref_frame_opt.?;
                defer ref_frame.deinit(allocator);
                var dis_frame = dis_frame_opt.?;
                defer dis_frame.deinit(allocator);

                try worker.processFrames(ref_frame, dis_frame);
            }
            frame_index += 1;
        }
    } else {
        const queue_capacity = @max(32, parallelism * 4);
        var queue = try VideoQueue.init(allocator, io, queue_capacity);
        defer queue.deinit();

        var workers = try allocator.alloc(VideoWorker, parallelism);
        var workers_len: usize = 0;
        defer {
            for (workers[0..workers_len]) |*worker| worker.deinit();
            allocator.free(workers);
        }

        while (workers_len < workers.len) : (workers_len += 1)
            workers[workers_len] = try VideoWorker.init(
                allocator,
                &queue,
                &score_sink,
                metric,
                ref_dec.header.width,
                ref_dec.header.height,
                butteraugli_options,
            );

        const spawned_count = parallelism - 1;
        var worker_threads = try allocator.alloc(std.Thread, spawned_count);
        var spawned_len: usize = 0;
        defer allocator.free(worker_threads);
        defer for (worker_threads[0..spawned_len]) |thread| thread.join();

        while (spawned_len < worker_threads.len) : (spawned_len += 1) {
            worker_threads[spawned_len] = try std.Thread.spawn(.{}, VideoWorker.worker, .{&workers[spawned_len]});
        }

        var produce_err: ?anyerror = null;
        while (true) {
            const ref_frame_opt = ref_dec.readFrame() catch |err| {
                produce_err = err;
                break;
            };
            const dis_frame_opt = dis_dec.readFrame() catch |err| {
                if (ref_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                produce_err = err;
                break;
            };

            if (ref_frame_opt == null and dis_frame_opt == null) break;
            if (ref_frame_opt == null or dis_frame_opt == null) {
                if (ref_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                if (dis_frame_opt) |frame| {
                    var mutable_frame = frame;
                    mutable_frame.deinit(allocator);
                }
                print("Error: Video frame count does not match\n", .{});
                produce_err = error.FrameCountMismatch;
                break;
            }

            if (!queue.push(.{
                .ref_frame = ref_frame_opt.?,
                .dis_frame = dis_frame_opt.?,
            })) {
                produce_err = error.MetricError;
                break;
            }
            frame_index += 1;
        }

        _ = queue.push(.{ .is_end = true });
        workers[spawned_count].worker();

        for (worker_threads[0..spawned_len]) |thread| thread.join();
        spawned_len = 0;

        if (produce_err) |err| return err;
        for (workers) |worker| {
            if (worker.err) |err| return err;
        }
    }

    if (frame_index == 0) return error.EmptyY4M;

    const stats = try computeVideoStats(allocator, score_sink.scores.items);

    if (json_output) {
        print(
            \\{{
            \\  "{s}": {d:.6},
            \\  "quality": {d:.6},
            \\  "reference": "{s}",
            \\  "distorted": "{s}",
            \\  "width": {d},
            \\  "height": {d},
            \\  "frames": {d}
            \\}}
        , .{ metricName(metric), stats.avg, stats.avg, ref_filename.?, dis_filename.?, ref_dec.header.width, ref_dec.header.height, frame_index });
    } else {
        print("{s}: {d:.6}\n", .{ metricName(metric), stats.avg });
        if (verbose) {
            print("frames:  {d}\n", .{stats.frames});
            print("avg:     {d:.8}\n", .{stats.avg});
            print("stddev:  {d:.8}\n", .{stats.stddev});
            print("median:  {d:.8}\n", .{stats.median});
            print("p5:      {d:.8}\n", .{stats.p5});
            print("p95:     {d:.8}\n", .{stats.p95});
            print("min:     {d:.8}\n", .{stats.min});
            print("max:     {d:.8}\n", .{stats.max});
            if (metric == .butteraugli) {
                print("nits:    {d:.4}\n", .{butteraugli_options.intensity_target});
                print("pnorm:   {d}\n", .{butteraugli_options.pnorm});
            }
        }
    }
    return;
}
