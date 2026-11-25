# ==============================================================================
# @file Test/Net/CMakeLists.txt
# @brief 여러 프로세스 네트워크 실행을 CTest 항목으로(`Net.<이름>`, 라벨 net) — 전용 서버 · 클라이언트를 띄우는 판정 명령(`Scripts/net`)마다 하나다.
#        목록이 없다 — `Scripts/net/ctest_items.py` 가 그 폴더의 모듈을 훑어 준다(모듈에 `CTEST_ITEMS` 가 없으면 구성이 멈춘다). 손으로만 돌리던 때 한 번도 통과할 수 없던
#        기대 · 로그 글이 바뀌어 지는 정규식이 쌓였다.
# @details 실제 시간으로 도는 창 · GPU 실행이라 혼자 돈다(RUN_SERIAL) — 묶음 검증의 gpu-release(Release ctest 전체)가 돌린다. 포트는 실행마다 빈 것을 받는다. 따로는 `ctest -L net`.
#          Release 하나에만 둔다: 보는 것은 프로세스 사이의 길이라 구성을 늘려도 얻는 것이 없고, ASan 은 실제 시간 시한을 넘긴다. Server 는 Game 타깃(Ninja-Release · Linux-Release)이
#          App 과 함께 짓는다 — Client · Server 타깃 구성에는 둘 중 하나가 없어 항목이 없다. 배포 서버의 기동은 shipping-check 가 본다.
# ==============================================================================

if(NOT TARGET App OR NOT TARGET Server OR NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR SW_ENABLE_SANITIZER OR SW_SHIPPING_BUILD)
	return()
endif()
get_property(list_net_game GLOBAL PROPERTY SW_GAME_PACKS)
if(NOT SW_ACTIVE_GAME STREQUAL "Playground" AND NOT "Playground" IN_LIST list_net_game)
	return()   # 모든 항목이 Playground 를 띄운다
endif()

file(GLOB list_net_script CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Scripts/net/*.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${list_net_script})
execute_process(COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_SOURCE_DIR}/Scripts/net/ctest_items.py"
	OUTPUT_VARIABLE net_item_text ERROR_VARIABLE net_item_error RESULT_VARIABLE net_item_result OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT net_item_result EQUAL 0 OR net_item_text STREQUAL "")
	message(FATAL_ERROR "[Net] Scripts/net/ctest_items.py failed (${net_item_result}): ${net_item_error}")
endif()

get_filename_component(net_preset "${CMAKE_BINARY_DIR}" NAME)   # build/<프리셋> — 실행기가 build/<프리셋>/Bin 의 실행 파일을 쓴다
string(REPLACE "\r" "" net_item_text "${net_item_text}")
string(REPLACE "\n" ";" list_net_line "${net_item_text}")
foreach(net_line IN LISTS list_net_line)
	string(REPLACE "|" ";" list_field "${net_line}")
	list(GET list_field 0 net_name)
	list(GET list_field 1 net_timeout)
	list(GET list_field 2 net_scenarios)
	list(GET list_field 3 net_arguments)
	string(REPLACE "," ";" net_scenarios "${net_scenarios}")
	string(REPLACE "," ";" net_arguments "${net_arguments}")
	sw_register_scenarios(${net_scenarios})
	add_test(NAME ${net_name}
		COMMAND "${Python3_EXECUTABLE}" -X utf8 -m Scripts ${net_arguments} --preset ${net_preset}
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
	set_tests_properties(${net_name} PROPERTIES LABELS net TIMEOUT ${net_timeout} RUN_SERIAL TRUE FIXTURES_REQUIRED ImportedAssets)
	if(SW_CPU_HEAVY_TEST)
		set_tests_properties(${net_name} PROPERTIES DEPENDS ${SW_CPU_HEAVY_TEST})
	endif()
endforeach()
