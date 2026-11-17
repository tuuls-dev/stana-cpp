const std = @import("std");
const cpp = @import("build_support/cpp.zig");
const root = @import("build_support/project.zig");

pub const addCppExecutable = cpp.addCppExecutable;

pub fn build(b: *std.Build) void {
    var diagnostics: std.zon.parse.Diagnostics = undefined;
    const manifest = std.zon.parse.fromSlice(struct { version: []const u8 }, .{
        .gpa = b.allocator,
        .arena = b.allocator,
        .source = @embedFile("build.zig.zon"),
        .diagnostics = &diagnostics,
        .ignore_unknown_fields = true,
    }) catch @panic("invalid build.zig.zon");
    var project = cpp.Project.init(b, false);
    project.setup();
    if (b.isRoot()) root.build(&project, manifest.version);
}
