# 립싱크 임포트(`App --import-lipsync -lipsync-dir=<OUT_DIR>`)를 한 번 돈다 — 음성(`<도메인>/…/voice/*.wav|ogg`)마다 비즘 트랙(`.visemes.xml`)을 OUT_DIR 아래
# 같은 리소스 상대 경로에 쓴다. 출력 폴더를 받으므로 소스 트리(커밋된 트랙)를 건드리지 않는다 — 테스트가 중간에 죽어도 트리가 더러워지지 않는다.
# 분석 값의 바이트(부동소수)는 구성 · 플랫폼마다 흔들릴 수 있어 견주지 않는다. 판정: 종료 0, 로그의 `Imported N viseme tracks (0 failures)` 에서 N ≥ 1,
# OUT_DIR 에 트랙이 N 개.
# 입력: APP · OUT_DIR · LOG_DIR(절대 경로).
set(log_path "${LOG_DIR}/Tool.ImportLipSync.log")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}" "${LOG_DIR}")
execute_process(COMMAND "${APP}" --import-lipsync "-lipsync-dir=${OUT_DIR}" -unattended "-log-file=${log_path}" RESULT_VARIABLE exit_code)
if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "--import-lipsync exited ${exit_code} - see ${log_path}")
endif()
file(STRINGS "${log_path}" list_summary_line REGEX "Imported [0-9]+ viseme tracks \(0 failures\)")
if(NOT list_summary_line MATCHES "Imported ([0-9]+) viseme tracks" OR CMAKE_MATCH_1 EQUAL 0)
	message(FATAL_ERROR "--import-lipsync did not report 'Imported N viseme tracks (0 failures)' with N >= 1 - see ${log_path}")
endif()
set(reported_count "${CMAKE_MATCH_1}")
file(GLOB_RECURSE list_track "${OUT_DIR}/*.visemes.xml")
list(LENGTH list_track track_count)
if(NOT track_count EQUAL reported_count)
	message(FATAL_ERROR "--import-lipsync reported ${reported_count} tracks but ${OUT_DIR} holds ${track_count} - the output folder was not used for every track")
endif()
