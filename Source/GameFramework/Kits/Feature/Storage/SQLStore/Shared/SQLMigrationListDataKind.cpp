#include "pch.h"

#include "Engine/Resource/ResourceDataKind.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SQLMigrationListDataKindInternal
        {
            /** @brief 목록이 가리키는 방언입니다. 방언 분기 파일(`NNNN_이름.<드라이버>.sql`)이 있는 드라이버를 모두 본다. */
            static constexpr const utf8* kArrDriverName[] = { "sqlite", "postgres" };

            /**
             * @brief 마이그레이션 목록(`<폴더>/migrations.json`)을 실행기가 읽는 그대로 읽습니다. 드라이버마다 목록의 모든 `.sql` 이 있고 번호가 이어져야 합니다.
             * @details 서버는 기동 때 이 목록으로 스키마를 올린다 — 목록이 틀리면 스키마 테스트에서 먼저 드러나게 한다.
             */
            [[nodiscard]] static bool loadList( const string& resourceID )
            {
                const size_t slash = resourceID.rfind( '/' );
                if ( slash == string::npos )
                    return false;
                const string_view folder( resourceID.data(), slash );
                for ( const utf8* pDriverName : kArrDriverName )
                {
                    vector<SQLMigration> listMigration;
                    string               error;
                    if ( SQLMigrationRunner::loadMigrations( folder, pDriverName, listMigration, error ) == false )
                    {
                        SW_LOG_ERROR( "SQL migration list '%#' (%#): %#", resourceID.c_str(), pDriverName, error.c_str() );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    /** @brief SQL 마이그레이션 목록의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 `common/sql/<폴더>/migrations.json` 을 읽습니다). */
    SW_RESOURCE_DATA_KIND( SQLMigrationList, "sqlmigrations", "common/sql/", nullptr, "/migrations.json", &SQLMigrationListDataKindInternal::loadList );
} // namespace sw
