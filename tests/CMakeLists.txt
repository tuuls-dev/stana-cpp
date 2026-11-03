add_executable(stana-smoke smoke.cpp)
target_link_libraries(stana-smoke PRIVATE stana::stana)
stana_apply_safety(stana-smoke)
set_property(SOURCE smoke.cpp APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/.clang-tidy")
add_test(NAME stana.smoke COMMAND stana-smoke)

add_test(NAME stana.policy
	COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/policy_test.py"
		--root "${PROJECT_SOURCE_DIR}" --build "${PROJECT_BINARY_DIR}"
		--tidy "${STANA_CLANG_TIDY}" --compiler "${CMAKE_CXX_COMPILER}")
set_tests_properties(stana.policy PROPERTIES TIMEOUT 180)

if(STANA_SANITIZER STREQUAL "")
	add_test(NAME stana.consumer
		COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/consumer_test.py"
			--root "${PROJECT_SOURCE_DIR}" --build "${PROJECT_BINARY_DIR}"
			--compiler "${CMAKE_CXX_COMPILER}" --config "${CMAKE_BUILD_TYPE}")
	set_tests_properties(stana.consumer PROPERTIES TIMEOUT 180)
endif()

add_executable(stana-reactive reactive/contracts.cpp)
target_link_libraries(stana-reactive PRIVATE stana::stana)
stana_apply_safety(stana-reactive)
add_test(NAME stana.reactive COMMAND stana-reactive)

add_executable(stana-ui-contracts ui/contracts.cpp)
target_link_libraries(stana-ui-contracts PRIVATE stana::stana)
stana_apply_safety(stana-ui-contracts)
add_test(NAME stana.ui COMMAND stana-ui-contracts)

if(APPLE)
	add_executable(stana-native-smoke platform/macos/window_smoke.cpp)
	target_link_libraries(stana-native-smoke PRIVATE stana::stana)
	stana_apply_safety(stana-native-smoke)
	option(STANA_NATIVE_TESTS "Run native tests requiring a logged-in window server" OFF)
	if(STANA_NATIVE_TESTS)
		add_test(NAME stana.native.window COMMAND stana-native-smoke)
		set_tests_properties(stana.native.window PROPERTIES TIMEOUT 15 LABELS native)
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

add_executable(stana-presentation-contracts graphics/presentation.cpp)
target_link_libraries(stana-presentation-contracts PRIVATE stana::stana)
stana_apply_safety(stana-presentation-contracts)
add_test(NAME stana.presentation COMMAND stana-presentation-contracts)
