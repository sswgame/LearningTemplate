# 작업 폴더 둘(실행 파일 폴더 BIN_DIR · 빈 폴더 OTHER_DIR)에서 같은 자리를 가리키는 상대 경로 인자로 App 을 띄워, 산출물이 같은 자리(OUT_DIR)에 생기는지 본다(R5).
# 상대 경로 인자는 기동 때의 작업 폴더로 한 번 절대화하고, 루트(`ProcessRoots` — Content · Saved)는 실행 파일 위치로 정한다. 그래서
#   1) 두 실행의 보고 · 로그가 모두 OUT_DIR 에 있고, 2) 빈 작업 폴더 OTHER_DIR 에는 아무것도 생기지 않는다(작업 폴더에 `Saved/` 를 만들던 결함).
# 입력: APP · BIN_DIR · OTHER_DIR · OUT_DIR(절대 경로) · FILTER(--test-filter 값).
file(REMOVE_RECURSE "${OUT_DIR}" "${OTHER_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}" "${OTHER_DIR}")
foreach(run bin other)
	if(run STREQUAL "bin")
		set(working_dir "${BIN_DIR}")
	else()
		set(working_dir "${OTHER_DIR}")
	endif()
	file(RELATIVE_PATH relative_out "${working_dir}" "${OUT_DIR}")
	execute_process(
		COMMAND "${APP}" --test -unattended "--test-filter=${FILTER}" "--test-report=${relative_out}/report.${run}.json" "-log-file=${relative_out}/${run}.log"
		WORKING_DIRECTORY "${working_dir}"
		RESULT_VARIABLE exit_code)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "run from ${working_dir} exited ${exit_code} - see ${OUT_DIR}/${run}.log")
	endif()
	foreach(output "report.${run}.json" "${run}.log")
		if(NOT EXISTS "${OUT_DIR}/${output}")
			message(FATAL_ERROR "run from ${working_dir} did not write ${output} under ${OUT_DIR} (relative argument '${relative_out}/${output}')")
		endif()
	endforeach()
endforeach()
file(GLOB_RECURSE list_stray LIST_DIRECTORIES true "${OTHER_DIR}/*")
if(list_stray)
	message(FATAL_ERROR "the working folder ${OTHER_DIR} got files - roots must not follow the working folder:\n${list_stray}")
endif()
