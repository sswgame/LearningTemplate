# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Storage/SQLStore/Server/CMakeLists.txt
# @brief SQL 저장 키트(서버) — 서비스 저장소(IServiceStore)의 SQL 구현 · PostgreSQL 드라이버 · 서버 설정 → 저장소
# ==============================================================================

sw_addGameFrameworkKit(GF_Server_SQLStore)
if(NOT TARGET GF_Server_SQLStore)
	return()
endif()
sw_linkSharedKit(GF_Server_SQLStore GF_SQLStore)
# libpq-fe.h 는 Driver/Postgres 에서만 include 한다(CheckThirdPartyIsolation.py) — 링크도 이 파일에서만.
find_package(PostgreSQL REQUIRED)
target_link_libraries(GF_Server_SQLStore PRIVATE PostgreSQL::PostgreSQL)
