//! Assemble Clang's -MJ fragments after Zig has committed outputs to its cache.
const std = @import("std");

const Command = struct {
    directory: []const u8,
    file: []const u8,
    output: []const u8,
    arguments: [][]const u8,
};

pub fn main(init: std.process.Init) !void {
    const arena = init.arena.allocator();
    const io = init.io;
    const args = try init.minimal.args.toSlice(arena);
    if (args.len < 3) return error.InvalidArguments;
    var commands: std.ArrayList(Command) = .empty;
    for (args[2..]) |path| {
        const text = try std.Io.Dir.cwd().readFileAlloc(io, path, arena, .limited(1024 * 1024));
        var command = (try std.json.parseFromSlice(Command, arena, std.mem.trim(u8, text, "\r\n ,"), .{ .ignore_unknown_fields = true })).value;
        const temporary = std.fs.path.dirname(command.output) orelse return error.MissingOutputDirectory;
        const cached = std.fs.path.dirname(path) orelse return error.MissingRecordDirectory;
        for (command.arguments) |*argument| {
            const prefix = if (std.mem.startsWith(u8, argument.*, "-fmodule-output=")) "-fmodule-output=" else "";
            const value = argument.*[prefix.len..];
            if (std.fs.path.isAbsolute(value) and std.mem.eql(u8, std.fs.path.dirname(value) orelse "", temporary))
                argument.* = try std.fmt.allocPrint(arena, "{s}{s}", .{ prefix, try std.fs.path.join(arena, &.{ cached, std.fs.path.basename(value) }) });
        }
        command.output = try std.fs.path.join(arena, &.{ cached, std.fs.path.basename(command.output) });
        try commands.append(arena, command);
    }
    const json = try std.json.Stringify.valueAlloc(arena, commands.items, .{ .whitespace = .indent_2 });
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = args[1], .data = json });
}
