# ==============================================================================
# @file Test/SmokeTest/CMakeLists.txt
# @brief Dev: MODULE DLL fill*API 로드 스모크 / Shipping: 정적 fillGameAPI 스모크
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SmokeTest — Dev는 SWGame/Editor/RHI를 링크하지 않고 의존만 (로드 스모크)
#    Shipping은 SWGame을 정적 링크
# ------------------------------------------------------------------------------
set(moduleSmokeLibs Engine RuntimeAPI TestFramework)
if(SW_SHIPPING_BUILD)
	list(APPEND moduleSmokeLibs SWGame)
endif()

file(GLOB_RECURSE SMOKETEST_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c" "*.h" "*.hpp")

add_executable(SmokeTest ${SMOKETEST_SOURCES})

target_include_directories(SmokeTest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(SmokeTest
	PRIVATE
		${moduleSmokeLibs}
		sw_global_options
)
if(sw_flag_libraries)
	target_link_libraries(SmokeTest PRIVATE ${sw_flag_libraries})
endif()

target_compile_definitions(SmokeTest PRIVATE "SW_LOG_TAG=\"Test\"")
target_precompile_headers(SmokeTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/Engine/pch.h")
set_target_properties(SmokeTest PROPERTIES FOLDER "Test")

if(NOT SW_SHIPPING_BUILD)
	target_compile_definitions(SmokeTest PRIVATE
		"SW_TEST_MODULE_DIR=\"${sw_output_directory}/Bin\""
	)
	if(TARGET SWGame)
		add_dependencies(SmokeTest SWGame)
	endif()
	if(TARGET EditorModule)
		add_dependencies(SmokeTest EditorModule)
	endif()
	if(TARGET RHI_DX11)
		add_dependencies(SmokeTest RHI_DX11)
	endif()
endif()

# ------------------------------------------------------------------------------
# 2) ctest — Bin에서 실행 (모듈 DLL 옆)
# ------------------------------------------------------------------------------
if(BUILD_TESTING)
	add_test(NAME SmokeTest COMMAND SmokeTest)
	set_tests_properties(SmokeTest PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
		LABELS "unit;module;nogpu"
		RUN_SERIAL TRUE
		TIMEOUT 90
	)
endif()
