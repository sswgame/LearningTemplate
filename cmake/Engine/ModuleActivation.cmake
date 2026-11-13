# ==============================================================================
# @file cmake/Engine/ModuleActivation.cmake
# @brief 모듈 켜짐 조회 — 모듈을 만드는 함수가 꺼진 모듈을 건너뛰고, 테스트가 꺼진 키트를 쓰는 소스를 뺀다(답은 ModuleManifest.cmake 의 해석 결과)
# ==============================================================================

# 모듈 NAME 이 이 구성에서 켜져 있으면 OUT_VAR 를 ON 으로 둔다. 매니페스트가 없는 모듈은 구성을 세운다(모든 동적 모듈은 매니페스트를 갖는다).
function(sw_is_module_active NAME OUT_VAR)
	get_property(swNames GLOBAL PROPERTY SW_MODULE_NAMES)

	if(NOT NAME IN_LIST swNames)
		message(FATAL_ERROR "[Module] '${NAME}' has no manifest — add ${NAME}.module.json next to its CMakeLists.txt")
	endif()

	get_property(swActive GLOBAL PROPERTY SW_MODULE_${NAME}_ACTIVE)
	set(${OUT_VAR} ${swActive} PARENT_SCOPE)
endfunction()

# 꺼진 모듈 NAME 의 폴더를 "이 구성이 짓지 않는다" 로 적고 OUT_VAR 를 ON 으로 둔다(켜져 있으면 OFF). 모듈 타깃을 만드는 함수의 첫 줄에서 부른다.
function(sw_skip_inactive_module NAME OUT_VAR)
	sw_is_module_active(${NAME} swActive)

	if(swActive)
		set(${OUT_VAR} OFF PARENT_SCOPE)
		return()
	endif()

	get_property(swReason GLOBAL PROPERTY SW_MODULE_${NAME}_REASON)
	message(STATUS "[Module] ${NAME} is not built — ${swReason}")
	sw_declare_excluded_directory("${CMAKE_CURRENT_SOURCE_DIR}")
	set(${OUT_VAR} ON PARENT_SCOPE)
endfunction()

