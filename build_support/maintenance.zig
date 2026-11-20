const std = @import("std");
const cpp = @import("cpp.zig");

pub fn build(b: *std.Build) void {
    const audit = b.step("audit", "Run standalone source audits and their contracts");
    for ([_][]const u8{ "tools/policy.py", "tools/namespace_policy.py", "tests/canvas_api_test.py" }) |file| {
        const run = cpp.python(b, b.path(file));
        run.addDirectoryArg2(b.path("."), .{ .make_absolute = true });
        audit.dependOn(&run.step);
    }
    audit.dependOn(&cpp.python(b, b.path("tests/namespace_policy_test.py")).step);

    var files: std.ArrayList([]const u8) = .empty;
    for ([_][]const u8{ "modules", "src", "tests", "examples" }) |folder| {
        var directory = std.Io.Dir.cwd().openDir(b.graph.io, cpp.toolchain.absolute(b, b.path(folder)), .{ .iterate = true }) catch @panic("Cannot open first-party sources");
        defer directory.close(b.graph.io);
        b.dependOnDirectoryContents(b.path(folder));
        var walker = directory.walk(b.allocator) catch @panic("OOM");
        defer walker.deinit();
        while (walker.next(b.graph.io) catch @panic("Cannot enumerate first-party sources")) |entry| {
            const path = b.pathJoin(&.{ folder, entry.path });
            if (entry.kind == .directory) b.dependOnDirectoryContents(b.path(path));
            if (entry.kind != .file) continue;
            const extension = std.fs.path.extension(entry.path);
            for ([_][]const u8{ ".cpp", ".cppm", ".cxx", ".cc", ".hpp", ".h", ".hxx", ".ixx", ".mm", ".m" }) |suffix| {
                if (!std.mem.eql(u8, extension, suffix)) continue;
                files.append(b.allocator, path) catch @panic("OOM");
                break;
            }
        }
    }
    std.mem.sort([]const u8, files.items, {}, struct {
        fn lessThan(_: void, a: []const u8, c: []const u8) bool {
            return std.mem.lessThan(u8, a, c);
        }
    }.lessThan);
    const formatter = cpp.toolchain.llvmTool(b, "clang-format");
    const version = cpp.Run.create(b, "verify clang-format version");
    cpp.addFile(version, formatter);
    version.addArg("--version");
    version.expectStdOutMatch("version " ++ cpp.toolchain.llvm_version);
    for ([_][]const u8{ "format", "format-check" }, [_]bool{ false, true }) |name, check| {
        const step = b.step(name, if (check) "Check first-party C++ formatting" else "Format first-party C++");
        const zig_format = b.addFmt(.{ .paths = &.{ b.path("build.zig"), b.path("build_support"), b.path("build_support/installed.zig.in") }, .check = check });
        step.dependOn(&zig_format.step);
        const run = cpp.Run.create(b, name);
        cpp.addFile(run, formatter);
        run.addArgs(if (check) &.{ "--dry-run", "--Werror" } else &.{"-i"});
        run.addFileInput(b.path(".clang-format"));
        for (files.items) |file| cpp.addFile(run, b.path(file));
        run.step.dependOn(&version.step);
        step.dependOn(&run.step);
    }
}
