# ==============================================================================
# @file cmake/Modules/Build/NinjaBuildLogs.cmake
# @brief 지다 만 ninja 재압축을 구성 시작에서 마저 끝낸다 — 손 빌드 · IDE 빌드 · preflight 가 모두 구성을 지나므로 여기 한 곳이다.
# @note ninja 1.12 의 재압축(DepsLog · BuildLog Recompact — 구성 끝의 `ninja -t recompact`, 빌드 시작의 자동 재압축)은 새 파일 <이름>.recompact 를 다 쓰고 닫은 뒤
#       옛 파일을 지우고 이름을 바꾼다. 그 사이 바깥 프로세스(백신 검사)가 새 파일을 잡으면 "failed recompaction: Permission denied" 로 져 옛 파일만 사라지고,
#       다음 구성 · 빌드가 빈 기록으로 시작해 전체 재빌드가 난다(2026-11-21 ASan). .recompact 는 옛 것을 지우기 전에 이미 닫힌 완성본이다.
#       빌드 시작의 재압축이 지면 다음 빌드는 구성을 거치지 않는다 — 남은 .recompact 를 CONFIGURE_DEPENDS 로 훑어 그 빌드가 먼저 다시 구성하게 하고, ninja 는 구성 뒤
#       기록을 다시 읽는다. `cmake -P` 로도 부를 수 있게 include 때는 함수만 둔다(Scripts/setup/tests/test_ninja_build_logs.py).
# ==============================================================================
include_guard(GLOBAL)

# 빌드 폴더 BINARY_DIR 의 지다 만 재압축을 끝낸다.
function(sw_restore_ninja_build_logs BINARY_DIR)
	foreach(name IN ITEMS .ninja_deps .ninja_log)
		set(log "${BINARY_DIR}/${name}")
		if(NOT EXISTS "${log}.recompact")
			continue()
		endif()
		if(NOT EXISTS "${log}")
			file(RENAME "${log}.recompact" "${log}")
			message(STATUS "[Ninja] ${name}: finished an interrupted recompaction")
			continue()
		endif()
		# 빌드 안의 재구성이면 이 빌드가 이미 새 .ninja_log 를 열어 몇 줄 적었다(잡혀 있어 갈아 끼울 수 없다) — 완성본이 더 크면 그 줄들을 뒤에 잇는다(글 기록이라 줄마다 독립이다).
		file(SIZE "${log}" log_size)
		file(SIZE "${log}.recompact" recompact_size)
		if(name STREQUAL ".ninja_log" AND recompact_size GREATER log_size)
			file(READ "${log}.recompact" recompact_text)
			file(APPEND "${log}" "${recompact_text}")
			message(STATUS "[Ninja] ${name}: merged an interrupted recompaction")
		endif()
		file(REMOVE "${log}.recompact") # 남기면 다음에 또 지다 만 것을 목록 바뀜으로 알아보지 못한다
	endforeach()
endfunction()

if(CMAKE_GENERATOR MATCHES "Ninja")
	sw_restore_ninja_build_logs("${CMAKE_BINARY_DIR}")
	file(GLOB interrupted_recompactions CONFIGURE_DEPENDS "${CMAKE_BINARY_DIR}/.ninja_*.recompact") # 목록은 쓰지 않는다 — 빌드마다 훑어 새로 생기면 다시 구성한다
	unset(interrupted_recompactions)
endif()
