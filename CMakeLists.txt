cmake_minimum_required(VERSION 4.4)
project(stana VERSION 0.1.0 DESCRIPTION "A strict, portable C++23 module library" LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/Safety.cmake)
stana_validate_toolchain()
include(cmake/Dependencies.cmake)
stana_find_icu()
include(cmake/ShaderCompiler.cmake)

add_custom_target(stana-source-policy ALL
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/policy.py"
					"${CMAKE_CURRENT_SOURCE_DIR}"
	COMMENT "Checking Stana source policy" VERBATIM)

add_library(stana STATIC)
add_library(stana::stana ALIAS stana)
target_sources(stana
	PUBLIC FILE_SET modules TYPE CXX_MODULES BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules"
		FILES modules/stana.cppm modules/primitives/primitives.cppm modules/reactive/reactive.cppm modules/tasks/tasks.cppm modules/graphics/graphics.cppm modules/graphics/presentation.cppm modules/graphics/program.cppm modules/graphics/gpu.cppm modules/core/core.cppm modules/ui/ui.cppm modules/testing/testing.cppm modules/application/application.cppm modules/text/text.cppm
	PRIVATE src/graphics/shader_compiler.cpp src/graphics/drawing.cpp src/graphics/gpu.cpp src/graphics/gpu_program.cpp src/version.cpp src/core/document.cpp src/core/input.cpp src/layout/layout.cpp src/scene/scene.cpp src/scene/geometry.cpp src/platform/tiles.cpp src/platform/scheduler.cpp src/ui/mount.cpp src/application/application.cpp src/text/text.cpp)
target_compile_features(stana PUBLIC cxx_std_23)
target_link_libraries(stana PRIVATE ICU::uc ICU::i18n ICU::data stana::tint)
target_include_directories(stana SYSTEM PRIVATE "${STANA_TINT_ROOT}/include")
set_source_files_properties(src/graphics/shader_compiler.cpp PROPERTIES
    COMPILE_DEFINITIONS "DAWN_HAVE_UTILS_BUILD_CONFIG;DAWN_ENABLE_ASSERTS")
set_target_properties(stana PROPERTIES
	CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
	CXX_SCAN_FOR_MODULES YES POSITION_INDEPENDENT_CODE YES)
stana_apply_safety(stana)
set_property(SOURCE modules/stana.cppm src/version.cpp APPEND PROPERTY OBJECT_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
add_dependencies(stana stana-source-policy)

if(APPLE)
	target_compile_options(stana PUBLIC "$<BUILD_INTERFACE:-nostdinc++>")
	target_include_directories(stana SYSTEM PUBLIC "$<BUILD_INTERFACE:${CMAKE_OSX_SYSROOT}/usr/include/c++/v1>")
	set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
	enable_language(OBJCXX)
	target_sources(stana PRIVATE src/platform/macos/native_host.mm src/platform/macos/renderer.mm src/platform/macos/presentation.mm)
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
endif()


if(PROJECT_IS_TOP_LEVEL)
	enable_testing()
	add_subdirectory(tests)
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
