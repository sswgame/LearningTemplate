# ==============================================================================
# @file Test/ModuleSmokeTest/CMakeLists.txt
# @brief Dev MODULE DLL fill*API 로드 스모크 테스트
# ==============================================================================

if(SW_SHIPPING_BUILD)
	message(STATUS "[ModuleSmokeTest] Skipped (SW_SHIPPING_BUILD=ON)")
	return()
endif()

sw_add_executable(ModuleSmokeTest
	LINK_LIBRARIES
		Core
		RuntimeAPI
		TestFramework
)

sw_set_log_tag(ModuleSmokeTest Test)
sw_configure_pch(ModuleSmokeTest)

# LiveReload 모듈 출력 경로와 동일 (cmake/internal/Targets.cmake sw_set_module_bin_output)
target_compile_definitions(ModuleSmokeTest PRIVATE
	"SW_TEST_MODULE_DIR=\"${sw_output_directory}/Bin\""
)

if(TARGET SWGame)
	add_dependencies(ModuleSmokeTest SWGame)
endif()
if(TARGET EditorModule)
	add_dependencies(ModuleSmokeTest EditorModule)
endif()

if(BUILD_TESTING)
	add_test(NAME ModuleSmokeTest COMMAND ModuleSmokeTest)
	set_tests_properties(ModuleSmokeTest PROPERTIES
		WORKING_DIRECTORY "${sw_output_directory}/Bin"
	)
endif()
