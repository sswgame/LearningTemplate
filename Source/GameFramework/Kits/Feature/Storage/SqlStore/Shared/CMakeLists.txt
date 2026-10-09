# ==============================================================================
# @file Source/GameFramework/Kits/Feature/Storage/SqlStore/Shared/CMakeLists.txt
# @brief SQL 저장 키트(공유) — SQL 드라이버 계약 · 연결 풀 · 드라이버 등록부 · 마이그레이션 적용기 · SQLite 드라이버
# ==============================================================================

sw_addGameFrameworkKit(GF_SqlStore)
if(NOT TARGET GF_SqlStore)
	return()
endif()
# sqlite3.h 는 Driver/Sqlite 에서만 include 한다(CheckThirdPartyIsolation.py) — 링크도 이 파일에서만.
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(GF_SqlStore PRIVATE unofficial::sqlite3::sqlite3)
