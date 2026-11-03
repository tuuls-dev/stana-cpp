set(STANA_LLVM_VERSION "22.1.8")
set(STANA_SANITIZER "" CACHE STRING "Runtime instrumentation: empty, address, address-undefined")
set_property(CACHE STANA_SANITIZER PROPERTY STRINGS "" address address-undefined)

function(stana_validate_toolchain)
	if(APPLE AND (NOT CMAKE_OSX_DEPLOYMENT_TARGET OR
		CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "26.0"))
		message(FATAL_ERROR "Stana requires a macOS deployment target of 26.0 or newer. Reconfigure with -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 (including existing caches).")
	endif()
	if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
		NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL STANA_LLVM_VERSION)
		message(FATAL_ERROR "Stana requires upstream Clang ${STANA_LLVM_VERSION}; use the clang preset and LLVM_ROOT.")
	endif()
	if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
		message(FATAL_ERROR "Stana requires the Ninja generator for consistent module builds.")
	endif()
	execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version OUTPUT_VARIABLE ninja_version
		OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
	if(ninja_version VERSION_LESS "1.11")
		message(FATAL_ERROR "Stana requires Ninja 1.11 or newer.")
	endif()
	if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
		message(FATAL_ERROR "Select Debug or Release (use a supplied preset).")
	endif()
	if(NOT STANA_SANITIZER MATCHES "^(|address|address-undefined)$" OR
		(WIN32 AND STANA_SANITIZER STREQUAL "address-undefined"))
		message(FATAL_ERROR "Unsupported sanitizer selection for this platform.")
	endif()
	find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
	get_filename_component(llvm_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
	foreach(tool IN ITEMS clang-tidy clang-format clang-scan-deps)
		string(REPLACE "-" "_" key "${tool}")
		string(TOUPPER "${key}" key)
		find_program(STANA_${key} NAMES "${tool}" HINTS "${llvm_bin}" NO_DEFAULT_PATH REQUIRED)
		execute_process(COMMAND "${STANA_${key}}" --version OUTPUT_VARIABLE tool_version
			COMMAND_ERROR_IS_FATAL ANY)
		if(NOT tool_version MATCHES "version ${STANA_LLVM_VERSION}([^0-9.]|$)")
			message(FATAL_ERROR "${tool} must match Clang ${STANA_LLVM_VERSION}.")
		endif()
		set(STANA_${key} "${STANA_${key}}" PARENT_SCOPE)
	endforeach()
	# LLVM 22's --verify-config runs before custom check registration. Validate
	# built-ins separately, then require the custom diagnostics from a real probe.
	file(READ "${PROJECT_SOURCE_DIR}/.clang-tidy" verification_config)
	string(REPLACE "  custom-*" "" verification_config "${verification_config}")
	file(WRITE "${PROJECT_BINARY_DIR}/verify-tidy.yaml" "${verification_config}")
	execute_process(COMMAND "${STANA_CLANG_TIDY}" --experimental-custom-checks
		"--config-file=${PROJECT_BINARY_DIR}/verify-tidy.yaml" --verify-config
		RESULT_VARIABLE tidy_status OUTPUT_VARIABLE tidy_output ERROR_VARIABLE tidy_error)
	if(NOT tidy_status EQUAL 0)
		message(FATAL_ERROR "Required clang-tidy configuration unavailable:\n${tidy_output}${tidy_error}")
	endif()
	file(WRITE "${PROJECT_BINARY_DIR}/tidy-probe.cppm"
		"export module stana_toolchain_probe;\nexport auto rejected() -> int* { return new int((int)1); }\n")
	execute_process(COMMAND "${STANA_CLANG_TIDY}" --experimental-custom-checks
		"--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy" "${PROJECT_BINARY_DIR}/tidy-probe.cppm"
		-- -std=c++23
		RESULT_VARIABLE probe_status OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_error)
	if(NOT "${probe_output}${probe_error}" MATCHES "custom-no-raw-allocation" OR
		NOT "${probe_output}${probe_error}" MATCHES "custom-no-c-style-cast" OR probe_status EQUAL 0)
		message(FATAL_ERROR "Required custom AST checks are not working:\n${probe_output}${probe_error}")
	endif()
	# Reject silently ignored hardening macros, a different standard library,
	# and externally disabled RTTI/exceptions before building module interfaces.
	include(CheckCXXSourceCompiles)
	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED YES)
	set(CMAKE_CXX_EXTENSIONS NO)
	set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
	if(APPLE)
		set(CMAKE_REQUIRED_DEFINITIONS "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE")
		set(stl_contract "
#ifndef _LIBCPP_VERSION
#error macOS builds require libc++
#endif
#if !defined(_LIBCPP_HARDENING_MODE_EXTENSIVE) || _LIBCPP_HARDENING_MODE != _LIBCPP_HARDENING_MODE_EXTENSIVE
#error libc++ extensive hardening is unavailable
#endif")
	elseif(WIN32)
		set(CMAKE_REQUIRED_DEFINITIONS "-D_MSVC_STL_HARDENING=1")
		set(stl_contract "
#ifndef _MSVC_STL_VERSION
#error Windows builds require the MSVC STL
#endif
#if !defined(_MSVC_STL_HARDENING_VECTOR) || !_MSVC_STL_HARDENING_VECTOR
#error MSVC STL hardening is unavailable
#endif")
	else()
		set(CMAKE_REQUIRED_DEFINITIONS "-D_GLIBCXX_ASSERTIONS=1")
		set(stl_contract "
#if !defined(_GLIBCXX_RELEASE) || _GLIBCXX_RELEASE < 14
#error Linux builds require libstdc++ from GCC 14 or newer
#endif
#ifndef _GLIBCXX_ASSERTIONS
#error libstdc++ assertions are unavailable
#endif")
	endif()
	unset(STANA_STL_CONTRACT CACHE)
	check_cxx_source_compiles("
#include <version>
#include <vector>
${stl_contract}
#if !defined(__cpp_exceptions) || !defined(__cpp_rtti)
#error Stana requires exceptions and RTTI
#endif
int main() { return 0; }
" STANA_STL_CONTRACT)
	if(NOT STANA_STL_CONTRACT)
		message(FATAL_ERROR "The selected SDK/STL does not satisfy Stana's hardening, RTTI and exception requirements.")
	endif()
	set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)
endfunction()

function(stana_apply_safety target)
	set_target_properties(${target} PROPERTIES
		CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
		CXX_SCAN_FOR_MODULES YES
		CXX_CLANG_TIDY "${STANA_CLANG_TIDY};--experimental-custom-checks;--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")

	if(APPLE)
 set_target_properties(${target} PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES
 OBJCXX_EXTENSIONS NO
 OBJCXX_CLANG_TIDY "${STANA_CLANG_TIDY};--experimental-custom-checks;--config-file=${PROJECT_SOURCE_DIR}/.clang-tidy")
endif()

	set(warnings
		-Wall -Wextra -Wpedantic -Werror -pedantic-errors
		-Wconversion -Wsign-conversion -Wshadow-all -Wold-style-cast -Wcast-align
		-Wcast-qual -Wformat=2 -Wundef -Wdouble-promotion -Wimplicit-fallthrough
		-Wnon-virtual-dtor -Woverloaded-virtual -Wextra-semi -Wzero-as-null-pointer-constant
		-Wdeprecated -Wdeprecated-declarations -Wdeprecated-copy-dtor
		-Wnull-dereference -Wunreachable-code-aggressive -Wswitch-enum
		-Wcovered-switch-default -Wmissing-prototypes -Wmissing-noreturn
		-Wimplicit-int-conversion -Wshorten-64-to-32 -Wfloat-equal
		-Wthread-safety -Wdangling -Wunsafe-buffer-usage)
	if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
		target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc /GR /GS)
		foreach(warning IN LISTS warnings)
			target_compile_options(${target} PRIVATE "/clang:${warning}")
		endforeach()
		target_compile_definitions(${target} PUBLIC _MSVC_STL_HARDENING=1)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	else()
		target_compile_options(${target} PRIVATE ${warnings} -fstack-protector-strong)
		if(APPLE)
			target_compile_definitions(${target} PUBLIC
				"_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE")
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			target_compile_definitions(${target} PUBLIC _GLIBCXX_ASSERTIONS=1
				"$<$<CONFIG:Release>:_FORTIFY_SOURCE=3>")
		else()
			message(FATAL_ERROR "Supported platforms are Windows, macOS and Linux.")
		endif()
	endif()

	if(NOT STANA_SANITIZER STREQUAL "")
		if(STANA_SANITIZER STREQUAL "address-undefined")
			set(sanitizers address,undefined)
		else()
			set(sanitizers address)
		endif()
		if(WIN32)
			target_compile_options(${target} PUBLIC /fsanitize=address /Oy-)
			target_link_options(${target} PUBLIC /INCREMENTAL:NO)
		else()
			target_compile_options(${target} PUBLIC "-fsanitize=${sanitizers}"
				-fno-omit-frame-pointer -fno-sanitize-recover=all)
			target_link_options(${target} PUBLIC "-fsanitize=${sanitizers}" -fno-sanitize-recover=all)
		endif()
	endif()
endfunction()
