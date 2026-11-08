cmake_minimum_required(VERSION 4.4)
project(stana VERSION 0.1.0 DESCRIPTION "A strict, portable C++23 module library" LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/Safety.cmake)
stana_validate_toolchain()
include(cmake/Dependencies.cmake)
stana_find_icu()
find_package(Threads REQUIRED)
include(cmake/ShaderCompiler.cmake)

if(STANA_ENABLE_CHECKS)
	add_custom_target(stana-source-policy ALL
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/policy.py"
				"${CMAKE_CURRENT_SOURCE_DIR}"
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/namespace_policy.py"
				"${CMAKE_CURRENT_SOURCE_DIR}"
		COMMENT "Checking Stana source and namespace policy" VERBATIM)
endif()

add_library(stana STATIC)
add_library(stana::stana ALIAS stana)
target_sources(stana
	PUBLIC FILE_SET modules TYPE CXX_MODULES BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
		FILES modules/stana.cppm modules/primitives/quantities.cppm modules/primitives/primitives.cppm modules/symbol/symbol.cppm modules/symbol/units.cppm modules/symbol/asset.cppm modules/symbol/playback.cppm modules/symbol/geometry.cppm modules/symbol/compiler_detail.cppm modules/symbol/compiler.cppm modules/reactive/reactive.cppm modules/tasks/tasks.cppm modules/rendering/shadow.cppm modules/rendering/rendering.cppm modules/rendering/presentation.cppm modules/rendering/program.cppm modules/rendering/gpu.cppm modules/document/document.cppm modules/layout/layout.cppm modules/rendering/diagnostics.cppm modules/ui/ui.cppm modules/testing/testing.cppm modules/application/window_types.cppm modules/application/window_requests.cppm modules/application/window_events.cppm modules/application/platform_macos.cppm modules/application/window_options.cppm modules/application/application.cppm modules/text/text.cppm
	PRIVATE src/rendering/shadow.cpp src/gpu/shader/compiler.cpp src/rendering/drawing.cpp src/rendering/gpu.cpp src/rendering/gpu_program.cpp src/version.cpp src/document/document.cpp src/document/input.cpp src/layout/layout.cpp src/rendering/scene/scene.cpp src/rendering/scene/geometry.cpp src/platform/tiles.cpp src/platform/scheduler.cpp src/ui/mount.cpp src/application/application.cpp src/application/window_configuration.cpp src/application/window_session.cpp src/text/text.cpp src/reactive/executor.cpp src/symbol/playback.cpp)
target_compile_features(stana PUBLIC cxx_std_23)
target_link_libraries(stana PUBLIC Threads::Threads PRIVATE ICU::uc ICU::i18n ICU::data stana::tint)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(_stana_tint_include "/imsvc${STANA_TINT_ROOT}/include")
else()
    set(_stana_tint_include "-isystem${STANA_TINT_ROOT}/include")
endif()
set_property(SOURCE src/gpu/shader/compiler.cpp src/platform/macos/shader_compiler.cpp APPEND PROPERTY COMPILE_OPTIONS "${_stana_tint_include}")
set_source_files_properties(src/gpu/shader/compiler.cpp src/platform/macos/shader_compiler.cpp PROPERTIES
    COMPILE_DEFINITIONS "DAWN_HAVE_UTILS_BUILD_CONFIG;DAWN_ENABLE_ASSERTS")
set_target_properties(stana PROPERTIES
	CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
	CXX_SCAN_FOR_MODULES YES POSITION_INDEPENDENT_CODE YES)
stana_apply_safety(stana)
if(STANA_ENABLE_CHECKS)
	set_property(SOURCE modules/stana.cppm src/version.cpp APPEND PROPERTY OBJECT_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
	add_dependencies(stana stana-source-policy)
endif()

if(APPLE)
	target_compile_options(stana PUBLIC "$<BUILD_INTERFACE:-nostdinc++>")
	target_include_directories(stana SYSTEM PUBLIC "$<BUILD_INTERFACE:${CMAKE_OSX_SYSROOT}/usr/include/c++/v1>")
	set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
	enable_language(OBJCXX)
	add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/stana_shader.hpp"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_shader.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/macos/shaders/stana.metal"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/stana_shader.hpp"
        DEPENDS tools/embed_shader.py src/platform/macos/shaders/stana.metal VERBATIM)
    set_property(SOURCE src/platform/macos/gpu_resources.mm APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_sources(stana PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/stana_shader.hpp"
        src/platform/macos/shadows.mm src/platform/macos/shader_compiler.cpp src/platform/macos/compositor.mm src/platform/macos/gpu_resources.mm src/platform/macos/gpu_programs.mm src/platform/macos/native_host.mm src/platform/macos/window_configuration.mm src/platform/macos/renderer.mm src/platform/macos/presentation.mm src/platform/macos/symbol_paths.mm)
	target_compile_options(stana PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
	target_link_libraries(stana PRIVATE "-framework AppKit" "-framework CoreGraphics"
		"-framework CoreText" "-framework Metal" "-framework QuartzCore")
else()
	target_sources(stana PRIVATE src/platform/unsupported.cpp)
endif()

if(PROJECT_IS_TOP_LEVEL)
	add_executable(stana-counter examples/counter/main.cpp)
	if(APPLE)
		set_target_properties(stana-counter PROPERTIES MACOSX_BUNDLE YES
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppInfo.plist.in"
			MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.counter"
			MACOSX_BUNDLE_BUNDLE_NAME "Stana Counter"
			MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
	endif()
	target_link_libraries(stana-counter PRIVATE stana::stana)
	stana_apply_safety(stana-counter)
	add_executable(stana-symbols examples/symbols/main.cpp)
	if(APPLE)
		set_target_properties(stana-symbols PROPERTIES MACOSX_BUNDLE YES
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppInfo.plist.in"
			MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.symbols"
			MACOSX_BUNDLE_BUNDLE_NAME "Stana Symbols"
			MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
	endif()
	target_link_libraries(stana-symbols PRIVATE stana::stana)
	stana_apply_safety(stana-symbols)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/examples/canvas/field.wgsl" stana_canvas_wgsl)
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS examples/canvas/field.wgsl)
	configure_file(examples/canvas/resource.hpp.in generated/canvas_resource.hpp @ONLY)
	add_executable(stana-canvas examples/canvas/main.cpp)
	target_include_directories(stana-canvas PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
	target_link_libraries(stana-canvas PRIVATE stana::stana)
	stana_apply_safety(stana-canvas)
	if(APPLE)
		set_target_properties(stana-canvas PROPERTIES MACOSX_BUNDLE YES
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppInfo.plist.in"
			MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.canvas"
			MACOSX_BUNDLE_BUNDLE_NAME "Stana Canvas"
			MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
	endif()
endif()


if(PROJECT_IS_TOP_LEVEL)
	enable_testing()
	add_subdirectory(tests)
	add_test(NAME stana.canvas-example COMMAND stana-canvas --validate)
	file(GLOB_RECURSE stana_format_sources CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/modules/*.cppm"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.mm" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp")
	add_custom_target(format-check
		COMMAND "${STANA_CLANG_FORMAT}" --dry-run --Werror ${stana_format_sources}
		VERBATIM)
endif()

if(STANA_SANITIZER STREQUAL "")
	install(TARGETS stana EXPORT stanaTargets
		ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
		FILE_SET modules DESTINATION "${CMAKE_INSTALL_DATADIR}/stana/modules")
	install(EXPORT stanaTargets NAMESPACE stana::
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stana" CXX_MODULES_DIRECTORY modules)
	configure_package_config_file(cmake/stanaConfig.cmake.in
		"${CMAKE_CURRENT_BINARY_DIR}/stanaConfig.cmake"
		INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stana")
	write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/stanaConfigVersion.cmake"
		VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion)
	install(FILES cmake/Dependencies.cmake DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stana")
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/stanaConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/stanaConfigVersion.cmake"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stana")
	configure_file(cmake/build-info.json.in "${CMAKE_CURRENT_BINARY_DIR}/build-info.json" @ONLY)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/build-info.json" README.md
		DESTINATION "${CMAKE_INSTALL_DATADIR}/stana")
endif()
