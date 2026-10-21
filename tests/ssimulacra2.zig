const std = @import("std");
const fm = @import("fmetrics");
const legacy = @import("legacy_ssimu2");

test "streamed scales retain reference scores across odd and small sizes" {
    const allocator = std.testing.allocator;
    var workspace = try fm.Workspace.init();
    defer workspace.deinit();
    for ([_][2]u32{ .{ 7, 9 }, .{ 8, 8 }, .{ 15, 17 }, .{ 32, 31 }, .{ 127, 95 }, .{ 319, 257 } }) |size| {
        const w, const h = .{ size[0], size[1] };
        const ref = try allocator.alloc(u8, @as(usize, w) * h * 3);
        defer allocator.free(ref);
        const dist = try allocator.alloc(u8, ref.len);
        defer allocator.free(dist);
        for (ref, dist, 0..) |*a, *b, i| {
            a.* = @intCast((i * 13 + i / (w * 3) * 7) % 224 + 16);
            b.* = a.* - @as(u8, @intCast(i % 9));
        }
        const r = try fm.Image.init(ref, w, h);
        const d = try fm.Image.init(dist, w, h);
        const expected = try legacy.computeSsimu2(allocator, ref, dist, w, h, 3, null);
        const actual = try fm.ssimu2(&workspace, r, d);
        try std.testing.expectApproxEqAbs(expected, actual, 0.25);
        try std.testing.expectEqual(@as(f64, 100), try fm.ssimu2(&workspace, r, r));
        const map = try allocator.alloc(u32, @as(usize, w) * h);
        defer allocator.free(map);
        try std.testing.expectApproxEqAbs(actual, try fm.ssimu2Map(&workspace, r, d, map), 0.00001);
    }
}
