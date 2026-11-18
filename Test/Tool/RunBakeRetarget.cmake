# 리타깃 베이크(`App --bake-retarget=<프로필>,<원본 클립>,<출력 클립>`)를 한 번 돈다 — 커밋된 프로필 knight_to_minion 으로 기사 클립 하나를 미니언 스켈레톤에 굽는다.
# 클립은 임포트 산출물(`common/models/_shared/clips/<해시>.animclip`)이라 이름이 고정되지 않는다 — 기사 애님셋(임포트가 쓴다, 고정 단계 `Import.models` 뒤)의 첫 클립을 쓴다.
# 판정: 종료 0, 출력 클립 파일이 비어 있지 않다.
# 입력: APP · CONTENT_DIR · OUT_DIR · LOG_DIR(절대 경로).
set(profile "common/rigs/knight_to_minion.retarget.xml")
set(animset "${CONTENT_DIR}/common/models/kaykit/knight/knight.animset.xml")
set(out_clip "${OUT_DIR}/knight_on_minion.animclip")
set(log_path "${LOG_DIR}/Tool.BakeRetarget.log")
file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}" "${LOG_DIR}")
if(NOT EXISTS "${animset}")
	message(FATAL_ERROR "${animset} is missing - the model import (Import.models) writes it")
endif()
file(READ "${animset}" animset_text)
if(NOT animset_text MATCHES "path=\"([^\"]+\.animclip)\"")
	message(FATAL_ERROR "${animset} names no .animclip")
endif()
set(source_clip "${CMAKE_MATCH_1}")

execute_process(COMMAND "${APP}" "--bake-retarget=${profile},${source_clip},${out_clip}" -unattended "-log-file=${log_path}" RESULT_VARIABLE exit_code)
if(NOT exit_code EQUAL 0)
	message(FATAL_ERROR "--bake-retarget exited ${exit_code} - see ${log_path}")
endif()
if(EXISTS "${out_clip}")
	file(SIZE "${out_clip}" out_size)
endif()
if(NOT EXISTS "${out_clip}" OR out_size EQUAL 0)
	message(FATAL_ERROR "--bake-retarget did not write a clip to ${out_clip}")
endif()
