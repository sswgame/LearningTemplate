# ==============================================================================
# @file Source/GameFramework/Kits/Storage/Server/SqlStore/CMakeLists.txt
# @brief SQL 저장 키트(서버) — 서비스 저장소(IServiceStore)의 SQL 구현 · 서버 설정 → 저장소
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_SqlStore)
if(NOT TARGET GF_Server_SqlStore)
	return()
endif()
target_link_libraries(GF_Server_SqlStore PUBLIC GF_SqlStore)
