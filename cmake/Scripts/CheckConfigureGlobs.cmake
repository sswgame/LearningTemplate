# ==============================================================================
# @file cmake/Scripts/CheckConfigureGlobs.cmake
# @brief `cmake -P` — 빌드마다 다시 훑는 글롭(VerifyGlobs.cmake) 가운데 뿌리(`Source/` · `Test/` · 시나리오 폴더) 아래를
#        `sw_glob_source_tree` 밖에서 훑는 것이 있으면 멈춘다(`sw_check_configure_globs` 가 부른다, 까닭은 SourceTree.cmake 머리말).
# 인자: -DVERIFY_GLOBS=<VerifyGlobs.cmake> -DROOTS=<뿌리|뿌리…> -DSOURCE_DIR=<저장소> -DSTAMP=<스탬프>
# ==============================================================================
cmake_minimum_required(VERSION 3.25)

string(REPLACE "|" ";" list_root "${ROOTS}")
file(STRINGS "${VERIFY_GLOBS}" list_line REGEX "^(# .* at .*\\(file\\)|file\\(GLOB)")
set(site "")
set(list_violation "")
foreach(line IN LISTS list_line)
	if(line MATCHES "^# [^ ]+ at (.+):[0-9]+ \\(file\\)")
		set(site "${CMAKE_MATCH_1}")
		set(site_line "${line}")
		continue()
	endif()
	if(NOT line MATCHES "\"([^\"]+)\"\\)[ \t\r]*$")
		continue()
	endif()
	set(pattern "${CMAKE_MATCH_1}")
	if(site STREQUAL "cmake/Modules/Build/SourceTree.cmake")
		continue()
	endif()
	foreach(root IN LISTS list_root)
		string(FIND "${pattern}" "${root}/" root_position)
		if(root_position EQUAL 0)
			string(REGEX REPLACE "^# [^ ]+ at " "" site_text "${site_line}")
			string(REGEX REPLACE " \\(file\\)[ \t\r]*$" "" site_text "${site_text}")
			list(APPEND list_violation "${site_text}: ${pattern}")
			break()
		endif()
	endforeach()
endforeach()

if(list_violation)
	list(REMOVE_DUPLICATES list_violation)
	string(REPLACE ";" "\n  " violation_text "${list_violation}")
	message(FATAL_ERROR
		"[SourceTree] these file(GLOB … CONFIGURE_DEPENDS) calls walk a source tree root on every build:\n  ${violation_text}\n"
		"  Use sw_glob_source_tree (cmake/Modules/Build/SourceTree.cmake) — it filters the one walk of each root.")
endif()
file(TOUCH "${STAMP}")
