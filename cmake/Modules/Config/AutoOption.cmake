# ==============================================================================
# @file cmake/Modules/Config/AutoOption.cmake
# @brief 구성에서 유도하는 옵션의 틀 하나 — 캐시에는 `auto`(또는 사람이 고른 값)만 두고, 푼 값은 같은 이름의 보통 변수로 가린다
# @details 유도한 값을 캐시 기본값으로 적으면 첫 구성의 값이 빌드 폴더에 굳어, 유도에 쓰인 것(대상 · 다른 옵션 · 표의 기본값)이 바뀌어도 옛 값을 쓴다.
#          프리셋(base-vcpkg · base-shipping)도 `auto` 를 적는다 — `cmake --preset` 이 옛 방식이 남긴 값과 지난번 `-D` 를 되돌린다.
#          `cmake -P` 로도 부를 수 있게 함수만 둔다(Scripts/setup/tests/test_cmake_build_rules.py).
# ==============================================================================
include_guard(GLOBAL)

# 옵션 NAME 을 캐시에 선언하고 푼다: 캐시 값이 auto(빈 값도)면 DERIVED, 아니면 캐시 값을 부른 쪽의 보통 변수 NAME 에.
#   나머지 인자는 받는 값 목록이다(없으면 부른 쪽이 검사한다).
function(sw_resolve_auto_option NAME DERIVED DOC)
	set(${NAME} "auto" CACHE STRING "${DOC}")
	set(resolved "$CACHE{${NAME}}")
	if(resolved STREQUAL "" OR resolved STREQUAL "auto")
		set(resolved "${DERIVED}")
	elseif(ARGN)
		list(FIND ARGN "${resolved}" value_index)
		if(value_index EQUAL -1)
			list(JOIN ARGN ", " value_text)
			message(FATAL_ERROR "${NAME}='${resolved}' - use auto, ${value_text}")
		endif()
	endif()
	set_property(CACHE ${NAME} PROPERTY STRINGS auto ${ARGN})
	set(${NAME} "${resolved}" PARENT_SCOPE)
endfunction()
