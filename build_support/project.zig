const std = @import("std");
const cpp = @import("cpp.zig");

const Build = std.Build;
const Path = cpp.Path;
const Project = cpp.Project;

pub fn build(project: *Project, version: []const u8) void {
    const b = project.b;
    const tests = b.step("test", "Build and run the Stana contracts");
    addContracts(project, tests);
    addExamples(project, tests, version);
    const compdb_tool = b.addExecutable(.{
        .name = "stana-compdb",
        .root_module = b.createModule(.{ .root_source_file = b.path("build_support/compdb.zig"), .target = b.graph.host, .optimize = .safe }),
    });
    const compdb = b.addRunArtifact(compdb_tool);
    const database = compdb.addOutputFileArg2("compile_commands.json", .{ .make_absolute = true });
    for (project.records.items) |record| cpp.addFile(compdb, record);
    const update = b.addUpdateSourceFiles();
    update.addCopyFileToSource(database, "compile_commands.json");
    b.step("compdb", "Write compile_commands.json for clangd").dependOn(&update.step);

    const verify = b.step("verify", "Build, check formatting, analyze, and run contracts");
    verify.dependOn(b.getInstallStep());
    verify.dependOn(&b.top_level_steps.get("examples").?.step);
    verify.dependOn(&b.top_level_steps.get("format-check").?.step);
    verify.dependOn(project.analysis);
    verify.dependOn(tests);
    project.analysis.dependOn(&b.top_level_steps.get("audit").?.step);

    const package = b.step("package", "Stage and archive a relocatable SDK");
    if (project.settings.sanitizer != .none) {
        b.getInstallStep().dependOn(&b.addInstallFileWithDir(project.sdk.inputs[0], .lib, cpp.library_name).step);
        package.dependOn(&b.addFail("Sanitized builds cannot be packaged").step);
        return;
    }
    const stage = stageSdk(project, version);
    b.installDirectory(.{ .source_dir = stage, .install_dir = .prefix, .install_subdir = "" });
    const release = cpp.python(b, b.path("tools/release.py"));
    release.addArgs(&.{ "archive", "--sdk" });
    release.addDirectoryArg2(stage, .{ .make_absolute = true });
    release.addArg("--output");
    const dist = release.addOutputDirectoryArg2("dist", .{ .make_absolute = true });
    const install = b.addInstallDirectory(.{ .source_dir = dist, .install_dir = .prefix, .install_subdir = "dist" });
    package.dependOn(&install.step);
}

fn stageSdk(project: *Project, version: []const u8) Path {
    const b = project.b;
    const stage = b.addWriteFiles();
    _ = stage.addCopyFile(project.sdk.inputs[0], "lib/" ++ cpp.library_name);
    _ = stage.addCopyFile(project.sdk.inputs[1], "lib/" ++ cpp.tint_name);
    _ = stage.addCopyFile(project.sdk.tools.tint_manifest, "share/stana/stana-tint.json");
    _ = stage.addCopyDirectory(project.sdk.tools.tint_licenses, "share/stana/licenses/tint", .{});
    _ = stage.addCopyDirectory(b.path("modules"), "modules", .{ .include_extensions = &.{".cppm"} });
    _ = stage.addCopyFile(b.path("build_support/cpp.zig"), "build_support/cpp.zig");
    _ = stage.addCopyFile(b.path("build_support/sources.zig"), "build_support/sources.zig");
    for ([_][]const u8{ "toolchain.zig", "metadata.zig", "toolchain.cpp.in" }) |file|
        _ = stage.addCopyFile(b.path(b.fmt("build_support/{s}", .{file})), b.fmt("build_support/{s}", .{file}));
    _ = stage.addCopyFile(b.path(".clang-tidy"), ".clang-tidy");
    _ = stage.addCopyFile(b.path("README.md"), "share/stana/README.md");
    _ = stage.addCopyFile(b.path("build_support/installed.zig.in"), "build.zig");
    const manifest = std.mem.replaceOwned(u8, b.allocator, @embedFile("installed.zon.in"), "@VERSION@", version) catch @panic("OOM");
    _ = stage.add("build.zig.zon", manifest);
    _ = stage.addCopyFile(project.sdk.tools.info, "share/stana/build-info.json");
    return stage.getDirectory();
}

fn command(project: *Project, name: []const u8, timeout: usize) *cpp.Run {
    const b = project.b;
    const run = b.addRunArtifact(project.hostTool());
    run.addArgs(&.{ "run", b.fmt("{d}", .{timeout}) });
    run.setName(name);
    run.has_side_effects = true;
    project.runtimeEnvironment(run);
    return run;
}

fn addContracts(project: *Project, tests: *Build.Step) void {
    const b = project.b;
    const contracts = [_]struct { []const u8, []const u8 }{
        .{ "symbols.asset", "symbols/asset.cpp" },
        .{ "symbols.scene", "symbols/scene.cpp" },
        .{ "ui.frame", "ui/frame.cpp" },
        .{ "primitives", "primitives/contracts.cpp" },
        .{ "theming", "theming/contracts.cpp" },
        .{ "smoke", "smoke.cpp" },
        .{ "application", "application/contracts.cpp" },
        .{ "reactive", "reactive/contracts.cpp" },
        .{ "reactive.threadSafeWrites", "reactive/thread_safe_writes.cpp" },
        .{ "ui", "ui/contracts.cpp" },
        .{ "ui.events", "ui/events.cpp" },
        .{ "ui.textInput", "ui/text_input.cpp" },
        .{ "ui.textLabel", "ui/text_label.cpp" },
        .{ "text.layout", "text/layout.cpp" },
        .{ "text", "text/contracts.cpp" },
        .{ "tasks", "tasks/contracts.cpp" },
        .{ "presentation", "rendering/presentation.cpp" },
        .{ "incremental", "rendering/incremental.cpp" },
        .{ "tiles", "rendering/tiles.cpp" },
        .{ "cpu-canvas", "rendering/cpu_canvas.cpp" },
        .{ "gpu-values", "rendering/gpu_values.cpp" },
        .{ "shaders", "rendering/shader/compiler.cpp" },
        .{ "gpu-validation", "rendering/gpu_validation.cpp" },
        .{ "shadows", "rendering/shadows.cpp" },
        .{ "color-space", "rendering/color_space.cpp" },
        .{ "ui.shadows", "ui/shadows.cpp" },
        .{ "ui.states", "ui/states.cpp" },
    };
    for (contracts) |entry| {
        _ = contract(project, tests, entry[0], &.{b.fmt("tests/{s}", .{entry[1]})});
    }
    _ = contract(project, tests, "symbols.compiler", &.{"tests/symbols/compiler.cpp"});
    var failures = std.Io.Dir.cwd().openDir(b.graph.io, cpp.toolchain.absolute(b, b.path("tests/symbols/fail")), .{ .iterate = true }) catch @panic("Cannot open symbol fixtures");
    defer failures.close(b.graph.io);
    b.dependOnDirectoryContents(b.path("tests/symbols/fail"));
    var entries = failures.iterate();
    var count: usize = 0;
    while (entries.next(b.graph.io) catch @panic("Cannot read symbol fixtures")) |entry| {
        if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, ".cpp")) continue;
        const reject = project.compileFail(b.path(b.fmt("tests/symbols/fail/{s}", .{entry.name})));
        tests.dependOn(&reject.step);
        count += 1;
    }
    if (count == 0) @panic("No symbol compile-fail fixtures");
    var state_failures = std.Io.Dir.cwd().openDir(b.graph.io, cpp.toolchain.absolute(b, b.path("tests/ui/fail")), .{ .iterate = true }) catch @panic("Cannot open state fixtures");
    defer state_failures.close(b.graph.io);
    b.dependOnDirectoryContents(b.path("tests/ui/fail"));
    var state_entries = state_failures.iterate();
    var state_count: usize = 0;
    while (state_entries.next(b.graph.io) catch @panic("Cannot read state fixtures")) |entry| {
        if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, ".cpp")) continue;
        const reject = project.compileFailMatching(b.path(b.fmt("tests/ui/fail/{s}", .{entry.name})), "does not refer to any field");
        tests.dependOn(&reject.step);
        state_count += 1;
    }
    if (state_count == 0) @panic("No state compile-fail fixtures");

    const diagnostics = command(project, "stana.diagnostics", 30);
    cpp.addFile(diagnostics, project.executable("stana-diagnostics", &.{"tests/diagnostics/contracts.cpp"}, null));
    diagnostics.setEnvironmentVariable("STANA_TRACE_NATIVE", "1");
    diagnostics.expectStdOutEqual("");
    diagnostics.expectStdErrEqual("");
    tests.dependOn(&diagnostics.step);

    if (cpp.macos) {
        _ = contract(project, tests, "raster", &.{"tests/platform/macos/raster.mm"});
        _ = contract(project, tests, "shaders.metal", &.{"tests/platform/macos/shader_compiler.cpp"});
    }
}

fn contract(project: *Project, tests: *Build.Step, name: []const u8, files: []const []const u8) Path {
    const test_name = project.b.fmt("stana.{s}", .{name});
    const unit = project.executable(test_name, files, null);
    const run = command(project, test_name, 60);
    cpp.addFile(run, unit);
    tests.dependOn(&run.step);
    return unit;
}

fn addExamples(project: *Project, tests: *Build.Step, version: []const u8) void {
    const b = project.b;
    const examples = b.step("examples", "Build the existing Stana examples");
    const trace = b.option(bool, "trace", "Enable native rendering traces when launching examples") orelse true;
    const header = cpp.embedShader(b, "examples/canvas/field.wgsl", "examples/canvas/resource.hpp.in");

    const native = b.option(bool, "native-tests", "Enable macOS tests requiring a window server") orelse false;
    const offscreen = b.option(bool, "native-offscreen-tests", "Enable offscreen Metal tests") orelse false;
    addNativeTests(project, tests, version, native, offscreen);

    for ([_][]const u8{ "counter", "symbols", "canvas" }, [_][]const u8{ "Counter", "Symbols", "Canvas" }) |key, label| {
        const target = b.fmt("stana-{s}", .{key});
        const unit = project.executable(target, &.{b.fmt("examples/{s}/main.cpp", .{key})}, if (std.mem.eql(u8, key, "canvas")) header else null);
        if (std.mem.eql(u8, key, "canvas")) {
            const validate = command(project, "stana.canvas-example", 60);
            cpp.addFile(validate, unit);
            validate.addArg("--validate");
            tests.dependOn(&validate.step);
        }
        const run = if (cpp.macos) b.addRunArtifact(project.hostTool()) else cpp.Run.create(b, b.fmt("run {s}", .{key}));
        if (cpp.macos) run.addArg("example");
        const install = if (cpp.macos) result: {
            const app = bundle(project, unit, target, key, b.fmt("Stana {s}", .{label}), version);
            run.addDirectoryArg2(app, .{ .make_absolute = true });
            run.addArg(if (trace) "trace" else "quiet");
            break :result &b.addInstallDirectory(.{ .source_dir = app, .install_dir = .bin, .install_subdir = b.fmt("{s}.app", .{target}) }).step;
        } else result: {
            cpp.addFile(run, unit);
            break :result &b.addInstallFileWithDir(unit, .bin, b.fmt("{s}{s}", .{ target, if (cpp.windows) ".exe" else "" })).step;
        };
        examples.dependOn(install);
        if (trace) run.setEnvironmentVariable("STANA_TRACE_NATIVE", "1") else run.removeEnvironmentVariable("STANA_TRACE_NATIVE");
        project.runtimeEnvironment(run);
        b.step(b.fmt("run-{s}", .{key}), b.fmt("Launch the {s} example", .{key})).dependOn(&run.step);
    }
}

fn addNativeTests(project: *Project, tests: *Build.Step, version: []const u8, gui: bool, offscreen: bool) void {
    const b = project.b;
    const native = b.step("test-native", "Run selected macOS window and Metal contracts");
    if (!cpp.macos or (!gui and !offscreen)) {
        native.dependOn(&b.addFail("Enable -Dnative-tests=true or -Dnative-offscreen-tests=true on macOS").step);
        if (gui or offscreen) tests.dependOn(native);
        return;
    }
    var previous: ?*Build.Step = null;
    if (gui) {
        const fixture = "tests/platform/macos/window_fixture.mm";
        const windows = [_]struct { []const u8, []const u8, []const u8 }{
            .{ "window", "native-smoke", "window_smoke.cpp" },
            .{ "window-configuration", "window-configuration", "window_configuration.mm" },
            .{ "window-requests", "window-requests", "window_requests.cpp" },
            .{ "threadSafeWrites", "native-thread-safe-writes", "thread_safe_writes.cpp" },
            .{ "canvas.presentation", "canvas-qualification", "canvas_presentation.cpp" },
        };
        for (windows) |window| {
            const name = b.fmt("stana-{s}", .{window[1]});
            const exe = project.executable(name, &.{ b.fmt("tests/platform/macos/{s}", .{window[2]}), fixture }, null);
            const app = bundle(project, exe, name, window[1], name, version);
            const run = b.addRunArtifact(project.hostTool());
            run.addArg("native");
            run.setName(b.fmt("stana.native.{s}", .{window[0]}));
            project.runtimeEnvironment(run);
            run.addDirectoryArg2(app, .{ .make_absolute = true });
            run.addArg(name);
            const scratch = b.addWriteFiles();
            scratch.mode = .tmp;
            run.addDirectoryArg2(scratch.getDirectory(), .{ .make_absolute = true });
            run.has_side_effects = true;
            if (std.mem.eql(u8, window[0], "canvas.presentation")) metalValidation(run);
            if (previous) |step| run.step.dependOn(step);
            previous = &run.step;
        }
        previous = nativeExecutable(project, previous, "shared-copy", &.{"tests/platform/macos/shared_copy.mm"}, 30, false);
    }
    previous = nativeExecutable(project, previous, "textInput", &.{"tests/platform/macos/text_input.mm"}, 60, false);
    previous = nativeExecutable(project, previous, "canvas.offscreen", &.{ "tests/platform/macos/canvas_offscreen.mm", "tests/platform/macos/canvas_fixture.cpp" }, 120, false);
    previous = nativeExecutable(project, previous, "shadows", &.{ "tests/platform/macos/shadows.mm", "tests/platform/macos/shadow_fixture.cpp" }, 120, true);
    native.dependOn(previous.?);
    tests.dependOn(native);
}

fn nativeExecutable(project: *Project, previous: ?*Build.Step, name: []const u8, files: []const []const u8, timeout: usize, validation: bool) *Build.Step {
    const target = project.b.fmt("stana.native.{s}", .{name});
    const exe = project.executable(target, files, null);
    const run = command(project, target, timeout);
    cpp.addFile(run, exe);
    if (validation) metalValidation(run);
    if (previous) |step| run.step.dependOn(step);
    return &run.step;
}

fn metalValidation(run: *cpp.Run) void {
    run.setEnvironmentVariable("MTL_DEBUG_LAYER", "1");
    run.setEnvironmentVariable("MTL_SHADER_VALIDATION", "1");
}

fn bundle(project: *Project, exe: Path, target: []const u8, identifier: []const u8, name: []const u8, version: []const u8) Path {
    const b = project.b;
    const writer = b.addWriteFiles();
    const contents = b.fmt("{s}.app/Contents", .{target});
    const plist = writer.add(b.fmt("{s}/Info.plist", .{contents}), b.fmt(
        \\<?xml version="1.0" encoding="UTF-8"?>
        \\<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
        \\<plist version="1.0">
        \\<dict>
        \\    <key>CFBundleExecutable</key><string>{s}</string>
        \\    <key>CFBundleIdentifier</key><string>dev.stana.{s}</string>
        \\    <key>CFBundleName</key><string>{s}</string>
        \\    <key>CFBundlePackageType</key><string>APPL</string>
        \\    <key>CFBundleShortVersionString</key><string>{s}</string>
        \\    <key>CFBundleVersion</key><string>{s}</string>
        \\    <key>LSMinimumSystemVersion</key><string>{s}</string>
        \\    <key>NSHighResolutionCapable</key><true/>
        \\</dict>
        \\</plist>
    , .{ target, identifier, name, version, version, project.settings.deployment }));
    _ = writer.addCopyFile(exe, b.fmt("{s}/MacOS/{s}", .{ contents, target }));
    return plist.dirname().dirname();
}
