# ==============================================================================
# @file Test/PythonTest/CMakeLists.txt
# @brief 파이썬 도구의 단위 시험(nogpu)
# ==============================================================================

# ------------------------------------------------------------------------------
# 1) 단위 시험 — 이 폴더의 `Test*.py` 하나 = CTest 항목 하나(`PythonTest_<이름>`)
#
# 목록을 적지 않는다. 파일을 놓으면 그것이 시험이다(린트 게이트와 같은 규칙). 시험은 표준 `unittest` 만 쓰고
# 저장소 루트에서 돈다 — GPU · 빌드 산출물이 필요 없어 CI 가 도는 `nogpu` 다.
# ------------------------------------------------------------------------------
file(GLOB listPythonTestScript CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Test*.py")
foreach(scriptPath IN LISTS listPythonTestScript)
	get_filename_component(scriptName "${scriptPath}" NAME_WE)
	add_test(NAME PythonTest_${scriptName} COMMAND "${Python3_EXECUTABLE}" "${scriptPath}")
	set_tests_properties(PythonTest_${scriptName} PROPERTIES
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
		LABELS "nogpu;unit;python"
		TIMEOUT 120
	)
endforeach()
