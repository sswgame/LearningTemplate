# 립싱크 임포트(`App --import-lipsync`)를 한 번 돈다 — 음성(`<도메인>/…/voice/*.wav|ogg`)마다 비즘 트랙(`.visemes.xml`)을 곁에 쓴다.
# 임포트는 리소스 루트(소스 트리 `Content/`)에 쓰므로 커밋된 트랙을 먼저 BACKUP_DIR 에 두고 끝나면 되돌린다 — 테스트가 소스 트리를 바꾸지 않는다.
# 분석 값의 바이트(부동소수)는 구성 · 플랫폼마다 흔들릴 수 있어 견주지 않는다. 판정: 종료 0, 로그의 `Imported N viseme tracks (0 failures)` 에서 N ≥ 1.
# 입력: APP · CONTENT_DIR · BACKUP_DIR · LOG_DIR(절대 경로).
set(log_path "${LOG_DIR}/Tool.ImportLipSync.log")
file(REMOVE_RECURSE "${BACKUP_DIR}")
file(MAKE_DIRECTORY "${BACKUP_DIR}" "${LOG_DIR}")
file(GLOB_RECURSE list_committed_track RELATIVE "${CONTENT_DIR}" "${CONTENT_DIR}/*.visemes.xml")
foreach(track IN LISTS list_committed_track)
	get_filename_component(track_dir "${BACKUP_DIR}/${track}" DIRECTORY)
	file(MAKE_DIRECTORY "${track_dir}")
	file(COPY_FILE "${CONTENT_DIR}/${track}" "${BACKUP_DIR}/${track}")
endforeach()

execute_process(COMMAND "${APP}" --import-lipsync -unattended "-log-file=${log_path}" RESULT_VARIABLE exit_code)

# 되돌리기 — 커밋된 트랙은 원래 바이트로, 임포트가 새로 만든 트랙(커밋 안 된 음성의 것)은 지운다.
foreach(track IN LISTS list_committed_track)
	file(COPY_FILE "${BACKUP_DIR}/${track}" "${CONTENT_DIR}/${track}")
endforeach()
file(GLOB_RECURSE list_track_after RELATIVE "${CONTENT_DIR}" "${CONTENT_DIR}/*.visemes.xml")
foreach(track IN LISTS list_track_after)
	if(NOT track IN_LIST list_committed_track)
		file(REMOVE "${CONTENT_DIR}/${track}")
	endif()
endforeach()

if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "--import-lipsync exited ${exit_code} - see ${log_path}")
endif()
file(STRINGS "${log_path}" list_summary_line REGEX "Imported [0-9]+ viseme tracks \(0 failures\)")
if(NOT list_summary_line MATCHES "Imported ([0-9]+) viseme tracks" OR CMAKE_MATCH_1 EQUAL 0)
	message(FATAL_ERROR "--import-lipsync did not report 'Imported N viseme tracks (0 failures)' with N >= 1 - see ${log_path}")
endif()
