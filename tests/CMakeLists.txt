add_test(NAME stana.namespace-policy COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/namespace_policy_test.py")

add_executable(stana-symbol-compiler symbols/compiler.cpp)
target_link_libraries(stana-symbol-compiler PRIVATE stana::stana)
stana_apply_safety(stana-symbol-compiler)
add_test(NAME stana.symbols.compiler COMMAND stana-symbol-compiler)
add_test(NAME stana.symbols.compiler-fail
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/symbols/compiler_fail.py"
		--build "${PROJECT_BINARY_DIR}"
		--root "${PROJECT_SOURCE_DIR}")
set_tests_properties(stana.symbols.compiler-fail PROPERTIES TIMEOUT 300)

add_executable(stana-symbol-asset symbols/asset.cpp)
target_link_libraries(stana-symbol-asset PRIVATE stana::stana)
stana_apply_safety(stana-symbol-asset)
add_test(NAME stana.symbols.asset COMMAND stana-symbol-asset)

add_executable(stana-symbol-scene symbols/scene.cpp)
target_link_libraries(stana-symbol-scene PRIVATE stana::stana)
stana_apply_safety(stana-symbol-scene)
add_test(NAME stana.symbols.scene COMMAND stana-symbol-scene)

add_executable(stana-ui-frame ui/frame.cpp)
target_link_libraries(stana-ui-frame PRIVATE stana::stana)
stana_apply_safety(stana-ui-frame)
add_test(NAME stana.ui.frame COMMAND stana-ui-frame)

add_executable(stana-primitive-contracts primitives/contracts.cpp)
target_link_libraries(stana-primitive-contracts PRIVATE stana::stana)
stana_apply_safety(stana-primitive-contracts)
add_test(NAME stana.primitives COMMAND stana-primitive-contracts)

add_executable(stana-smoke smoke.cpp)
target_link_libraries(stana-smoke PRIVATE stana::stana)
stana_apply_safety(stana-smoke)
set_property(SOURCE smoke.cpp APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/.clang-tidy")
add_test(NAME stana.smoke COMMAND stana-smoke)

add_executable(stana-application-contracts application/contracts.cpp)
target_link_libraries(stana-application-contracts PRIVATE stana::stana)
stana_apply_safety(stana-application-contracts)
add_test(NAME stana.application COMMAND stana-application-contracts)

add_test(NAME stana.policy
	COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/policy_test.py"
		--root "${PROJECT_SOURCE_DIR}" --build "${PROJECT_BINARY_DIR}"
		--tidy "${STANA_CLANG_TIDY}" --compiler "${CMAKE_CXX_COMPILER}")
# Analysis imports the complete public module; leave room for expanded partitions on local hosts.
set_tests_properties(stana.policy PROPERTIES TIMEOUT 360)

if(STANA_SANITIZER STREQUAL "")
	add_test(NAME stana.consumer
		COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/consumer_test.py"
			--root "${PROJECT_SOURCE_DIR}" --build "${PROJECT_BINARY_DIR}"
			--compiler "${CMAKE_CXX_COMPILER}" --config "${CMAKE_BUILD_TYPE}")
	# This gate builds both relocated-install and source consumers with mandatory analysis enabled.
	set_tests_properties(stana.consumer PROPERTIES TIMEOUT 900)
endif()

add_executable(stana-reactive reactive/contracts.cpp)
target_link_libraries(stana-reactive PRIVATE stana::stana)
stana_apply_safety(stana-reactive)
add_test(NAME stana.reactive COMMAND stana-reactive)

add_executable(stana-thread-safe-writes reactive/thread_safe_writes.cpp)
target_link_libraries(stana-thread-safe-writes PRIVATE stana::stana)
stana_apply_safety(stana-thread-safe-writes)
add_test(NAME stana.reactive.threadSafeWrites COMMAND stana-thread-safe-writes)

add_executable(stana-ui-contracts ui/contracts.cpp)
target_link_libraries(stana-ui-contracts PRIVATE stana::stana)
stana_apply_safety(stana-ui-contracts)
add_test(NAME stana.ui COMMAND stana-ui-contracts)

add_executable(stana-ui-events ui/events.cpp)
target_link_libraries(stana-ui-events PRIVATE stana::stana)
stana_apply_safety(stana-ui-events)
add_test(NAME stana.ui.events COMMAND stana-ui-events)

if(APPLE)
	add_executable(stana-native-smoke MACOSX_BUNDLE platform/macos/window_smoke.cpp platform/macos/window_fixture.mm)
    set_target_properties(stana-native-smoke PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/AppInfo.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.native-smoke"
        MACOSX_BUNDLE_BUNDLE_NAME "Stana Native Qualification"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_compile_options(stana-native-smoke PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
	target_link_libraries(stana-native-smoke PRIVATE stana::stana)
	stana_apply_safety(stana-native-smoke)
	option(STANA_NATIVE_TESTS "Run native tests requiring a logged-in window server" OFF)
	if(STANA_NATIVE_TESTS)
		add_test(NAME stana.native.window COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/launch_native.py" "$<TARGET_BUNDLE_DIR:stana-native-smoke>")
		set_tests_properties(stana.native.window PROPERTIES TIMEOUT 45 LABELS native)
		add_executable(stana-native-thread-safe-writes MACOSX_BUNDLE
			platform/macos/thread_safe_writes.cpp platform/macos/window_fixture.mm)
		set_target_properties(stana-native-thread-safe-writes PROPERTIES
			MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/AppInfo.plist.in"
			MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.native-thread-safe-writes"
			MACOSX_BUNDLE_BUNDLE_NAME "Stana Thread-Safe Writes"
			MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
			MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
		target_compile_options(stana-native-thread-safe-writes PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
		target_link_libraries(stana-native-thread-safe-writes PRIVATE stana::stana)
		stana_apply_safety(stana-native-thread-safe-writes)
		add_test(NAME stana.native.threadSafeWrites COMMAND "${Python3_EXECUTABLE}"
			"${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/launch_native.py"
			"$<TARGET_BUNDLE_DIR:stana-native-thread-safe-writes>")
		set_tests_properties(stana.native.threadSafeWrites PROPERTIES TIMEOUT 45 LABELS native RUN_SERIAL TRUE)
	endif()
endif()

add_executable(stana-text-contracts text/contracts.cpp)
target_link_libraries(stana-text-contracts PRIVATE stana::stana)
stana_apply_safety(stana-text-contracts)
add_test(NAME stana.text COMMAND stana-text-contracts)

add_executable(stana-task-contracts tasks/contracts.cpp)
target_link_libraries(stana-task-contracts PRIVATE stana::stana)
stana_apply_safety(stana-task-contracts)
add_test(NAME stana.tasks COMMAND stana-task-contracts)

add_executable(stana-presentation-contracts rendering/presentation.cpp)
target_link_libraries(stana-presentation-contracts PRIVATE stana::stana)
stana_apply_safety(stana-presentation-contracts)
add_test(NAME stana.presentation COMMAND stana-presentation-contracts)

add_executable(stana-incremental rendering/incremental.cpp)
target_link_libraries(stana-incremental PRIVATE stana::stana)
stana_apply_safety(stana-incremental)
add_test(NAME stana.incremental COMMAND stana-incremental)

add_executable(stana-tiles rendering/tiles.cpp)
target_link_libraries(stana-tiles PRIVATE stana::stana)
stana_apply_safety(stana-tiles)
add_test(NAME stana.tiles COMMAND stana-tiles)

if(APPLE)
	add_executable(stana-raster platform/macos/raster.mm)
	target_link_libraries(stana-raster PRIVATE stana::stana "-framework AppKit" "-framework CoreGraphics" "-framework CoreText")
	target_compile_options(stana-raster PRIVATE -fobjc-arc)
	stana_apply_safety(stana-raster)
	add_test(NAME stana.raster COMMAND stana-raster)
	if(STANA_NATIVE_TESTS)
		add_executable(stana-shared-copy platform/macos/shared_copy.mm)
		target_link_libraries(stana-shared-copy PRIVATE stana::stana "-framework Metal" "-framework CoreGraphics")
		target_compile_options(stana-shared-copy PRIVATE -fobjc-arc)
		stana_apply_safety(stana-shared-copy)
		add_test(NAME stana.native.shared-copy COMMAND stana-shared-copy)
		set_tests_properties(stana.native.shared-copy PROPERTIES TIMEOUT 30 LABELS native)
	endif()
endif()

add_executable(stana-cpu-canvas rendering/cpu_canvas.cpp)
target_link_libraries(stana-cpu-canvas PRIVATE stana::stana)
stana_apply_safety(stana-cpu-canvas)
add_test(NAME stana.cpu-canvas COMMAND stana-cpu-canvas)

add_executable(stana-gpu-values rendering/gpu_values.cpp)
target_link_libraries(stana-gpu-values PRIVATE stana::stana)
stana_apply_safety(stana-gpu-values)
add_test(NAME stana.gpu-values COMMAND stana-gpu-values)

if(APPLE)
	add_executable(stana-metal-shader-contracts platform/macos/shader_compiler.cpp)
	target_link_libraries(stana-metal-shader-contracts PRIVATE stana::stana)
	stana_apply_safety(stana-metal-shader-contracts)
	add_test(NAME stana.shaders.metal COMMAND stana-metal-shader-contracts)
endif()

add_executable(stana-shader-contracts rendering/shader/compiler.cpp)
target_link_libraries(stana-shader-contracts PRIVATE stana::stana)
stana_apply_safety(stana-shader-contracts)
add_test(NAME stana.shaders COMMAND stana-shader-contracts)
add_test(NAME stana.canvas-api COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/canvas_api_test.py" "${PROJECT_SOURCE_DIR}")

if(APPLE)
    option(STANA_NATIVE_OFFSCREEN_TESTS "Run canvas tests requiring a Metal device, not a GUI" OFF)
    if(STANA_NATIVE_OFFSCREEN_TESTS OR STANA_NATIVE_TESTS)
        add_executable(stana-canvas-offscreen platform/macos/canvas_offscreen.mm platform/macos/canvas_fixture.cpp)
        target_link_libraries(stana-canvas-offscreen PRIVATE stana::stana "-framework Metal" "-framework Foundation")
        target_compile_options(stana-canvas-offscreen PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
        stana_apply_safety(stana-canvas-offscreen)
        add_test(NAME stana.native.canvas.offscreen COMMAND stana-canvas-offscreen)
        set_tests_properties(stana.native.canvas.offscreen PROPERTIES TIMEOUT 120 LABELS "native;offscreen" RUN_SERIAL TRUE)
    endif()
endif()

if(APPLE AND STANA_NATIVE_TESTS)
    add_executable(stana-canvas-presentation MACOSX_BUNDLE platform/macos/canvas_presentation.cpp platform/macos/window_fixture.mm)
    set_target_properties(stana-canvas-presentation PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/AppInfo.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.stana.canvas-qualification"
        MACOSX_BUNDLE_BUNDLE_NAME "Stana Canvas Qualification"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    target_compile_options(stana-canvas-presentation PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    target_link_libraries(stana-canvas-presentation PRIVATE stana::stana)
    stana_apply_safety(stana-canvas-presentation)
    add_test(NAME stana.native.canvas.presentation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/launch_native.py" "$<TARGET_BUNDLE_DIR:stana-canvas-presentation>")
    set_tests_properties(stana.native.canvas.presentation PROPERTIES TIMEOUT 45 LABELS "native;presentation" RUN_SERIAL TRUE
        ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
    set_tests_properties(stana.native.window PROPERTIES RUN_SERIAL TRUE)
endif()

add_executable(stana-gpu-validation rendering/gpu_validation.cpp)
target_link_libraries(stana-gpu-validation PRIVATE stana::stana)
stana_apply_safety(stana-gpu-validation)
add_test(NAME stana.gpu-validation COMMAND stana-gpu-validation)

add_executable(stana-shadows rendering/shadows.cpp)
target_link_libraries(stana-shadows PRIVATE stana::stana)
stana_apply_safety(stana-shadows)
add_test(NAME stana.shadows COMMAND stana-shadows)

add_executable(stana-ui-shadows ui/shadows.cpp)
target_link_libraries(stana-ui-shadows PRIVATE stana::stana)
stana_apply_safety(stana-ui-shadows)
add_test(NAME stana.ui.shadows COMMAND stana-ui-shadows)

if(APPLE AND (STANA_NATIVE_TESTS OR STANA_NATIVE_OFFSCREEN_TESTS))
	add_executable(stana-native-shadows platform/macos/shadows.mm platform/macos/shadow_fixture.cpp)
	target_link_libraries(stana-native-shadows PRIVATE stana::stana "-framework Metal" "-framework Foundation")
	target_compile_options(stana-native-shadows PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
	stana_apply_safety(stana-native-shadows)
	add_test(NAME stana.native.shadows COMMAND stana-native-shadows)
	set_tests_properties(stana.native.shadows PROPERTIES TIMEOUT 120 LABELS "native;offscreen" RUN_SERIAL TRUE
		ENVIRONMENT "MTL_DEBUG_LAYER=1;MTL_SHADER_VALIDATION=1")
endif()
