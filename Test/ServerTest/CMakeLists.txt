# ==============================================================================
# @file Test/ServerTest/CMakeLists.txt
# @brief 전용 서버 실행 파일(Server)을 실제로 띄운다 — 창 · GPU 없이 기동 · 틱 · 정상 종료(nogpu — CI 가 두 플랫폼에서 돈다).
#        Server 가 없는 타깃(Client)은 짓지 않는다.
# ==============================================================================
if(NOT TARGET Server)
	sw_declareUnbuiltDirectory("${CMAKE_CURRENT_SOURCE_DIR}")
	return()
endif()

sw_addTestExecutable(ServerTest
	LIBS
		Engine
	LABELS
		"unit;server;nogpu"
	TIMEOUT
		180
)
# 시험은 빌드된 Server 를 띄운다 — 옆에 서 있어야 한다(Dev 는 게임 모듈도).
add_dependencies(ServerTest Server)
if(TARGET SWGame)
	add_dependencies(ServerTest SWGame)
endif()
