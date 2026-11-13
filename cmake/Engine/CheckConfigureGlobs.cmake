# ==============================================================================
# @file cmake/Engine/CheckConfigureGlobs.cmake
# @brief `cmake -P` — 빌드마다 다시 훑는 글롭(VerifyGlobs.cmake) 가운데 뿌리(`Source/` · `Test/` · 시나리오 폴더) 아래를
#        `sw_glob_source_tree` 밖에서 훑는 것이 있으면 멈춘다(`sw_check_configure_globs` 가 부른다, 까닭은 SourceTree.cmake 머리말).
# 인자: -DVERIFY_GLOBS=<VerifyGlobs.cmake> -DROOTS=<뿌리|뿌리…> -DSOURCE_DIR=<저장소> -DSTAMP=<스탬프>
# ==============================================================================
cmake_minimum_required(VERSION 3.25)

string(REPLACE "|" ";" listRoot "${ROOTS}")
file(STRINGS "${VERIFY_GLOBS}" listLine REGEX "^(# .* at .*\\(file\\)|file\\(GLOB)")
set(site "")
set(listViolation "")
foreach(line IN LISTS listLine)
	if(line MATCHES "^# [^ ]+ at (.+):[0-9]+ \\(file\\)")
		set(site "${CMAKE_MATCH_1}")
		set(siteLine "${line}")
		continue()
	endif()
	if(NOT line MATCHES "\"([^\"]+)\"\\)[ \t\r]*$")
		continue()
	endif()
	set(pattern "${CMAKE_MATCH_1}")
	if(site STREQUAL "cmake/Engine/SourceTree.cmake")
		continue()
	endif()
	foreach(root IN LISTS listRoot)
		string(FIND "${pattern}" "${root}/" rootPosition)
		if(rootPosition EQUAL 0)
			string(REGEX REPLACE "^# [^ ]+ at " "" siteText "${siteLine}")
			string(REGEX REPLACE " \\(file\\)[ \t\r]*$" "" siteText "${siteText}")
			list(APPEND listViolation "${siteText}: ${pattern}")
			break()
		endif()
	endforeach()
endforeach()

if(listViolation)
	list(REMOVE_DUPLICATES listViolation)
	string(REPLACE ";" "\n  " violationText "${listViolation}")
	message(FATAL_ERROR
		"[SourceTree] these file(GLOB … CONFIGURE_DEPENDS) calls walk a source tree root on every build:\n  ${violationText}\n"
		"  Use sw_glob_source_tree (cmake/Engine/SourceTree.cmake) — it filters the one walk of each root.")
endif()
file(TOUCH "${STAMP}")
