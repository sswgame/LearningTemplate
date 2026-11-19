# 셰이더 데우기 — 요청 기록(`*.shaderrequests`, `ShaderRequestLog`)만 컴파일하는 길(`App --cook-shaders -shader-request-dir=`)이 셰이더 DDC 를 채워, 같은 요청의 다음 컴파일이
# 캐시에 맞는지 본다. 묶음 검증은 지난 시나리오 체인의 기록으로 이 길을 cpu 단계에서 돌려 GPU 단계가 캐시 미스 컴파일을 떠안지 않게 한다(Scripts/dev/preflight.sh 의 shader warm).
# 이 테스트의 DDC(DDC_DIR — `-shader-ddc-dir`)와 기록 폴더(REQUEST_DIR)는 비우고 시작한다 — 빌드 폴더가 함께 쓰는 DDC 는 건드리지 않는다.
# 첫 실행은 기록의 요청을 캐시 미스로 컴파일하고(`ShaderCompiler.CompiledOnCacheMiss`), 둘째 실행은 같은 기록을 한 번도 컴파일하지 않아야 한다(모두 DDC 적중).
# 소스가 없어진 요청을 건너뛰는 규칙은 단위 테스트(`ShaderRequestLogTest`)가 본다 — 여기서 경고 줄을 내지 않는다.
# 입력: APP · DDC_DIR · REQUEST_DIR · LOG_DIR(모두 절대 경로).
file(REMOVE_RECURSE "${DDC_DIR}" "${REQUEST_DIR}")
# 머리 줄 + 요청 한 줄(형식 폴더 · 스테이지 태그 · 소스 · 진입점 — 탭으로 가른다). 형식은 ShaderRequestLog.h 의 설명이다.
file(WRITE "${REQUEST_DIR}/warm.shaderrequests" "SW shader request log 1\ndx12\tcs\tengine/shaders/gpu_nan_check.hlsl\tCSMain\n")
foreach(pass first second)
	set(log_path "${LOG_DIR}/ShaderCache.warm.${pass}.log")
	execute_process(
		COMMAND "${APP}" --cook-shaders "-shader-request-dir=${REQUEST_DIR}" "-shader-ddc-dir=${DDC_DIR}" -unattended "-log-file=${log_path}"
		RESULT_VARIABLE exit_code)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "the ${pass} shader warm-up exited ${exit_code} - see ${log_path}")
	endif()
	file(STRINGS "${log_path}" list_compiled_line REGEX "ShaderCompiler\\.CompiledOnCacheMiss")
	file(STRINGS "${log_path}" list_warmed_line REGEX "ShaderRequestLog\\.Warmed")
	if(NOT list_warmed_line)
		message(FATAL_ERROR "the ${pass} shader warm-up did not report its summary (ShaderRequestLog.Warmed) - see ${log_path}")
	endif()
	if(pass STREQUAL "first" AND NOT list_compiled_line)
		message(FATAL_ERROR "the first shader warm-up compiled nothing although its shader cache was empty - see ${log_path}")
	endif()
	if(pass STREQUAL "second" AND list_compiled_line)
		message(FATAL_ERROR "the second shader warm-up compiled again - the first did not fill the shader cache for the same request: ${list_compiled_line}")
	endif()
endforeach()
