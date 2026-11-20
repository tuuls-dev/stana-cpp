//! Validate provisioned dependency identities and write the SDK's build metadata.
//! Compiler probing belongs to the build graph; this tool never invokes a compiler.
const std = @import("std");
const tc = @import("toolchain.zig");

pub fn main(init: std.process.Init) !void {
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);
    if (args.len != 9 and args.len != 10) return error.InvalidArguments;
    const io = init.io;
    const macros = try read(arena, io, args[5]);
    const icu_header = try read(arena, io, args[6]);
    if (!std.mem.eql(u8, macro(icu_header, "U_ICU_VERSION") orelse "", "\"" ++ tc.icu_version ++ "\""))
        std.process.fatal("Expected ICU4C {s} at {s}; run just provision icu", .{ tc.icu_version, args[6] });
    const tint = try parse(struct {
        revision: []const u8,
        sanitizer: []const u8,
        system: []const u8,
        architecture: []const u8,
        compiler: []const u8,
        config: []const u8,
    }, arena, io, args[7]);
    const sanitizer = std.meta.stringToEnum(tc.Sanitizer, args[2]) orelse return error.InvalidSanitizer;
    const host = @import("builtin");
    const tint_arch = if (tc.windows) "amd64" else if (host.os.tag == .linux and host.cpu.arch == .aarch64) "aarch64" else tc.host_architecture;
    if (!std.mem.eql(u8, tint.revision, tc.tint_revision) or
        !std.mem.eql(u8, tint.sanitizer, if (sanitizer == .none) "" else args[2]) or
        !std.mem.eql(u8, tint.system, tc.host_system) or !std.ascii.eqlIgnoreCase(tint.architecture, tint_arch) or
        !matchesVersion(tint.compiler) or (tc.windows and !std.mem.eql(u8, tint.config, args[1])))
        std.process.fatal("Incompatible Tint at {s}; run just provision tint with matching configuration and sanitizer", .{args[7]});
    if (tc.macos) _ = deploymentVersion(args[3]);
    var info: tc.BuildInfo = .{
        .version = args[4],
        .configuration = args[1],
        .macos_deployment_target = if (tc.macos) args[3] else "",
        .sanitizer = sanitizer,
        .standard_library_macros = .{},
    };
    for ([_][]const u8{ "_LIBCPP_VERSION", "_LIBCPP_ABI_VERSION", "__GLIBCXX__", "_GLIBCXX_RELEASE", "_GLIBCXX_USE_CXX11_ABI", "_MSVC_STL_VERSION", "_MSVC_STL_UPDATE", "_MSC_VER", "_ITERATOR_DEBUG_LEVEL" }) |name| {
        if (macro(macros, name)) |value| try info.standard_library_macros.map.put(arena, name, value);
    }
    if (args.len == 10) {
        const previous = try parse(tc.BuildInfo, arena, io, args[9]);
        inline for (.{ "system", "architecture", "compiler_version", "configuration", "private_shader_compiler_revision" }) |field| {
            if (!std.mem.eql(u8, @field(previous, field), @field(info, field)))
                std.process.fatal("Installed SDK requires {s} {s}, found {s}", .{ field, @field(previous, field), @field(info, field) });
        }
        if (previous.sanitizer != info.sanitizer) return error.SdkSanitizerMismatch;
        const abi_macros: []const []const u8 = if (tc.macos) &.{"_LIBCPP_ABI_VERSION"} else if (tc.windows) &.{ "_MSVC_STL_VERSION", "_ITERATOR_DEBUG_LEVEL" } else &.{"_GLIBCXX_USE_CXX11_ABI"};
        for (abi_macros) |name| {
            if (!std.mem.eql(u8, previous.standard_library_macros.map.get(name) orelse "", info.standard_library_macros.map.get(name) orelse ""))
                std.process.fatal("Installed SDK requires matching {s}", .{name});
        }
        if (tc.macos and std.mem.order(u32, &deploymentVersion(info.macos_deployment_target), &deploymentVersion(previous.macos_deployment_target)) == .lt)
            std.process.fatal("Installed SDK requires macOS {s} or newer", .{previous.macos_deployment_target});
    }
    const json = try std.json.Stringify.valueAlloc(arena, info, .{ .whitespace = .indent_2 });
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = args[8], .data = json });
}

fn read(arena: std.mem.Allocator, io: std.Io, path: []const u8) ![]const u8 {
    return std.Io.Dir.cwd().readFileAlloc(io, path, arena, .limited(4 * 1024 * 1024));
}

fn parse(comptime T: type, arena: std.mem.Allocator, io: std.Io, path: []const u8) !T {
    return (try std.json.parseFromSlice(T, arena, try read(arena, io, path), .{ .ignore_unknown_fields = true })).value;
}

fn matchesVersion(output: []const u8) bool {
    const prefix = "version " ++ tc.llvm_version;
    const index = std.mem.indexOf(u8, output, prefix) orelse return false;
    const end = index + prefix.len;
    return end == output.len or (output[end] != '.' and !std.ascii.isDigit(output[end]));
}

fn macro(text: []const u8, name: []const u8) ?[]const u8 {
    var lines = std.mem.splitScalar(u8, text, '\n');
    while (lines.next()) |line| {
        if (!std.mem.startsWith(u8, line, "#define ")) continue;
        const definition = line[8..];
        if (definition.len > name.len and std.mem.eql(u8, definition[0..name.len], name) and definition[name.len] == ' ')
            return definition[name.len + 1 ..];
    }
    return null;
}

fn deploymentVersion(text: []const u8) [3]u32 {
    var version: [3]u32 = @splat(0);
    var parts = std.mem.splitScalar(u8, text, '.');
    var count: usize = 0;
    while (parts.next()) |part| {
        if (count == version.len) std.process.fatal("Invalid macOS deployment target: {s}", .{text});
        version[count] = std.fmt.parseInt(u32, part, 10) catch std.process.fatal("Invalid macOS deployment target: {s}", .{text});
        count += 1;
    }
    if (version[0] < 26) std.process.fatal("Stana requires macOS 26.0 or newer", .{});
    return version;
}
