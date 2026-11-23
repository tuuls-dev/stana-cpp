const std = @import("std");
const sources = @import("sources.zig");
pub const toolchain = @import("toolchain.zig");

const Build = std.Build;
pub const Path = Build.LazyPath;
pub const Run = Build.Step.Run;
pub const macos = toolchain.macos;
pub const windows = toolchain.windows;
pub const library_name = toolchain.library_name;
pub const tint_name = toolchain.tint_name;
pub const Sanitizer = toolchain.Sanitizer;

pub const Module = struct { name: []const u8, bmi: Path };
pub const Unit = struct {
    object: Path,
    record: Path,
    bmi: ?Path = null,
};

pub const ExecutableOptions = struct {
    name: []const u8,
    sources: []const Path,
    include_dirs: []const Path = &.{},
    compile_flags: []const []const u8 = &.{},
    link_inputs: []const Path = &.{},
    link_flags: []const []const u8 = &.{},
};

const CompileOptions = struct {
    flags: []const []const u8 = &.{},
    imports: []const Module = &.{},
    include_dirs: []const Path = &.{},
    generated_header: ?Path = null,
    module: ?[]const u8 = null,
    precompile: bool = false,
    first_party: bool = true,

    fn addArguments(options: CompileOptions, b: *Build, tools: toolchain.Toolchain, source: Path, run: *Run) void {
        tools.addCompileFlags(run);
        if (options.first_party) {
            run.addArgs(&toolchain.warnings);
            if (windows) run.addArg("-WCL4");
        }
        run.addArgs(options.flags);
        for (options.include_dirs) |dir| run.addDirectoryArg2(dir, .{ .prefix = "-I", .make_absolute = true });
        if (options.generated_header) |header| {
            run.addFileInput(header);
            run.addDirectoryArg2(header.dirname(), .{ .prefix = "-I", .make_absolute = true });
        }
        for (options.imports) |dep| run.addFileArg2(dep.bmi, .{
            .prefix = b.fmt("-fmodule-file={s}=", .{dep.name}),
            .make_absolute = true,
        });
        const source_path = switch (source) {
            .src_path => |path| path.sub_path,
            .cwd_relative => |path| path,
            .generated => |path| path.sub_path,
            .dependency => |path| path.sub_path,
            .relative => |path| path.sub_path,
        };
        if (std.mem.endsWith(u8, source_path, ".mm")) run.addArg("-fobjc-arc");
    }
};

pub const Sdk = struct {
    tools: toolchain.Toolchain,
    inputs: []const Path,

    pub fn executable(sdk: Sdk, b: *Build, modules: []const Module, options: ExecutableOptions) Path {
        const objects = b.allocator.alloc(Path, options.sources.len) catch @panic("OOM");
        for (options.sources, objects, 0..) |source, *object, index| {
            object.* = sdk.compile(b, source, b.fmt("{s}-{d}", .{ options.name, index }), .{
                .imports = modules,
                .include_dirs = options.include_dirs,
                .flags = options.compile_flags,
                .first_party = false,
            }).object;
        }
        return sdk.link(b, options.name, objects, options.link_inputs, options.link_flags);
    }

    pub fn link(sdk: Sdk, b: *Build, name: []const u8, objects: []const Path, extra: []const Path, flags: []const []const u8) Path {
        const run = Run.create(b, b.fmt("link {s}", .{name}));
        addFile(run, sdk.tools.compiler);
        sdk.tools.info.addStepDependencies(&run.step);
        for (objects) |object| addFile(run, object);
        for (extra) |input| addFile(run, input);
        for (sdk.inputs) |input| addFile(run, input);
        sdk.tools.addLinkFlags(run);
        run.addArgs(flags);
        run.addArg("-o");
        return run.addOutputFileArg2(b.fmt("{s}{s}", .{ name, if (windows) ".exe" else "" }), .{ .make_absolute = true });
    }

    pub fn compile(sdk: Sdk, b: *Build, source: Path, name: []const u8, options: CompileOptions) Unit {
        const run = Run.create(b, b.fmt("compile {s}", .{name}));
        addFile(run, sdk.tools.compiler);
        sdk.tools.info.addStepDependencies(&run.step);
        options.addArguments(b, sdk.tools, source, run);
        run.addArg(if (options.precompile) "--precompile" else "-c");
        addFile(run, source);
        run.addArgs(&.{ "-MD", "-MF" });
        _ = run.addDepFileOutputArg2("headers.d", .{ .make_absolute = true });
        run.addArg("-MJ");
        const record = run.addOutputFileArg2("command.json", .{ .make_absolute = true });
        run.addArg("-o");
        const suffix = if (options.precompile) ".pcm" else if (windows) ".obj" else ".o";
        const object = run.addOutputFileArg2(b.fmt("{s}{s}", .{ name, suffix }), .{ .make_absolute = true });
        const bmi = if (options.precompile) object else if (options.module) |module_name|
            run.addOutputFileArg2(b.fmt("{s}.pcm", .{std.mem.replaceOwned(u8, b.allocator, module_name, ":", "-") catch @panic("OOM")}), .{
                .prefix = "-fmodule-output=",
                .make_absolute = true,
            })
        else
            null;
        return .{ .object = object, .record = record, .bmi = bmi };
    }

    fn publish(sdk: Sdk, b: *Build) void {
        b.addNamedLazyPath("cpp-build-info", sdk.tools.info);
        if (sdk.tools.sdk_path) |path| b.addNamedLazyPath("cpp-macos-sdk", path);
        const names = [_][]const u8{ "library", "tint", "icu-i18n", "icu-uc", "icu-data" };
        for (names, sdk.inputs) |name, path| b.addNamedLazyPath(name, path);
    }

    fn fromDependency(b: *Build, dependency: *Build.Dependency) Sdk {
        const settings = toolchain.Settings.fromDependency(dependency);
        return .{
            .tools = toolchain.Toolchain.fromDependency(dependency, settings),
            .inputs = b.allocator.dupe(Path, &.{
                dependency.namedLazyPath("library"),  dependency.namedLazyPath("tint"),
                dependency.namedLazyPath("icu-i18n"), dependency.namedLazyPath("icu-uc"),
                dependency.namedLazyPath("icu-data"),
            }) catch @panic("OOM"),
        };
    }
};

pub const Project = struct {
    b: *Build,
    settings: toolchain.Settings,
    sdk: Sdk,
    records: std.ArrayList(Path) = .empty,
    interfaces: [sources.interfaces.len]?Unit = @splat(null),
    active: [sources.interfaces.len]bool = @splat(false),
    installed: bool,
    analysis: *Build.Step,
    tidy: Path,
    metal_header: ?Path = null,
    runner: ?*Build.Step.Compile = null,

    pub fn init(b: *Build, installed: bool) Project {
        const settings = toolchain.Settings.init(b);
        // Tidy is intentionally not resolved here: building needs only the compiler.
        const tidy = toolchain.llvmTool(b, "clang-tidy");
        const analysis = b.step("analyze", "Run source audits and clang-tidy");
        b.step("check", "Alias for analyze").dependOn(analysis);
        if (installed) b.addNamedLazyPath("installed-sdk", b.path("share/stana/build-info.json"));
        return .{
            .b = b,
            .settings = settings,
            .sdk = .{ .tools = toolchain.Toolchain.init(b, settings, installed), .inputs = &.{} },
            .installed = installed,
            .analysis = analysis,
            .tidy = tidy,
        };
    }

    pub fn setup(project: *Project) void {
        const b = project.b;
        const library = if (project.installed) b.path("lib/" ++ library_name) else project.archive();
        const inputs = b.allocator.alloc(Path, 5) catch @panic("OOM");
        inputs[0] = library;
        inputs[1] = project.sdk.tools.tint_archive;
        @memcpy(inputs[2..], &project.sdk.tools.icu_libs);
        project.sdk.inputs = inputs;
        project.sdk.publish(b);
        for (sources.interfaces, 0..) |source, index| b.addNamedLazyPath(source.module.?, project.interface(index).bmi.?);
        project.addTidyProbe();
    }

    fn archive(project: *Project) Path {
        const b = project.b;
        if (macos) project.metal_header = embedShader(b, "src/platform/macos/shaders/stana.metal", null);
        const run = b.addSystemCommand(&.{b.graph.zig_exe});
        run.addArgs(if (windows) &.{ "lib", "/NOLOGO" } else if (macos) &.{ "ar", "--format=darwin", "rcs" } else &.{ "ar", "rcs" });
        const result = run.addOutputFileArg2(library_name, .{ .prefix = if (windows) "/OUT:" else "", .make_absolute = true });
        for (sources.interfaces, 0..) |_, index| addFile(run, project.interface(index).object);
        for (sources.portable) |source| addFile(run, project.libraryUnit(source).object);
        if (macos) {
            for (sources.macos) |source| addFile(run, project.libraryUnit(source).object);
        } else addFile(run, project.libraryUnit(.{ .path = "src/platform/unsupported.cpp" }).object);
        run.setName("archive Stana");
        return result;
    }

    fn interface(project: *Project, index: usize) Unit {
        if (project.interfaces[index]) |unit| return unit;
        if (project.active[index]) std.process.fatal("Cyclic dependency on {s}", .{sources.interfaces[index].module.?});
        project.active[index] = true;
        const unit = project.libraryUnit(sources.interfaces[index]);
        project.interfaces[index] = unit;
        project.active[index] = false;
        return unit;
    }

    fn addImport(project: *Project, modules: *std.ArrayList(Module), name: []const u8) void {
        for (modules.items) |entry| if (std.mem.eql(u8, entry.name, name)) return;
        for (sources.interfaces, 0..) |source, index| {
            if (!std.mem.eql(u8, source.module.?, name)) continue;
            const unit = project.interface(index);
            modules.append(project.b.allocator, .{ .name = name, .bmi = unit.bmi.? }) catch @panic("OOM");
            for (source.imports) |dep| project.addImport(modules, dep);
            return;
        }
        std.process.fatal("Unknown C++ module {s}", .{name});
    }

    fn libraryUnit(project: *Project, source: sources.Source) Unit {
        const b = project.b;
        const tools = project.sdk.tools;
        var flags: std.ArrayList([]const u8) = .empty;
        if (!windows) flags.append(b.allocator, "-fPIC") catch @panic("OOM");
        if (std.mem.eql(u8, source.path, "src/gpu/shader/compiler.cpp") or std.mem.eql(u8, source.path, "src/platform/macos/shader_compiler.cpp"))
            flags.appendSlice(b.allocator, &.{ "-isystem", tools.tint_include.?, "-DDAWN_HAVE_UTILS_BUILD_CONFIG", "-DDAWN_ENABLE_ASSERTS" }) catch @panic("OOM");
        if (std.mem.eql(u8, source.path, "src/text/text.cpp") or std.mem.eql(u8, source.path, "src/application/window_configuration.cpp"))
            flags.appendSlice(b.allocator, &.{ "-isystem", tools.icu_include }) catch @panic("OOM");
        var imports: std.ArrayList(Module) = .empty;
        for (source.imports) |name| project.addImport(&imports, name);
        const name = std.mem.replaceOwned(u8, b.allocator, source.path, "/", "_") catch @panic("OOM");
        return project.compile(b.path(source.path), name, .{
            .imports = imports.items,
            .flags = flags.items,
            .module = source.module,
            .precompile = project.installed,
            .generated_header = if (std.mem.eql(u8, source.path, "src/platform/macos/gpu_resources.mm")) project.metal_header else null,
        });
    }

    fn compile(project: *Project, source: Path, name: []const u8, options: CompileOptions) Unit {
        const b = project.b;
        const unit = project.sdk.compile(b, source, name, options);
        project.records.append(b.allocator, unit.record) catch @panic("OOM");
        const tidy = Run.create(b, b.fmt("clang-tidy {s}", .{name}));
        addFile(tidy, project.tidy);
        tidy.addArg("--experimental-custom-checks");
        tidy.addFileArg2(b.path(".clang-tidy"), .{ .prefix = "--config-file=", .make_absolute = true });
        addFile(tidy, source);
        tidy.addArg("--");
        options.addArguments(b, project.sdk.tools, source, tidy);
        // Tooling strips driver dependency flags. Pass these directly to cc1 so
        // Zig tracks the headers read by this analysis, including system headers.
        tidy.addArg("-Xclang=-dependency-file");
        _ = tidy.addDepFileOutputArg2("headers.d", .{ .prefix = "-Xclang=", .make_absolute = true });
        tidy.addArgs(&.{ "-Xclang=-MT", "-Xclang=analysis", "-Xclang=-sys-header-deps" });
        tidy.addFileInput(unit.object);
        tidy.expectStdOutEqual("");
        project.analysis.dependOn(&tidy.step);
        return unit;
    }

    pub fn executable(project: *Project, name: []const u8, files: []const []const u8, header: ?Path) Path {
        const b = project.b;
        var imports: std.ArrayList(Module) = .empty;
        project.addImport(&imports, "stana");
        var objects: std.ArrayList(Path) = .empty;
        for (files, 0..) |file, index| {
            const unit = project.compile(b.path(file), b.fmt("{s}-{d}", .{ name, index }), .{ .imports = imports.items, .generated_header = header });
            objects.append(b.allocator, unit.object) catch @panic("OOM");
        }
        return project.sdk.link(b, name, objects.items, &.{}, &.{});
    }

    pub fn compileFail(project: *Project, source: Path) *Run {
        return compileFailMatching(project, source, "constant expression");
    }

    pub fn compileFailMatching(project: *Project, source: Path, marker: []const u8) *Run {
        const b = project.b;
        var imports: std.ArrayList(Module) = .empty;
        project.addImport(&imports, "stana");
        const run = Run.create(b, b.fmt("stana.compiler-fail {f}", .{source}));
        addFile(run, project.sdk.tools.compiler);
        project.sdk.tools.info.addStepDependencies(&run.step);
        (CompileOptions{ .imports = imports.items }).addArguments(b, project.sdk.tools, source, run);
        run.addArg("-fsyntax-only");
        addFile(run, source);
        run.expectExitCode(1);
        run.expectStdErrMatch(marker);
        return run;
    }

    fn addTidyProbe(project: *Project) void {
        const b = project.b;
        const writer = b.addWriteFiles();
        const source = writer.add("tidy-probe.cppm", "export module stana_toolchain_probe;\nexport auto rejected() -> int* { return new int((int)1); }\n");
        const probe = Run.create(b, "verify required clang-tidy checks");
        addFile(probe, project.tidy);
        probe.addArg("--experimental-custom-checks");
        probe.addFileArg2(b.path(".clang-tidy"), .{ .prefix = "--config-file=", .make_absolute = true });
        addFile(probe, source);
        probe.addArgs(&.{ "--", "-std=c++23" });
        probe.expectExitCode(1);
        probe.expectStdOutMatch("custom-no-raw-allocation");
        probe.expectStdOutMatch("custom-no-c-style-cast");
        probe.expectStdErrMatch("warnings treated as errors");
        project.analysis.dependOn(&probe.step);
        const config = std.mem.replaceOwned(u8, b.allocator, toolchain.read(b, b.path(".clang-tidy")), "  custom-*", "") catch @panic("OOM");
        const verification_config = writer.add("verify-tidy.yaml", config);
        const verify_config = Run.create(b, "verify clang-tidy configuration");
        addFile(verify_config, project.tidy);
        verify_config.addArgs(&.{ "--experimental-custom-checks", "--verify-config" });
        verify_config.addFileArg2(verification_config, .{ .prefix = "--config-file=", .make_absolute = true });
        verify_config.expectExitCode(0);
        project.analysis.dependOn(&verify_config.step);
        const version = Run.create(b, "verify clang-tidy version");
        addFile(version, project.tidy);
        version.expectStdOutMatch("version " ++ toolchain.llvm_version);
        version.addArg("--version");
        project.analysis.dependOn(&version.step);
    }

    pub fn runtimeEnvironment(project: *const Project, run: *Run) void {
        if (project.sdk.tools.runtime_path) |path| run.setEnvironmentVariable("PATH", path);
        if (project.settings.sanitizer != .none) run.setEnvironmentVariable("ASAN_OPTIONS", "halt_on_error=1:abort_on_error=1");
        if (project.settings.sanitizer == .@"address-undefined") run.setEnvironmentVariable("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1");
    }

    pub fn hostTool(project: *Project) *Build.Step.Compile {
        if (project.runner) |runner| return runner;
        const b = project.b;
        const runner = b.addExecutable(.{
            .name = "stana-process",
            .root_module = b.createModule(.{ .root_source_file = b.path("build_support/process.zig"), .target = b.graph.host, .optimize = .safe }),
        });
        project.runner = runner;
        return runner;
    }
};

pub fn embedShader(b: *Build, source_path: []const u8, template_path: ?[]const u8) Path {
    const source = toolchain.read(b, b.path(source_path));
    const delimiter = if (template_path != null) "STANA_APP" else "STANA_SHADER";
    if (std.mem.indexOf(u8, source, b.fmt("){s}\"", .{delimiter})) != null)
        std.process.fatal("Shader {s} contains its raw-string delimiter", .{source_path});
    const header = if (template_path) |template|
        std.mem.replaceOwned(u8, b.allocator, toolchain.read(b, b.path(template)), "@stana_canvas_wgsl@", source) catch @panic("OOM")
    else
        b.fmt("#ifndef STANA_EMBEDDED_SHADER_HPP\n#define STANA_EMBEDDED_SHADER_HPP\n#include <string_view>\nnamespace stana::platform::detail::gpu {{\ninline constexpr std::string_view builtinShader = R\"STANA_SHADER({s})STANA_SHADER\";\n}}\n#endif\n", .{source});
    return b.addWriteFiles().add(if (template_path != null) "canvas_resource.hpp" else "stana_shader.hpp", header);
}

pub fn pythonName() []const u8 {
    return if (windows) "python" else "python3";
}

pub fn python(b: *Build, script: Path) *Run {
    const run = b.addSystemCommand(&.{pythonName()});
    addFile(run, script);
    return run;
}

pub fn addFile(run: *Run, file: Path) void {
    run.addFileArg2(file, .{ .make_absolute = true });
}

pub fn addCppExecutable(b: *Build, dependency: *Build.Dependency, options: ExecutableOptions) Path {
    const sdk = Sdk.fromDependency(b, dependency);
    var modules: [sources.interfaces.len]Module = undefined;
    for (sources.interfaces, &modules) |source, *entry| entry.* = .{ .name = source.module.?, .bmi = dependency.namedLazyPath(source.module.?) };
    return sdk.executable(b, &modules, options);
}
