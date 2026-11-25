# 패치 만들기 명령줄(`App -make-patch -pack-dir -patch-base -pack-build -patch-dir -patch-sign-key`)을 한 바퀴 돈다 — 언리얼 `-generatepatch` 처럼 기준 빌드 → 새 빌드 → 패치 → 적용 결과 비교.
#   1) 자식 역할 `PackPatchCommandLineTest.WriteInputs` 가 이전 빌드 팩 · 그 빌드 기록(1) · 새 빌드 팩 · 테스트 서명 키를 WORK_DIR 에 쓴다
#      (팩 쿠킹은 셰이더까지 굽느라 수백 초라 입력 팩은 작게 손으로 쓴다 — Source/DevTools/Cook/Tests/TestPackPatchCommandLine.cpp).
#   2) `-make-patch` 가 빌드 2 의 패치를 서명해 WORK_DIR/out 에 쓴다.
#   3) 자식 역할 `PackPatchCommandLineTest.CheckPatch` 가 패치를 이전 빌드 폴더에 얹어 런타임 적용 길(서명 확인 → 마운트)로 새 빌드의 내용과 견준다.
# 입력: APP · WORK_DIR · LOG_DIR(절대 경로).
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}" "${LOG_DIR}")

function(sw_run_make_patch_step step_name)
	execute_process(COMMAND "${APP}" ${ARGN} -unattended "-log-file=${LOG_DIR}/Pack.MakePatch.${step_name}.log" RESULT_VARIABLE exit_code)
	if(NOT exit_code EQUAL 0)
		message(FATAL_ERROR "${step_name} exited ${exit_code} - see ${LOG_DIR}/Pack.MakePatch.${step_name}.log")
	endif()
endfunction()

sw_run_make_patch_step(inputs --test --test-child-role=PackPatchCommandLineTest.WriteInputs "--test-child-arg=${WORK_DIR}")
sw_run_make_patch_step(make -make-patch "-pack-dir=${WORK_DIR}/new" "-patch-base=${WORK_DIR}/old/base.packbuild.xml" -pack-build=2 "-patch-dir=${WORK_DIR}/out"
	"-patch-sign-key=${WORK_DIR}/signing.pem")
sw_run_make_patch_step(check --test --test-child-role=PackPatchCommandLineTest.CheckPatch "--test-child-arg=${WORK_DIR}")
