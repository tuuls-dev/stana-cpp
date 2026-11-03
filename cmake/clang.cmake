# Set LLVM_ROOT to a matching upstream LLVM distribution. Never replace system tools.
if(CMAKE_HOST_WIN32)
	set(_stana_driver clang-cl)
else()
	set(_stana_driver clang++)
endif()
find_program(_stana_compiler NAMES "${_stana_driver}"
	HINTS "$ENV{LLVM_ROOT}/bin" /opt/homebrew/opt/llvm@22/bin /usr/local/opt/llvm@22/bin
	REQUIRED)
set(CMAKE_CXX_COMPILER "${_stana_compiler}" CACHE FILEPATH "Stana Clang compiler")
if(CMAKE_HOST_APPLE)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version")
	if(NOT CMAKE_OSX_SYSROOT)
		execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _stana_sdk
			OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
		set(CMAKE_OSX_SYSROOT "${_stana_sdk}" CACHE PATH "macOS SDK" FORCE)
	endif()
endif()

if(CMAKE_HOST_APPLE)
	# LLVM's bundled libc++ headers may require runtime symbols absent from Apple's
	# system libc++. Select the SDK headers together with the system runtime.
	set(CMAKE_CXX_FLAGS_INIT "-nostdinc++ -isystem ${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
	set(CMAKE_OBJCXX_FLAGS_INIT "-nostdinc++ -isystem ${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
endif()
