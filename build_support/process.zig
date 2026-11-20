//! Supervise contracts with bounded execution; compilation stays in std.Build.
const std = @import("std");
const native = @import("native.zig");

pub fn main(init: std.process.Init) !void {
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);
    if (args.len < 4) return error.InvalidArguments;
    if (std.mem.eql(u8, args[1], "example")) {
        if (args.len != 4) return error.InvalidArguments;
        return native.launchExample(init, args[2], std.mem.eql(u8, args[3], "trace"));
    }
    if (std.mem.eql(u8, args[1], "native")) return native.run(init, args[2..]);
    if (!std.mem.eql(u8, args[1], "run")) return error.InvalidArguments;
    const seconds = try std.fmt.parseInt(i64, args[2], 10);
    const result = std.process.run(arena, init.io, .{
        .argv = args[3..],
        .timeout = .{ .duration = .{ .raw = .fromSeconds(seconds), .clock = .awake } },
        .stdout_limit = .limited(16 * 1024 * 1024),
        .stderr_limit = .limited(16 * 1024 * 1024),
    }) catch |err| std.process.fatal("Contract {s}: {t} (limit {d}s)", .{ args[3], err, seconds });
    try std.Io.File.stdout().writeStreamingAll(init.io, result.stdout);
    try std.Io.File.stderr().writeStreamingAll(init.io, result.stderr);
    if (!result.term.success()) std.process.fatal("Contract {s} {f}", .{ args[3], result.term });
}
