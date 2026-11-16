# 두 번 기동 쌍 하나를 돈다 — 빈 상태 폴더(STATE_DIR)로 <PAIR>.1 을 돌려 끝낼 때 상태를 쓰게 하고, 같은 폴더로 <PAIR>.2 를 띄워 돌아왔는지 본다.
# 입력: APP · PAIR(Content 기준 경로, `.1.scenario.xml` 앞까지) · STATE_DIR · STATE_ARG(상태 자리를 가리키는 App 인자 — 편집기 `-gv_editorStateDir=…` · 게임 `-gv_userSettingsFile=…`)
#       · USER_DATA_DIR(두 단계가 함께 쓰는 사용자 데이터 뿌리 — `<Bin>/Saved/Automation/UserData/` 아래여야 한다, App 이 밖이면 거절한다 `AutomationUserDataRoot`)
#       · EDITOR(ON 이면 `-EnableEditor`) · GAME_ARG(활성 게임이 아니면 `-game=<게임>`, 아니면 비움) · LOG_NAME · LOG_DIR · SKIP_CODE. 앞 단계가 지면(건너뜀 포함) 뒤는 돌지 않고 그 코드로 끝낸다.
if(NOT USER_DATA_DIR)
	message(FATAL_ERROR "RunStateDirPair: USER_DATA_DIR is required - both stages share one user data root (saves carried from .1 to .2)")
endif()
file(REMOVE_RECURSE "${STATE_DIR}" "${USER_DATA_DIR}")
file(MAKE_DIRECTORY "${STATE_DIR}")
# 세이브 · 로컬 저장을 묶음 단위로 잇는다(velmora carry · release) — 프로세스마다 새 뿌리(`run_p<pid>`)면 .2 가 .1 의 세이브를 못 본다. 자식 App 이 이 환경을 물려받는다.
set(ENV{SW_USER_DATA_ROOT} "${USER_DATA_DIR}")
get_filename_component(pair_name "${PAIR}" NAME)
set(list_editor_argument "")
if(EDITOR)
	set(list_editor_argument -EnableEditor)
endif()
foreach(stage 1 2)
	execute_process(
		COMMAND "${APP}" ${GAME_ARG} ${list_editor_argument} "${STATE_ARG}" "-scenario=${PAIR}.${stage}.scenario.xml" -unattended
			"-log-file=${LOG_DIR}/${LOG_NAME}.${stage}.log"
		RESULT_VARIABLE exit_code)
	if(NOT exit_code EQUAL 0)
		message(STATUS "${pair_name}.${stage} exited ${exit_code} - see ${LOG_DIR}/${LOG_NAME}.${stage}.log")
		if(exit_code EQUAL SKIP_CODE)
			cmake_language(EXIT ${SKIP_CODE})
		endif()
		message(FATAL_ERROR "${pair_name}.${stage} failed")
	endif()
endforeach()
