# 개발 구성의 쿠킹본 실행(`-cooked-content`)을 한 바퀴 돈다 — 배포 읽기 경로(팩만 · 미리 구운 셰이더 · 이진 번역 표)를 배포 빌드 없이 돌려 본다.
#   1) `--cook-all` 이 게임 하나(Empty) · 백엔드 하나(-dx12)의 스테이징과 팩을 굽는다. 스테이징은 상대 경로(`../Cooked`)로 넘긴다 — `..` 가 남은 스테이징 경로를
#      포함 판정이 밖으로 봐 셰이더 바이너리 · 번역 표가 팩에서 통째로 빠지던 결함(명령줄 경로의 `..` 를 풀지 않았다 — `AbsolutePath::tryResolve`)을 이 길이 잡는다.
#      팩은 실행 파일 옆(`Bin/Packs`) 하나다 — 쿠킹본 실행이 그 자리만 읽는다(`ProcessRoots::_packs`). 스테이징은 CookAssets 와 같은 `<빌드>/Cooked` 라 바뀐 것만 다시 굽는다.
#   2) 구운 팩으로 기동해 셀 스트리밍 시나리오를 돈다(셀은 쿠킹본에만 있다 — 소스 콘텐츠로는 스트리밍을 지나지 않는다).
# 입력: APP · BIN_DIR · LOG_DIR(절대 경로) · SKIP_CODE(시나리오의 건너뜀 종료 코드 — 그대로 돌려준다).
file(MAKE_DIRECTORY "${LOG_DIR}")

function(sw_run_cooked_content_step step_name)
	execute_process(COMMAND "${APP}" ${ARGN} -unattended "-log-file=${LOG_DIR}/Pack.CookedContent.${step_name}.log" WORKING_DIRECTORY "${BIN_DIR}" RESULT_VARIABLE exit_code)
	if(NOT exit_code EQUAL 0)
		message(STATUS "${step_name} exited ${exit_code} - see ${LOG_DIR}/Pack.CookedContent.${step_name}.log")
		if(exit_code EQUAL SKIP_CODE)
			cmake_language(EXIT ${SKIP_CODE})
		endif()
		message(FATAL_ERROR "${step_name} failed")
	endif()
endfunction()

sw_run_cooked_content_step(cook --cook-all --cooked-dir=../Cooked --pack-dir=Packs --game=Empty -dx12)
sw_run_cooked_content_step(run -scenario=game/empty/automation/load/streamingcells.scenario.xml -cooked-content -dx12)
