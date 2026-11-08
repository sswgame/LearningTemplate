# 셰이더 캐시 두 폴더 — 셰이더 DDC(`<Bin>/Saved/ShaderCompileCache`)와 쿠킹 스테이징(COOKED_DIR) — 를 지우고 굽은 뒤 판정한다.
# 캐시가 비어도 굽기가 소스에서 다시 만들고(`App --cook-shaders`, 종료 코드 = `ShaderCookSummary::isClean`), 만든 바이너리가 소스와 맞는지
# 판정(`App --verify-shaders`)이 통과해야 한다. 지운 두 폴더가 다시 채워졌는지도 본다(캐시를 지운 채 다른 곳에 쓰면 다음 실행이 매번 다시 굽는다).
# 입력: APP · DDC_DIR · COOKED_DIR · LOG_DIR(모두 절대 경로).
file(REMOVE_RECURSE "${DDC_DIR}" "${COOKED_DIR}")
foreach(stage cook verify)
	execute_process(
		COMMAND "${APP}" --${stage}-shaders "--cooked-dir=${COOKED_DIR}" -unattended "-log-file=${LOG_DIR}/ShaderCache.${stage}.log"
		RESULT_VARIABLE exitCode)
	if(NOT exitCode EQUAL 0)
		message(FATAL_ERROR "--${stage}-shaders exited ${exitCode} after both shader caches were removed - see ${LOG_DIR}/ShaderCache.${stage}.log")
	endif()
endforeach()
foreach(folder "${DDC_DIR}" "${COOKED_DIR}")
	file(GLOB_RECURSE listFile "${folder}/*")
	if(NOT listFile)
		message(FATAL_ERROR "the shader cache folder ${folder} is still empty after cooking")
	endif()
endforeach()
