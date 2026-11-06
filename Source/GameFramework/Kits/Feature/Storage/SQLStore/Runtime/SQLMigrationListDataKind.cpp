#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/AssetTypeRegistry.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLMigrationRunner.h"

namespace sw
{
    namespace
    {
        struct SQLMigrationListDataKindInternal
        {
            /**
             * @brief 마이그레이션 목록(`<폴더>/migrations.xml`)을 실행기가 읽는 그대로 읽습니다. 드라이버마다 목록의 모든 `.sql` 이 있고 번호가 이어져야 합니다.
             * @details 서버는 기동 때 이 목록으로 스키마를 올린다 — 목록이 틀리면 스키마 테스트에서 먼저 드러나게 한다.
             *          보는 드라이버는 손으로 적은 이름 목록이 아니라 드라이버 등록 표(`SQLDriverRegistry`)의 이름과, 폴더의 방언 분기 파일이 가리키는 이름이다
             *          (등록 표에 아직 오르지 않은 드라이버의 방언 분기도 빠뜨리지 않는다).
             */
            [[nodiscard]] static bool load( const AssetID& id, vector<string>& outListDriverName )
            {
                const string& assetID = id.str();
                string_view   folder;
                FileUtil::getDirectoryPart( assetID, folder );
                if ( folder.empty() )
                    return false;
                SQLDriverRegistry::getDriverNames( outListDriverName );
                string error;
                if ( SQLMigrationRunner::collectDialectDriverNames( folder, outListDriverName, error ) == false )
                {
                    SW_LOG_ERROR_ID( "SQLMigrationListDataKind.SQLMigrationList", "SQL migration list '%#': %#", assetID.c_str(), error.c_str() );
                    return false;
                }
                for ( const string& driverName : outListDriverName )
                {
                    vector<SQLMigration> listMigration;
                    if ( SQLMigrationRunner::loadMigrations( folder, driverName, listMigration, error ) == false )
                    {
                        SW_LOG_ERROR_ID( "SQLMigrationListDataKind.SQLMigrationList2", "SQL migration list '%#' (%#): %#", assetID.c_str(), driverName.c_str(), error.c_str() );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    /** @brief SQL 마이그레이션 목록의 데이터 파일 종류입니다(스키마 테스트가 이 줄로 `common/sql/<폴더>/migrations.xml` 을 읽습니다). */
    SW_ASSET_TYPE( SQLMigrationList, AssetTypeDef::makeDataFile( "sqlmigrations", AssetPathRule{ "common/sql/", {}, "/migrations.xml" }, AssetTypeLoader::with<vector<string>, &SQLMigrationListDataKindInternal::load>() ) );
} // namespace sw
