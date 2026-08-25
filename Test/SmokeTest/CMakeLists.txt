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

sw_addExecutable(SmokeTest
	NO_INSTALL
	LINK_LIBRARIES
		${moduleSmokeLibs}
)

sw_setLogTag(SmokeTest Test)
sw_configurePch(SmokeTest)

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
	)
endif()
