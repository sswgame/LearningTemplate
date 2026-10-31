# 두 번 기동 쌍 하나를 돈다 — 빈 상태 폴더(STATE_DIR)로 <PAIR>.1 을 돌려 끝낼 때 상태를 쓰게 하고, 같은 폴더로 <PAIR>.2 를 띄워 돌아왔는지 본다.
# 입력: APP · PAIR(Content 기준 경로, `.1.scenario.xml` 앞까지) · STATE_DIR · STATE_ARG(상태 자리를 가리키는 App 인자 — 편집기 `-gv_editorStateDir=…` · 게임 `-gv_userSettingsFile=…`)
#       · EDITOR(ON 이면 `-EnableEditor`) · GAME_ARG(활성 게임이 아니면 `-game=<게임>`, 아니면 비움) · LOG_NAME · LOG_DIR · SKIP_CODE. 앞 단계가 지면(건너뜀 포함) 뒤는 돌지 않고 그 코드로 끝낸다.
file(REMOVE_RECURSE "${STATE_DIR}")
file(MAKE_DIRECTORY "${STATE_DIR}")
get_filename_component(pairName "${PAIR}" NAME)
set(listEditorArgument "")
if(EDITOR)
	set(listEditorArgument -EnableEditor)
endif()
foreach(stage 1 2)
	execute_process(
		COMMAND "${APP}" ${GAME_ARG} ${listEditorArgument} "${STATE_ARG}" "-scenario=${PAIR}.${stage}.scenario.xml" -unattended
			"-log-file=${LOG_DIR}/${LOG_NAME}.${stage}.log"
		RESULT_VARIABLE exitCode)
	if(NOT exitCode EQUAL 0)
		message(STATUS "${pairName}.${stage} exited ${exitCode} - see ${LOG_DIR}/${LOG_NAME}.${stage}.log")
		if(exitCode EQUAL SKIP_CODE)
			cmake_language(EXIT ${SKIP_CODE})
		endif()
		message(FATAL_ERROR "${pairName}.${stage} failed")
	endif()
endforeach()
