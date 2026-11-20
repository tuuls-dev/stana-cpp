//! Launch window fixtures through LaunchServices and verify their explicit result.
const std = @import("std");

pub fn run(init: std.process.Init, args: []const [:0]const u8) !void {
    if (@import("builtin").os.tag != .macos) return error.NativeTestsRequireMacOS;
    if (args.len != 3) return error.InvalidArguments;
    const arena = init.arena.allocator();
    const io = init.io;
    const result_path = try std.fs.path.join(arena, &.{ args[2], "result" });
    const stdout_path = try std.fs.path.join(arena, &.{ args[2], "stdout" });
    const stderr_path = try std.fs.path.join(arena, &.{ args[2], "stderr" });
    const executable = try std.fs.path.join(arena, &.{ args[0], "Contents/MacOS", args[1] });
    defer cleanup(init, result_path, executable);
    defer printLog(init, stdout_path, .stdout);
    defer printLog(init, stderr_path, .stderr);

    var command: std.ArrayList([]const u8) = .empty;
    try command.appendSlice(arena, &.{ "/usr/bin/open", "-n", "-W", "-o", stdout_path, "--stderr", stderr_path, "--env", try std.fmt.allocPrint(arena, "STANA_NATIVE_RESULT_FILE={s}", .{result_path}), "--env", "STANA_TRACE_NATIVE=1" });
    for ([_][]const u8{ "MTL_DEBUG_LAYER", "MTL_SHADER_VALIDATION", "STANA_MANUAL_FACILITIES" }) |key| {
        if (init.environ_map.get(key)) |value|
            try command.appendSlice(arena, &.{ "--env", try std.fmt.allocPrint(arena, "{s}={s}", .{ key, value }) });
    }
    try command.append(arena, args[0]);
    var launch = try io.concurrent(std.process.run, .{ arena, io, std.process.RunOptions{
        .argv = command.items,
        .timeout = .{ .duration = .{ .raw = .fromSeconds(30), .clock = .awake } },
    } });
    defer _ = launch.cancel(io) catch {};
    const ready = try std.fmt.allocPrint(arena, "{s}.ready", .{result_path});
    const start = std.Io.Timestamp.now(io, .awake);
    while (!exists(io, ready) and !exists(io, result_path) and start.durationTo(.now(io, .awake)).toMilliseconds() < 8000)
        try io.sleep(.fromMilliseconds(50), .awake);
    if (exists(io, ready) and !exists(io, result_path)) {
        const activate = try std.process.run(arena, io, .{
            .argv = &.{ "/usr/bin/open", "-a", args[0] },
            .timeout = .{ .duration = .{ .raw = .fromSeconds(5), .clock = .awake } },
        });
        if (!activate.term.success()) return error.NativeActivationFailed;
    }
    const finished = try launch.await(io);
    if (!finished.term.success()) return error.NativeLaunchFailed;
    const result = std.Io.Dir.cwd().readFileAlloc(io, result_path, arena, .limited(1024)) catch |err| {
        std.log.err("Native fixture {s} exited without an explicit result: {t}", .{ args[1], err });
        return error.MissingNativeResult;
    };
    if (!std.mem.eql(u8, std.mem.trim(u8, result, "\r\n "), "0")) return error.NativeContractFailed;
}

fn exists(io: std.Io, path: []const u8) bool {
    std.Io.Dir.cwd().access(io, path, .{}) catch return false;
    return true;
}

fn printLog(init: std.process.Init, path: []const u8, stream: enum { stdout, stderr }) void {
    const text = std.Io.Dir.cwd().readFileAlloc(init.io, path, init.arena.allocator(), .limited(16 * 1024 * 1024)) catch return;
    const file = if (stream == .stdout) std.Io.File.stdout() else std.Io.File.stderr();
    file.writeStreamingAll(init.io, text) catch {};
}

fn cleanup(init: std.process.Init, result: []const u8, executable: []const u8) void {
    const arena = init.arena.allocator();
    const io = init.io;
    const identity = std.fmt.allocPrint(arena, "{s}.pid", .{result}) catch return;
    const text = std.Io.Dir.cwd().readFileAlloc(io, identity, arena, .limited(128)) catch return;
    const pid = std.mem.trim(u8, text, "\r\n ");
    const number = std.fmt.parseInt(i32, pid, 10) catch return;
    if (number <= 0) return;
    const process = std.process.run(arena, io, .{ .argv = &.{ "/bin/ps", "-p", pid, "-o", "comm=" } }) catch return;
    if (!process.term.success()) return;
    const actual = std.Io.Dir.cwd().realPathFileAlloc(io, std.mem.trim(u8, process.stdout, "\r\n "), arena) catch return;
    const expected = std.Io.Dir.cwd().realPathFileAlloc(io, executable, arena) catch return;
    if (!std.mem.eql(u8, actual, expected)) return;
    _ = std.process.run(arena, io, .{ .argv = &.{ "/bin/kill", "-TERM", pid } }) catch {};
}

/// Launch an existing example as a bundle and keep traces attached to its terminal.
pub fn launchExample(init: std.process.Init, bundle: []const u8, trace: bool) !void {
    if (@import("builtin").os.tag != .macos) return error.NativeExamplesRequireMacOS;
    const arena = init.arena.allocator();
    const io = init.io;
    var command: std.ArrayList([]const u8) = .empty;
    try command.appendSlice(arena, &.{ "/usr/bin/open", "-W", "-n" });
    if (trace) try command.appendSlice(arena, &.{ "--env", "STANA_TRACE_NATIVE=1" });
    var terminal: [std.fs.max_path_bytes]u8 = undefined;
    if (try std.Io.File.stderr().isTty(io)) {
        const length = try std.Io.File.stderr().realPath(io, &terminal);
        try command.appendSlice(arena, &.{ "--stderr", terminal[0..length] });
    }
    try command.append(arena, bundle);
    var child = try std.process.spawn(io, .{ .argv = command.items });
    defer child.kill(io);
    if (!(try child.wait(io)).success()) return error.ExampleLaunchFailed;
}
