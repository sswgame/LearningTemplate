# ==============================================================================
# @file Test/SmokeTest/CMakeLists.txt
# @brief Dev: MODULE DLL fill*API 로드 스모크 / Shipping: 정적 fillGameAPI 스모크
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) SmokeTest 타겟 정의 및 의존성 구성
# ------------------------------------------------------------------------------
set(smokeLibs Engine RuntimeAPI)
if(SW_SHIPPING_BUILD)
	list(APPEND smokeLibs SWGame)
endif()

sw_addTestExecutable(SmokeTest
	RUN_SERIAL
	SOURCES
		TestSmoke.cpp
		${CMAKE_SOURCE_DIR}/Source/App/Module/ModuleCompiler.cpp
	LIBS
		${smokeLibs}
	LABELS
		"unit;module;nogpu"
	TIMEOUT
		180
)

target_include_directories(SmokeTest PRIVATE "${CMAKE_SOURCE_DIR}/Source/App")

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
