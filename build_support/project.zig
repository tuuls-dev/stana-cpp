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
    project.verifyModules(project.policy);
    b.getInstallStep().dependOn(project.policy);

    const format = b.step("format", "Format first-party C++ and Objective-C++");
    const format_check = b.step("format-check", "Check first-party C++ and Objective-C++ formatting");
    for ([_]*Build.Step{ format, format_check }, [_][]const u8{ "--write", "--check" }) |step, option| {
        const formatter = cpp.python(b, b.path("tools/format.py"));
        formatter.addArg(option);
        step.dependOn(&formatter.step);
    }

    const compdb = cpp.python(b, project.sdk.script);
    compdb.addArg("compdb");
    const database = compdb.addOutputFileArg2("compile_commands.json", .{ .make_absolute = true });
    for (project.records.items) |record| cpp.addFile(compdb, record);
    const update = b.addUpdateSourceFiles();
    update.addCopyFileToSource(database, "compile_commands.json");
    b.step("compdb", "Write compile_commands.json for clangd").dependOn(&update.step);

    const package = b.step("package", "Run the tests and package a relocatable SDK");
    if (project.sanitizer != .none) {
        b.getInstallStep().dependOn(&b.addInstallFileWithDir(project.sdk.inputs[0], .lib, cpp.library_name).step);
        package.dependOn(&b.addFail("Sanitized builds cannot be packaged").step);
        return;
    }
    const stage = stageSdk(project, version);
    b.installDirectory(.{ .source_dir = stage, .install_dir = .prefix, .install_subdir = "" });
    if (project.analysis_config == null) {
        package.dependOn(&b.addFail("Packaging requires source analysis").step);
        return;
    }
    const release = cpp.python(b, b.path("tools/release.py"));
    release.step.dependOn(tests);
    release.step.dependOn(project.policy);
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
    _ = stage.addCopyFile(b.graph.cwdRelativePath(project.tools.tint_manifest), "share/stana/stana-tint.json");
    _ = stage.addCopyDirectory(b.graph.cwdRelativePath(project.tools.tint_licenses), "share/stana/licenses/tint", .{});
    _ = stage.addCopyDirectory(b.path("modules"), "modules", .{ .include_extensions = &.{".cppm"} });
    _ = stage.addCopyFile(b.path("build_support/cpp.zig"), "build_support/cpp.zig");
    _ = stage.addCopyFile(b.path("build_support/sources.zig"), "build_support/sources.zig");
    _ = stage.addCopyFile(b.path("tools/stana_build.py"), "tools/stana_build.py");
    _ = stage.addCopyFile(b.path(".clang-tidy"), ".clang-tidy");
    _ = stage.addCopyFile(b.path("README.md"), "share/stana/README.md");
    _ = stage.addCopyFile(b.path("build_support/installed.zig.in"), "build.zig");
    const manifest = std.mem.replaceOwned(u8, b.allocator, @embedFile("installed.zon.in"), "@VERSION@", version) catch @panic("OOM");
    _ = stage.add("build.zig.zon", manifest);
    _ = stage.add("share/stana/build-info.json", b.fmt("{{\"name\":\"stana\",\"version\":\"{s}\",{s}\n", .{ version, project.tools.metadata[1..] }));
    return stage.getDirectory();
}

fn command(project: *Project, name: []const u8, timeout: usize) *cpp.Run {
    const b = project.b;
    const run = cpp.python(b, project.sdk.script);
    run.addArgs(&.{ "run", b.fmt("{d}", .{timeout}) });
    run.setName(name);
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
        .{ "smoke", "smoke.cpp" },
        .{ "application", "application/contracts.cpp" },
        .{ "reactive", "reactive/contracts.cpp" },
        .{ "reactive.threadSafeWrites", "reactive/thread_safe_writes.cpp" },
        .{ "ui", "ui/contracts.cpp" },
        .{ "ui.events", "ui/events.cpp" },
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
        .{ "ui.shadows", "ui/shadows.cpp" },
    };
    for (contracts) |entry| {
        _ = contract(project, tests, entry[0], &.{b.fmt("tests/{s}", .{entry[1]})});
    }
    const symbol = contract(project, tests, "symbols.compiler", &.{"tests/symbols/compiler.cpp"});
    const symbol_fail = command(project, "stana.symbols.compiler-fail", 300);
    symbol_fail.addArg(cpp.pythonName());
    cpp.addFile(symbol_fail, b.path("tests/symbols/compiler_fail.py"));
    symbol_fail.addArg("--command");
    cpp.addFile(symbol_fail, symbol.record);
    symbol_fail.addArg("--root");
    symbol_fail.addDirectoryArg2(b.path("."), .{ .make_absolute = true });
    tests.dependOn(&symbol_fail.step);

    const diagnostics = command(project, "stana.diagnostics", 30);
    diagnostics.addArg(cpp.pythonName());
    cpp.addFile(diagnostics, b.path("tests/diagnostics/silent.py"));
    cpp.addFile(diagnostics, project.executable("stana-diagnostics", &.{"tests/diagnostics/contracts.cpp"}, null).object);
    diagnostics.setEnvironmentVariable("STANA_TRACE_NATIVE", "1");
    tests.dependOn(&diagnostics.step);

    const namespace = command(project, "stana.namespace-policy", 30);
    namespace.addArg(cpp.pythonName());
    cpp.addFile(namespace, b.path("tests/namespace_policy_test.py"));
    tests.dependOn(&namespace.step);
    const canvas = command(project, "stana.canvas-api", 30);
    canvas.addArg(cpp.pythonName());
    cpp.addFile(canvas, b.path("tests/canvas_api_test.py"));
    canvas.addDirectoryArg2(b.path("."), .{ .make_absolute = true });
    tests.dependOn(&canvas.step);

    if (cpp.macos) {
        _ = contract(project, tests, "raster", &.{"tests/platform/macos/raster.mm"});
        _ = contract(project, tests, "shaders.metal", &.{"tests/platform/macos/shader_compiler.cpp"});
    }
}

fn contract(project: *Project, tests: *Build.Step, name: []const u8, files: []const []const u8) cpp.Unit {
    const test_name = project.b.fmt("stana.{s}", .{name});
    const unit = project.executable(test_name, files, null);
    const run = command(project, test_name, 60);
    cpp.addFile(run, unit.object);
    tests.dependOn(&run.step);
    return unit;
}

fn addExamples(project: *Project, tests: *Build.Step, version: []const u8) void {
    const b = project.b;
    const examples = b.step("examples", "Build the existing Stana examples");
    const trace = b.option(bool, "trace", "Enable native rendering traces when launching examples") orelse true;
    const embed = cpp.python(b, b.path("tools/embed_shader.py"));
    cpp.addFile(embed, b.path("examples/canvas/field.wgsl"));
    const header = embed.addOutputFileArg2("canvas_resource.hpp", .{ .make_absolute = true });
    embed.addArg("--canvas-template");
    cpp.addFile(embed, b.path("examples/canvas/resource.hpp.in"));

    const native = b.option(bool, "native-tests", "Enable macOS tests requiring a window server") orelse false;
    const offscreen = b.option(bool, "native-offscreen-tests", "Enable offscreen Metal tests") orelse false;
    addNativeTests(project, tests, version, native, offscreen);

    for ([_][]const u8{ "counter", "symbols", "canvas" }, [_][]const u8{ "Counter", "Symbols", "Canvas" }) |key, label| {
        const target = b.fmt("stana-{s}", .{key});
        const unit = project.executable(target, &.{b.fmt("examples/{s}/main.cpp", .{key})}, if (std.mem.eql(u8, key, "canvas")) header else null);
        if (std.mem.eql(u8, key, "canvas")) {
            const validate = command(project, "stana.canvas-example", 60);
            cpp.addFile(validate, unit.object);
            validate.addArg("--validate");
            tests.dependOn(&validate.step);
        }
        const run = cpp.python(b, b.path("tools/run_example.py"));
        const install = if (cpp.macos) result: {
            const app = bundle(project, unit.object, target, key, b.fmt("Stana {s}", .{label}), version);
            run.addDirectoryArg2(app, .{ .make_absolute = true });
            break :result &b.addInstallDirectory(.{ .source_dir = app, .install_dir = .bin, .install_subdir = b.fmt("{s}.app", .{target}) }).step;
        } else result: {
            cpp.addFile(run, unit.object);
            break :result &b.addInstallFileWithDir(unit.object, .bin, b.fmt("{s}{s}", .{ target, if (cpp.windows) ".exe" else "" })).step;
        };
        examples.dependOn(install);
        run.addArg(if (trace) "--trace" else "--no-trace");
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
            const app = bundle(project, exe.object, name, window[1], name, version);
            const run = command(project, b.fmt("stana.native.{s}", .{window[0]}), 45);
            run.addArg(cpp.pythonName());
            cpp.addFile(run, b.path("tests/platform/macos/launch_native.py"));
            run.addDirectoryArg2(app, .{ .make_absolute = true });
            if (std.mem.eql(u8, window[0], "canvas.presentation")) metalValidation(run);
            if (previous) |step| run.step.dependOn(step);
            previous = &run.step;
        }
        previous = nativeExecutable(project, previous, "shared-copy", &.{"tests/platform/macos/shared_copy.mm"}, 30, false);
    }
    previous = nativeExecutable(project, previous, "canvas.offscreen", &.{ "tests/platform/macos/canvas_offscreen.mm", "tests/platform/macos/canvas_fixture.cpp" }, 120, false);
    previous = nativeExecutable(project, previous, "shadows", &.{ "tests/platform/macos/shadows.mm", "tests/platform/macos/shadow_fixture.cpp" }, 120, true);
    native.dependOn(previous.?);
    tests.dependOn(native);
}

fn nativeExecutable(project: *Project, previous: ?*Build.Step, name: []const u8, files: []const []const u8, timeout: usize, validation: bool) *Build.Step {
    const target = project.b.fmt("stana.native.{s}", .{name});
    const exe = project.executable(target, files, null);
    const run = command(project, target, timeout);
    cpp.addFile(run, exe.object);
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
    , .{ target, identifier, name, version, version, project.deployment }));
    _ = writer.addCopyFile(exe, b.fmt("{s}/MacOS/{s}", .{ contents, target }));
    return plist.dirname().dirname();
}
