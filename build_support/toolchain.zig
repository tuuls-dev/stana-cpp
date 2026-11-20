const std = @import("std");
const builtin = @import("builtin");

const Build = std.Build;
pub const windows = builtin.os.tag == .windows;
pub const macos = builtin.os.tag == .macos;
pub const llvm_version = "22.1.8";
pub const icu_version = "78.3";
pub const tint_revision = "cb91d23e247042a2a5490c27d9b06300db91afef";
pub const library_name = if (windows) "stana.lib" else "libstana.a";
pub const tint_name = if (windows) "stana-tint.lib" else "libstana-tint.a";
pub const host_system = switch (builtin.os.tag) {
    .macos => "Darwin",
    .linux => "Linux",
    .windows => "Windows",
    else => @compileError("Unsupported Stana host"),
};
pub const host_architecture = switch (builtin.cpu.arch) {
    .aarch64 => "arm64",
    .x86_64 => "x86_64",
    else => @tagName(builtin.cpu.arch),
};

pub const Sanitizer = enum { none, address, @"address-undefined" };

pub const Settings = struct {
    version: []const u8,
    release: bool,
    sanitizer: Sanitizer,
    deployment: []const u8,

    pub fn init(b: *Build) Settings {
        if (builtin.zig_version.order(.{ .major = 0, .minor = 17, .patch = 1 }) != .eq)
            std.process.fatal("Stana requires Zig 0.17.1", .{});
        const optimize = b.standardOptimizeOption(.{});
        return .{
            .version = manifestVersion(b),
            .release = switch (optimize) {
                .debug => false,
                .fast => true,
                else => std.process.fatal("Stana supports Debug and --release=fast", .{}),
            },
            .sanitizer = b.option(Sanitizer, "sanitizer", "Runtime sanitizer (none, address, address-undefined)") orelse .none,
            .deployment = b.option([]const u8, "macos-deployment-target", "Minimum macOS version") orelse "26.0",
        };
    }

    /// The dependency already declared and validated these options during build().
    pub fn fromDependency(dependency: *Build.Dependency) Settings {
        const b = dependency.builder;
        const optimize = configuredScalar(b, "optimize");
        return .{
            .version = manifestVersion(b),
            .release = if (optimize) |mode| std.mem.eql(u8, mode, "fast") else b.graph.release_mode == .fast,
            .sanitizer = std.meta.stringToEnum(Sanitizer, configuredScalar(b, "sanitizer") orelse "none") orelse unreachable,
            .deployment = configuredScalar(b, "macos-deployment-target") orelse "26.0",
        };
    }

    pub fn configuration(settings: Settings) []const u8 {
        return if (settings.release) "Release" else "Debug";
    }
};

fn configuredScalar(b: *Build, name: []const u8) ?[]const u8 {
    return switch (b.user_input_options.get(name) orelse return null) {
        .scalar => |value| value,
        else => unreachable, // Settings.init already validated the option's type.
    };
}

fn manifestVersion(b: *Build) []const u8 {
    var diagnostics: std.zon.parse.Diagnostics = undefined;
    const manifest = std.zon.parse.fromSlice(struct { version: []const u8 }, .{
        .gpa = b.allocator,
        .arena = b.allocator,
        .source = b.allocator.dupeSentinel(u8, read(b, b.path("build.zig.zon")), 0) catch @panic("OOM"),
        .diagnostics = &diagnostics,
        .ignore_unknown_fields = true,
    }) catch @panic("Invalid build.zig.zon");
    return manifest.version;
}

pub const BuildInfo = struct {
    name: []const u8 = "stana",
    version: []const u8 = "",
    configuration: []const u8,
    system: []const u8 = host_system,
    architecture: []const u8 = host_architecture,
    compiler: []const u8 = "Clang",
    compiler_version: []const u8 = llvm_version,
    compiler_frontend: []const u8 = "GNU",
    macos_deployment_target: []const u8,
    cxx_standard: u8 = 23,
    private_shader_compiler_revision: []const u8 = tint_revision,
    sanitizer: Sanitizer,
    standard_library_macros: std.json.ArrayHashMap([]const u8),
    application_shader_contract: []const u8 = "WGSL and logical group/binding coordinates",
    bmi_policy: []const u8 = "Rebuilt from installed module interfaces by each consumer",
};

pub const Toolchain = struct {
    compiler: Build.LazyPath,
    compile_flags: []const []const u8,
    link_flags: []const []const u8,
    sdk_path: ?Build.LazyPath,
    icu_include: []const u8,
    icu_libs: [3]Build.LazyPath,
    tint_include: ?[]const u8,
    tint_archive: Build.LazyPath,
    tint_manifest: Build.LazyPath,
    tint_licenses: Build.LazyPath,
    runtime_path: ?[]const u8,
    info: Build.LazyPath,

    pub fn init(b: *Build, settings: Settings, installed: bool) Toolchain {
        return configure(b, settings, installed, null);
    }

    pub fn fromDependency(dependency: *Build.Dependency, settings: Settings) Toolchain {
        const b = dependency.builder;
        return configure(b, settings, b.named_lazy_paths.contains("installed-sdk"), dependency);
    }

    fn configure(b: *Build, settings: Settings, installed: bool, dependency: ?*Build.Dependency) Toolchain {
        const compiler = llvmTool(b, "clang++");
        var common: std.ArrayList([]const u8) = .empty;
        var link: std.ArrayList([]const u8) = .empty;
        append(b, &common, &.{ "-std=c++23", "-fexceptions", "-frtti", "-fstack-protector-strong" });
        var sdk_path: ?Build.LazyPath = null;
        switch (builtin.os.tag) {
            .macos => {
                if (dependency) |existing| {
                    sdk_path = existing.namedLazyPath("cpp-macos-sdk");
                } else {
                    const sdk = b.addSystemCommand(&.{ "/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path" });
                    // xcode-select can change without replacing xcrun itself.
                    sdk.has_side_effects = true;
                    sdk_path = sdk.captureStdOut(.{ .trim_whitespace = .all });
                }
                append(b, &common, &.{ b.fmt("-mmacosx-version-min={s}", .{settings.deployment}), "-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE" });
                append(b, &link, &.{b.fmt("-mmacosx-version-min={s}", .{settings.deployment})});
                for ([_][]const u8{ "AppKit", "UniformTypeIdentifiers", "ApplicationServices", "CoreGraphics", "CoreText", "Metal", "QuartzCore", "Foundation" }) |framework|
                    append(b, &link, &.{ "-framework", framework });
            },
            .linux => {
                append(b, &common, &.{ "-pthread", "-D_GLIBCXX_ASSERTIONS=1" });
                append(b, &link, &.{"-pthread"});
                if (settings.release) append(b, &common, &.{"-D_FORTIFY_SOURCE=3"});
            },
            .windows => {
                if (builtin.cpu.arch != .x86_64) std.process.fatal("Stana requires Windows x64", .{});
                if (settings.sanitizer == .@"address-undefined") std.process.fatal("Windows supports the address sanitizer only", .{});
                const platform_flags = &.{ "--target=x86_64-pc-windows-msvc", if (settings.release) "-fms-runtime-lib=dll" else "-fms-runtime-lib=dll_dbg" };
                append(b, &common, platform_flags);
                append(b, &link, platform_flags);
                append(b, &common, &.{"-D_MSVC_STL_HARDENING=1"});
                // The GNU driver adds libcmt; objects select the DLL CRT.
                append(b, &link, &.{ "-Xlinker", "/NODEFAULTLIB:libcmt" });
            },
            else => unreachable,
        }
        append(b, &common, if (settings.release) &.{ "-O3", "-DNDEBUG" } else &.{ "-O0", "-g" });
        if (settings.sanitizer != .none) {
            const flag = if (settings.sanitizer == .address) "-fsanitize=address" else "-fsanitize=address,undefined";
            append(b, &common, &.{ flag, "-fno-omit-frame-pointer", "-fno-sanitize-recover=all" });
            append(b, &link, &.{ flag, "-fno-sanitize-recover=all" });
            if (windows) append(b, &link, &.{ "-Xlinker", "/INCREMENTAL:NO", "-shared-libasan" });
        }
        var icu_libs: [3]Build.LazyPath = undefined;
        for (&icu_libs, if (windows) [_][]const u8{ "icuin.lib", "icuuc.lib", "icudt.lib" } else [_][]const u8{ "libicui18n.a", "libicuuc.a", "libicudata.a" }) |*path, name|
            path.* = b.path(b.fmt(".tools/icu/lib/{s}", .{name}));
        const tint_root = if (installed) "" else if (settings.sanitizer == .none) ".tools/tint" else b.fmt(".tools/tint-{t}", .{settings.sanitizer});
        var tools: Toolchain = .{
            .compiler = compiler,
            .compile_flags = common.items,
            .link_flags = link.items,
            .sdk_path = sdk_path,
            .icu_include = absolute(b, b.path(".tools/icu/include")),
            .icu_libs = icu_libs,
            .tint_include = if (installed) null else absolute(b, b.path(b.fmt("{s}/include", .{tint_root}))),
            .tint_archive = b.path(if (installed) "lib/" ++ tint_name else b.fmt("{s}/lib/{s}", .{ tint_root, tint_name })),
            .tint_manifest = b.path(if (installed) "share/stana/stana-tint.json" else b.fmt("{s}/stana-tint.json", .{tint_root})),
            .tint_licenses = b.path(if (installed) "share/stana/licenses/tint" else b.fmt("{s}/licenses", .{tint_root})),
            .runtime_path = if (windows) b.fmt("{s};{s};{s};{s}", .{ absolute(b, b.path(".tools/icu/bin")), absolute(b, b.path(".tools/llvm/bin")), absolute(b, b.path(".tools/llvm/lib/clang/22/lib/windows")), b.graph.environ_map.get("PATH") orelse "" }) else null,
            .info = undefined,
        };
        tools.info = if (dependency) |existing| existing.namedLazyPath("cpp-build-info") else tools.validate(b, settings, installed);
        return tools;
    }

    pub fn addCompileFlags(tools: Toolchain, run: *Build.Step.Run) void {
        run.addArgs(tools.compile_flags);
        if (tools.sdk_path) |sdk| {
            run.addArg("-isysroot");
            run.addFileContentArg2(sdk, .{});
            run.addArg("-isystem");
            run.addFileContentArg2(sdk, .{ .suffix = "/usr/include/c++/v1" });
        }
    }

    pub fn addLinkFlags(tools: Toolchain, run: *Build.Step.Run) void {
        run.addArgs(tools.link_flags);
        if (tools.sdk_path) |sdk| {
            run.addArg("-isysroot");
            run.addFileContentArg2(sdk, .{});
        }
    }

    fn validate(tools: Toolchain, b: *Build, settings: Settings, installed: bool) Build.LazyPath {
        const source = b.addWriteFiles().add("toolchain.cpp", @embedFile("toolchain.cpp.in"));
        const probe = Build.Step.Run.create(b, "verify compiler and standard library");
        probe.addFileArg2(tools.compiler, .{ .make_absolute = true });
        tools.addCompileFlags(probe);
        probe.addArgs(&.{ "-dM", "-E", "-x", "c++" });
        probe.addFileArg2(source, .{ .make_absolute = true });
        probe.addArgs(&.{ "-MD", "-MF" });
        _ = probe.addDepFileOutputArg2("headers.d", .{ .make_absolute = true });
        const macros = probe.captureStdOut(.{});
        const metadata = b.addExecutable(.{
            .name = "stana-metadata",
            .root_module = b.createModule(.{ .root_source_file = b.path("build_support/metadata.zig"), .target = b.graph.host, .optimize = .safe }),
        });
        const validation = b.addRunArtifact(metadata);
        validation.addArgs(&.{ settings.configuration(), @tagName(settings.sanitizer), settings.deployment, settings.version });
        validation.addFileArg2(macros, .{ .make_absolute = true });
        validation.addFileArg2(b.path(".tools/icu/include/unicode/uvernum.h"), .{ .make_absolute = true });
        validation.addFileArg2(tools.tint_manifest, .{ .make_absolute = true });
        const output = validation.addOutputFileArg2("build-info.json", .{ .make_absolute = true });
        if (installed) validation.addFileArg2(b.path("share/stana/build-info.json"), .{ .make_absolute = true });
        for (tools.icu_libs) |library| validation.addFileInput(library);
        validation.addFileInput(tools.tint_archive);
        validation.setName("verify dependencies and SDK compatibility");
        return output;
    }
};

pub fn llvmTool(b: *Build, name: []const u8) Build.LazyPath {
    return b.path(b.fmt(".tools/llvm/bin/{s}{s}", .{ name, if (windows) ".exe" else "" }));
}

pub fn absolute(b: *Build, path: Build.LazyPath) []const u8 {
    return switch (path) {
        .src_path => |p| b.pathResolve(&.{ p.owner.root.toString(b.allocator) catch @panic("OOM"), p.sub_path }),
        else => unreachable,
    };
}

pub fn read(b: *Build, path: Build.LazyPath) []const u8 {
    b.dependOnFileContents(path);
    return std.Io.Dir.cwd().readFileAlloc(b.graph.io, absolute(b, path), b.allocator, .limited(4 * 1024 * 1024)) catch |err|
        std.process.fatal("Cannot read {s}: {t}", .{ absolute(b, path), err });
}

fn append(b: *Build, list: *std.ArrayList([]const u8), values: []const []const u8) void {
    list.appendSlice(b.allocator, values) catch @panic("OOM");
}

pub const warnings = [_][]const u8{
    "-Wall",                     "-Wextra",                "-Wpedantic",         "-Werror",                         "-pedantic-errors",
    "-Wconversion",              "-Wsign-conversion",      "-Wshadow-all",       "-Wold-style-cast",                "-Wcast-align",
    "-Wcast-qual",               "-Wformat=2",             "-Wundef",            "-Wdouble-promotion",              "-Wimplicit-fallthrough",
    "-Wnon-virtual-dtor",        "-Woverloaded-virtual",   "-Wextra-semi",       "-Wzero-as-null-pointer-constant", "-Wdeprecated",
    "-Wdeprecated-declarations", "-Wdeprecated-copy-dtor", "-Wnull-dereference", "-Wunreachable-code-aggressive",   "-Wswitch-enum",
    "-Wcovered-switch-default",  "-Wmissing-prototypes",   "-Wmissing-noreturn", "-Wimplicit-int-conversion",       "-Wshorten-64-to-32",
    "-Wfloat-equal",             "-Wthread-safety",        "-Wdangling",         "-Wunsafe-buffer-usage",
};
