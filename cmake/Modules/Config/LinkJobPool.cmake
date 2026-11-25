# ==============================================================================
# @file cmake/Modules/Config/LinkJobPool.cmake
# @brief Ninja 링크 풀 크기 — ThinLTO 링크인지에 따라 가른다(BuildOptions.cmake 5 가 부른다)
# @details `cmake -P` 로도 부를 수 있게 함수만 둔다(Scripts/setup/tests/test_cmake_build_rules.py).
# ==============================================================================
include_guard(GLOBAL)

# CPU_COUNT(논리 코어) · LTO(이 구성이 ThinLTO 로 링크하는가)로 동시 링크 수를 OUT_VAR 에.
#   LTO   : 코어 수 / 4, 2 ~ 4 — ThinLTO 백엔드는 링크마다 코드 생성 스레드를 띄워 메모리가 링크 수에 곱으로 는다(BuildLayout.cmake 가 링크당 스레드를 코어 수 / 풀 크기로 나눈다).
#   그 밖 : 코어 수 / 2, 2 ~ 8 — 개발 구성은 LTO 가 없어 링크가 가볍다. 엔진 헤더를 고치면 모듈 80 여 개가 다시 링크되는데 넷씩만 돌았다.
#           shortcut: 상한 8 은 링크 하나의 메모리(Engine.dll 이 가장 크다)로 잡은 값이다, 링크 구간 겹침을 `.ninja_log` 로 잰 뒤 고친다.
function(sw_link_job_count CPU_COUNT LTO OUT_VAR)
	if(LTO)
		math(EXPR link_jobs "${CPU_COUNT} / 4")
		set(link_jobs_max 4)
	else()
		math(EXPR link_jobs "${CPU_COUNT} / 2")
		set(link_jobs_max 8)
	endif()
	if(link_jobs LESS 2)
		set(link_jobs 2)
	elseif(link_jobs GREATER link_jobs_max)
		set(link_jobs ${link_jobs_max})
	endif()
	set(${OUT_VAR} ${link_jobs} PARENT_SCOPE)
endfunction()
