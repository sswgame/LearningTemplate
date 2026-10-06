# ==============================================================================
# @file Source/GameFramework/Kits/CMakeLists.txt
# @brief GameFramework 장르 키트 — **목록이 없다.** 키트 폴더의 매니페스트(`<키트>.module.json`, `_kind: Kit`)가 목록이다.
# @note 폴더는 `<장르 묶음>/<키트>`(서버 전용은 `<묶음>/Server/<키트>`), 타겟은 `GF_<키트>` · `GF_Server_<키트>`. 키트를 더하는 것은
#       폴더 · 매니페스트 · 한 줄 CMakeLists(`sw_addGameFrameworkKit`)를 놓는 것뿐이다. 키트 목록은 Source/GameFramework/Kits/README.md.
# ==============================================================================

# 들어가는 순서는 매니페스트 의존 순서(동점은 이름 순)다 — 서버 키트가 `sw_linkSharedKit` 으로 공유 키트 타깃을 링크하므로 공유 키트가 먼저다
# (`PythonTest_TestKitBuildOrder` 가 지킨다).
sw_getModuleDirectoriesOfKind(Kit listKitDirectory)
foreach(kitDirectory IN LISTS listKitDirectory)
	file(RELATIVE_PATH kitRelativeDirectory "${CMAKE_CURRENT_SOURCE_DIR}" "${kitDirectory}")
	add_subdirectory(${kitRelativeDirectory})
endforeach()
