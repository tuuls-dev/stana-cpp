const std = @import("std");
const cpp = @import("build_support/cpp.zig");
const project = @import("build_support/project.zig");
const maintenance = @import("build_support/maintenance.zig");

pub const addCppExecutable = cpp.addCppExecutable;

pub fn build(b: *std.Build) void {
    var stana = cpp.Project.init(b, false);
    stana.setup();
    if (b.isRoot()) {
        maintenance.build(b);
        project.build(&stana, stana.settings.version);
    }
}
