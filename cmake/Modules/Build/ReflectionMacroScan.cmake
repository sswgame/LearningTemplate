# ==============================================================================
# @file cmake/Modules/Build/ReflectionMacroScan.cmake
# @brief 헤더에 반사 매크로가 있는지 — 구성 때 훑기와 빌드 때 후보 검사(`cmake -P`)가 같은 판정을 쓴다
# ==============================================================================

function(sw_has_reflection_macro OUT_VAR HEADER)
	file(STRINGS "${HEADER}" list_reflect_line REGEX "^[ \t]*(REFLECT\\(|ENUM\\(|REFLECT_CONTAINER\\()")
	if(list_reflect_line)
		set(${OUT_VAR} ON PARENT_SCOPE)
	else()
		set(${OUT_VAR} OFF PARENT_SCOPE)
	endif()
endfunction()
