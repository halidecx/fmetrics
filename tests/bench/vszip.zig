const f = @import("filters/ssimulacra2.zig");
export fn upstream_size(w: u32, h: u32) usize {
    return f.scratchSize(w, h);
}
export fn upstream_cmp(a: [*]const f32, b: [*]const f32, w: u32, h: u32, scratch: [*]f32) f64 {
    const n: usize = @as(usize, w) * h;
    const r = f.Input{ .kind = .f32, .planes = .{ @ptrCast(a), @ptrCast(a + n), @ptrCast(a + 2 * n) }, .stride = @as(usize, w) * 4, .srgb = false };
    const d = f.Input{ .kind = .f32, .planes = .{ @ptrCast(b), @ptrCast(b + n), @ptrCast(b + 2 * n) }, .stride = @as(usize, w) * 4, .srgb = false };
    return f.process(r, d, w, h, scratch[0..f.scratchSize(w, h)]);
}
