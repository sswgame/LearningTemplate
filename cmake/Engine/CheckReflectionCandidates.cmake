# ==============================================================================
# @file cmake/Engine/CheckReflectionCandidates.cmake
# @brief 구성 때 반사 매크로가 없던 헤더에 반사 매크로가 생겼는지 빌드 때 확인한다(cmake -P).
# @details sw_addReflectionStep 의 자동 훑기는 구성 시점에 굳는다. 새 헤더 · 옮긴 헤더는 소스 글롭(CONFIGURE_DEPENDS)이 다시 구성하게 하지만,
#          이미 있던 헤더에 REFLECT( · ENUM( 을 더하면 구성이 다시 돌지 않아 .gen.cpp 가 없는 채로 링크에서 깨진다.
#          이 스크립트는 그런 헤더(구성 때 매크로가 없던 것)가 바뀌었을 때만 돈다 — 매크로가 생겼으면 다시 구성하라고 알리고 빌드를 세운다.
# 인자: -DCANDIDATE_LIST=<줄마다 헤더 경로> -DSTAMP=<스탬프 파일> -DTARGET_NAME=<타깃>
# ==============================================================================
file(STRINGS "${CANDIDATE_LIST}" listCandidateHeader)
set(listNewlyReflected "")
foreach(hdr IN LISTS listCandidateHeader)
	if(NOT EXISTS "${hdr}")
		continue()
	endif()
	file(STRINGS "${hdr}" listReflectLine REGEX "^[ \t]*(REFLECT\\(|ENUM\\(|REFLECT_CONTAINER\\()")
	if(listReflectLine)
		list(APPEND listNewlyReflected "${hdr}")
	endif()
endforeach()
if(listNewlyReflected)
	string(REPLACE ";" "\n  " swNewlyReflectedText "${listNewlyReflected}")
	message(FATAL_ERROR
		"[Reflection] ${TARGET_NAME}: these headers gained REFLECT/ENUM after the last configure and have no generated code yet:\n  ${swNewlyReflectedText}\n"
		"  Re-run the configure preset (cmake --preset <name>) so the reflection step picks them up.")
endif()
file(TOUCH "${STAMP}")
