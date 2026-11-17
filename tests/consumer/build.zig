const std = @import("std");
const stana = @import("stana");

pub fn build(b: *std.Build) void {
    const optimize = b.standardOptimizeOption(.{});
    const deployment = b.option([]const u8, "macos-deployment-target", "Minimum macOS version") orelse "26.0";
    const rejected = b.option([]const u8, "reject", "A fixture expected to fail compilation");
    const dependency = b.dependency("stana", .{ .optimize = optimize, .@"macos-deployment-target" = deployment });
    const source = if (rejected) |fixture| b.fmt("{s}.cpp", .{fixture}) else "main.cpp";
    const exe = stana.addCppExecutable(b, dependency, .{
        .name = "consumer",
        .sources = &.{b.path(source)},
    });
    const run = b.addRunFile(exe);
    b.default_step.dependOn(&run.step);
}
