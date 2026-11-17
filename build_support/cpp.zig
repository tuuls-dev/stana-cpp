const std = @import("std");
const builtin = @import("builtin");
const sources = @import("sources.zig");

const Build = std.Build;
pub const Path = Build.LazyPath;
pub const Run = Build.Step.Run;
pub const macos = builtin.os.tag == .macos;
pub const windows = builtin.os.tag == .windows;
pub const library_name = if (windows) "stana.lib" else "libstana.a";
pub const tint_name = if (windows) "stana-tint.lib" else "libstana-tint.a";

pub const Sanitizer = enum { none, address, @"address-undefined" };

const Toolchain = struct {
    cxx: []const u8,
    tidy: []const u8,
    scanner: []const u8,
    icu_include: []const u8,
    icu_libs: []const []const u8,
    runtime_path: []const u8,
    tint_include: []const u8,
    tint_archive: []const u8,
    tint_manifest: []const u8,
    tint_licenses: []const u8,
    metadata: []const u8,
};

pub const Module = struct {
    name: []const u8,
    bmi: Path,
};

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

pub const Sdk = struct {
    script: Path,
    config: Path,
    tidy_config: Path,
    inputs: []const Path,
    compiler: Path,
    tidy: ?Path,

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
        const run = python(b, sdk.script);
        run.addArg("link");
        addFile(run, sdk.config);
        run.addFileInput(sdk.compiler);
        const result = run.addOutputFileArg2(b.fmt("{s}{s}", .{ name, if (windows) ".exe" else "" }), .{ .make_absolute = true });
        for (objects) |object| addFile(run, object);
        for (extra) |input| addFile(run, input);
        for (sdk.inputs) |input| addFile(run, input);
        run.addArg("--");
        // The GNU driver adds libcmt at link time. The COFF objects select the DLL CRT.
        if (windows) run.addArgs(&.{ "-Xlinker", "/NODEFAULTLIB:libcmt" });
        run.addArgs(flags);
        run.setName(b.fmt("link {s}", .{name}));
        return result;
    }

    pub fn compile(sdk: Sdk, b: *Build, source: Path, name: []const u8, options: CompileOptions) Unit {
        const run = python(b, sdk.script);
        run.addArg("compile");
        addFile(run, sdk.config);
        run.addFileInput(sdk.compiler);
        const record = run.addOutputFileArg2("command.json", .{ .make_absolute = true });
        if (options.first_party) {
            if (sdk.tidy) |tidy| run.addFileInput(tidy);
            addFile(run, sdk.tidy_config);
        } else {
            run.addArg("");
        }
        addFile(run, source);
        const suffix = if (options.precompile) ".pcm" else if (windows) ".obj" else ".o";
        const object = run.addOutputFileArg2(b.fmt("{s}{s}", .{ name, suffix }), .{ .make_absolute = true });
        _ = run.addDepFileOutputArg2("headers.d", .{ .make_absolute = true });
        run.addArgs(options.flags);
        for (options.include_dirs) |dir| run.addDirectoryArg2(dir, .{ .prefix = "-I", .make_absolute = true });
        if (options.generated_header) |header| {
            run.addFileInput(header);
            run.addDirectoryArg2(header.dirname(), .{ .prefix = "-I", .make_absolute = true });
        }
        for (options.imports) |dep| {
            run.addFileArg2(dep.bmi, .{ .prefix = b.fmt("-fmodule-file={s}=", .{dep.name}), .make_absolute = true });
        }
        const bmi = if (options.precompile) object else if (options.module) |module_name|
            run.addOutputFileArg2(b.fmt("{s}.pcm", .{moduleFileName(b, module_name)}), .{
                .prefix = "-fmodule-output=",
                .make_absolute = true,
            })
        else
            null;
        if (options.precompile) run.addArg("--precompile");
        run.setName(b.fmt("compile {s}", .{name}));
        return .{ .object = object, .record = record, .bmi = bmi };
    }

    fn publish(sdk: Sdk, b: *Build) void {
        b.addNamedLazyPath("cpp-config", sdk.config);
        b.addNamedLazyPath("cpp-compiler", sdk.compiler);
        if (sdk.tidy) |tidy| b.addNamedLazyPath("cpp-tidy", tidy);
        const names = [_][]const u8{ "library", "tint", "icu-i18n", "icu-uc", "icu-data" };
        for (names, sdk.inputs) |name, path| b.addNamedLazyPath(name, path);
    }

    fn fromDependency(b: *Build, dependency: *Build.Dependency) Sdk {
        const inputs = b.allocator.dupe(Path, &.{
            dependency.namedLazyPath("library"),
            dependency.namedLazyPath("tint"),
            dependency.namedLazyPath("icu-i18n"),
            dependency.namedLazyPath("icu-uc"),
            dependency.namedLazyPath("icu-data"),
        }) catch @panic("OOM");
        return .{
            .script = dependency.path("tools/stana_build.py"),
            .config = dependency.namedLazyPath("cpp-config"),
            .tidy_config = dependency.path(".clang-tidy"),
            .compiler = dependency.namedLazyPath("cpp-compiler"),
            .tidy = dependency.builder.named_lazy_paths.get("cpp-tidy"),
            .inputs = inputs,
        };
    }
};

const CompileOptions = struct {
    flags: []const []const u8 = &.{},
    imports: []const Module = &.{},
    include_dirs: []const Path = &.{},
    generated_header: ?Path = null,
    module: ?[]const u8 = null,
    precompile: bool = false,
    first_party: bool = true,
};

pub const Project = struct {
    b: *Build,
    tools: Toolchain,
    sdk: Sdk,
    sanitizer: Sanitizer,
    deployment: []const u8,
    configuration: []const u8,
    records: std.ArrayList(Path) = .empty,
    scan_records: std.ArrayList(Path) = .empty,
    scan_rules: std.ArrayList(Path) = .empty,
    rules: *Build.Step.WriteFile,
    interfaces: [sources.interfaces.len]?Unit = @splat(null),
    active: [sources.interfaces.len]bool = @splat(false),
    installed: bool,
    policy: *Build.Step,
    metal_header: ?Path = null,
    version_record: ?Path = null,

    pub fn init(b: *Build, installed: bool) Project {
        const zig_version: std.SemanticVersion = .{ .major = 0, .minor = 17, .patch = 1 };
        if (builtin.zig_version.order(zig_version) != .eq) {
            std.process.fatal("Stana requires Zig {f}", .{zig_version});
        }
        const optimize = b.standardOptimizeOption(.{});
        const configuration = switch (optimize) {
            .debug => "Debug",
            .fast => "Release",
            else => std.process.fatal("Stana supports Debug and --release=fast", .{}),
        };
        const sanitizer = b.option(Sanitizer, "sanitizer", "Runtime sanitizer (none, address, address-undefined)") orelse .none;
        const checks = b.option(bool, "checks", "Run source policy and clang-tidy") orelse true;
        const deployment = b.option([]const u8, "macos-deployment-target", "Minimum macOS version") orelse "26.0";
        const root = b.root.toString(b.allocator) catch @panic("OOM");
        const script = b.path("tools/stana_build.py");
        const config = b.run(&.{
            pythonName(), b.pathJoin(&.{ root, "tools/stana_build.py" }), "toolchain",
            root,         configuration,                                  @tagName(sanitizer),
            deployment,   if (checks) "true" else "false",                if (installed) "true" else "false",
        });
        var tools = (std.json.parseFromSlice(Toolchain, b.allocator, config, .{ .allocate = .alloc_always, .ignore_unknown_fields = true }) catch
            std.process.fatal("Invalid Stana toolchain description", .{})).value;
        if (windows and sanitizer != .none) {
            const printed = b.run(&.{ tools.cxx, "--target=x86_64-pc-windows-msvc", "-print-runtime-dir" });
            const runtime = std.mem.trim(u8, printed, " \r\n");
            if (std.fs.path.isAbsolute(runtime)) {
                tools.runtime_path = b.fmt("{s};{s}", .{ runtime, tools.runtime_path });
            }
        }
        const policy = b.step("check", "Run Stana's source policy and analysis configuration checks");
        const configuration_files = b.addWriteFiles();
        configuration_files.step.dependOn(policy);
        const sdk: Sdk = .{
            .script = script,
            .config = configuration_files.add("toolchain.json", config),
            .tidy_config = b.path(".clang-tidy"),
            .inputs = &.{},
            .compiler = b.graph.cwdRelativePath(tools.cxx),
            .tidy = if (checks) b.graph.cwdRelativePath(tools.tidy) else null,
        };
        if (checks) {
            const probe = python(b, script);
            probe.addArg("tidy-probe");
            addFile(probe, sdk.tidy.?);
            addFile(probe, sdk.tidy_config);
            _ = probe.addOutputFileArg2("verified", .{ .make_absolute = true });
            probe.setName("verify clang-tidy policy");
            policy.dependOn(&probe.step);
            if (!installed) {
                for ([_][]const u8{ "tools/policy.py", "tools/namespace_policy.py" }) |file| {
                    const run = python(b, b.path(file));
                    run.addDirectoryArg2(b.path("."), .{ .make_absolute = true });
                    policy.dependOn(&run.step);
                }
            }
        }
        return .{
            .b = b,
            .tools = tools,
            .sdk = sdk,
            .sanitizer = sanitizer,
            .deployment = deployment,
            .configuration = configuration,
            .rules = b.addWriteFiles(),
            .installed = installed,
            .policy = policy,
        };
    }

    pub fn setup(project: *Project) void {
        const b = project.b;
        const library = if (project.installed) b.path("lib/" ++ library_name) else project.archive();
        const inputs = b.allocator.alloc(Path, 5) catch @panic("OOM");
        inputs[0] = library;
        inputs[1] = b.graph.cwdRelativePath(project.tools.tint_archive);
        for (inputs[2..], project.tools.icu_libs) |*input, path| input.* = b.graph.cwdRelativePath(path);
        project.sdk.inputs = inputs;
        project.sdk.publish(b);
        for (sources.interfaces, 0..) |source, index| {
            b.addNamedLazyPath(source.module.?, project.interface(index).bmi.?);
        }
    }

    fn archive(project: *Project) Path {
        const b = project.b;
        if (macos) {
            const embed = python(b, b.path("tools/embed_shader.py"));
            addFile(embed, b.path("src/platform/macos/shaders/stana.metal"));
            project.metal_header = embed.addOutputFileArg2("stana_shader.hpp", .{ .make_absolute = true });
        }
        const run = b.addSystemCommand(&.{b.graph.zig_exe});
        run.step.dependOn(project.policy);
        run.addArgs(if (windows) &.{ "lib", "/NOLOGO" } else if (macos) &.{ "ar", "--format=darwin", "rcs" } else &.{ "ar", "rcs" });
        const result = run.addOutputFileArg2(library_name, .{
            .prefix = if (windows) "/OUT:" else "",
            .make_absolute = true,
        });
        for (sources.interfaces, 0..) |_, index| addFile(run, project.interface(index).object);
        for (sources.portable) |source| addFile(run, project.libraryUnit(source).object);
        if (macos) {
            for (sources.macos) |source| addFile(run, project.libraryUnit(source).object);
        } else {
            addFile(run, project.libraryUnit(.{ .path = "src/platform/unsupported.cpp" }).object);
        }
        run.setName("archive Stana");
        return result;
    }

    fn interface(project: *Project, index: usize) Unit {
        if (project.interfaces[index]) |unit| return unit;
        const source = sources.interfaces[index];
        if (project.active[index]) std.process.fatal("Cyclic dependency on {s}", .{source.module.?});
        project.active[index] = true;
        const unit = project.libraryUnit(source);
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
        var flags: std.ArrayList([]const u8) = .empty;
        if (!windows) flags.append(b.allocator, "-fPIC") catch @panic("OOM");
        if (std.mem.eql(u8, source.path, "src/gpu/shader/compiler.cpp") or
            std.mem.eql(u8, source.path, "src/platform/macos/shader_compiler.cpp"))
        {
            flags.appendSlice(b.allocator, &.{ "-isystem", project.tools.tint_include, "-DDAWN_HAVE_UTILS_BUILD_CONFIG", "-DDAWN_ENABLE_ASSERTS" }) catch @panic("OOM");
        }
        if (std.mem.eql(u8, source.path, "src/text/text.cpp") or
            std.mem.eql(u8, source.path, "src/application/window_configuration.cpp"))
        {
            flags.appendSlice(b.allocator, &.{ "-isystem", project.tools.icu_include }) catch @panic("OOM");
        }
        var imports: std.ArrayList(Module) = .empty;
        for (source.imports) |name| project.addImport(&imports, name);
        const name = std.mem.replaceOwned(u8, b.allocator, source.path, "/", "_") catch @panic("OOM");
        const unit = project.sdk.compile(b, b.path(source.path), name, .{
            .imports = imports.items,
            .flags = flags.items,
            .module = source.module,
            .precompile = project.installed,
            .generated_header = if (std.mem.eql(u8, source.path, "src/platform/macos/gpu_resources.mm"))
                project.metal_header
            else
                null,
        });
        project.records.append(b.allocator, unit.record) catch @panic("OOM");
        if (std.mem.eql(u8, source.path, "src/version.cpp")) project.version_record = unit.record;
        if (project.tools.scanner.len != 0 and !std.mem.endsWith(u8, source.path, ".mm")) {
            const expected = std.json.Stringify.valueAlloc(b.allocator, .{
                .provides = if (source.module) |provided| @as([]const []const u8, &.{provided}) else &.{},
                .requires = source.imports,
            }, .{}) catch @panic("OOM");
            project.scan_records.append(b.allocator, unit.record) catch @panic("OOM");
            project.scan_rules.append(b.allocator, project.rules.add(b.fmt("{s}.json", .{name}), expected)) catch @panic("OOM");
        }
        return unit;
    }

    pub fn executable(project: *Project, name: []const u8, files: []const []const u8, header: ?Path) Unit {
        const b = project.b;
        var imports: std.ArrayList(Module) = .empty;
        project.addImport(&imports, "stana");
        var objects: std.ArrayList(Path) = .empty;
        var first_record: Path = undefined;
        for (files, 0..) |file, index| {
            const unit = project.sdk.compile(b, b.path(file), b.fmt("{s}-{d}", .{ name, index }), .{
                .imports = imports.items,
                .generated_header = header,
            });
            if (index == 0) first_record = unit.record;
            objects.append(b.allocator, unit.object) catch @panic("OOM");
            project.records.append(b.allocator, unit.record) catch @panic("OOM");
        }
        return .{ .object = project.sdk.link(b, name, objects.items, &.{}, &.{}), .record = first_record };
    }

    pub fn publicInterfaceRecord(project: *Project) Path {
        for (sources.interfaces, 0..) |source, index| {
            if (std.mem.eql(u8, source.module.?, "stana")) return project.interface(index).record;
        }
        unreachable;
    }

    pub fn runtimeEnvironment(project: *const Project, run: *Run) void {
        if (project.tools.runtime_path.len != 0) run.setEnvironmentVariable("PATH", project.tools.runtime_path);
        if (project.sanitizer != .none) run.setEnvironmentVariable("ASAN_OPTIONS", "halt_on_error=1:abort_on_error=1");
        if (project.sanitizer == .@"address-undefined") run.setEnvironmentVariable("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1");
    }

    pub fn verifyModules(project: *Project, step: *Build.Step) void {
        if (project.tools.scanner.len == 0) return;
        const b = project.b;
        const run = python(b, project.sdk.script);
        run.addArg("verify-modules");
        addFile(run, b.graph.cwdRelativePath(project.tools.scanner));
        _ = run.addOutputFileArg2("verified", .{ .make_absolute = true });
        for (project.scan_records.items) |record| addFile(run, record);
        for (project.scan_rules.items) |rule| addFile(run, rule);
        run.setName("verify C++ module graph with clang-scan-deps");
        step.dependOn(&run.step);
    }
};

fn moduleFileName(b: *Build, name: []const u8) []const u8 {
    return std.mem.replaceOwned(u8, b.allocator, name, ":", "-") catch @panic("OOM");
}

pub fn pythonName() []const u8 {
    return if (windows) "python" else "python3";
}

pub fn python(b: *Build, script: Path) *Run {
    const command = b.addSystemCommand(&.{pythonName()});
    addFile(command, script);
    return command;
}

pub fn addFile(run: *Run, file: Path) void {
    run.addFileArg2(file, .{ .make_absolute = true });
}

pub fn addCppExecutable(b: *Build, dependency: *Build.Dependency, options: ExecutableOptions) Path {
    const sdk = Sdk.fromDependency(b, dependency);
    var modules: [sources.interfaces.len]Module = undefined;
    for (sources.interfaces, &modules) |source, *entry| {
        const name = source.module.?;
        entry.* = .{ .name = name, .bmi = dependency.namedLazyPath(name) };
    }
    return sdk.executable(b, &modules, options);
}
