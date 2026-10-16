# ==============================================================================
# @file cmake/Engine/WriteModuleBuildStamp.cmake
# @brief 모듈 빌드 성공 스탬프를 씁니다(`cmake -DSTAMP_PATH=<경로> -P` — `sw_addModuleBuildStamp` 가 부른다).
# ==============================================================================
# `cmake -E touch` 를 쓰지 않는다 — 있는 파일의 시각만 바꾸면 리눅스 inotify 는 IN_ATTRIB 만 내고, 핫 리로드 감시(`LinuxFileWatcher`)는 그것을
# 받지 않는다. 내용을 다시 쓰면 두 플랫폼 모두 쓰기 이벤트가 난다(Windows LAST_WRITE · 리눅스 IN_MODIFY / IN_CLOSE_WRITE).
if(NOT STAMP_PATH)
	message(FATAL_ERROR "[ModuleBuildStamp] STAMP_PATH is required")
endif()
string(TIMESTAMP swNow "%Y-%m-%dT%H:%M:%S.%fZ" UTC)
file(WRITE "${STAMP_PATH}" "${swNow}\n")
