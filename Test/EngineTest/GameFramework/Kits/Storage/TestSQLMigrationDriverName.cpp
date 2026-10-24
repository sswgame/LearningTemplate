#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLMigrationRunner.h"

#include "TestFramework/TestFramework.h"

// 마이그레이션 목록 검사(`SQLMigrationListDataKind`)가 보는 드라이버 이름은 손으로 적은 목록이 아니다 — 드라이버 등록 표의 이름에
// 폴더의 방언 분기 파일(`NNNN_이름.<드라이버>.sql`)이 가리키는 이름을 더한다. 등록 표에 늦게 오르는 드라이버의 분기도 빠뜨리지 않는다.

using namespace sw;

SW_TEST_CASE( SQLMigrationDriverNameTest, RegistryNamesTheBuiltInDriver )
{
    vector<string> listDriverName;
    SQLDriverRegistry::getDriverNames( listDriverName );
    SW_EXPECT_TRUE( std::find( listDriverName.begin(), listDriverName.end(), string( "sqlite" ) ) != listDriverName.end() );
}

SW_TEST_CASE( SQLMigrationDriverNameTest, DialectFilesAddTheirDriverOnce )
{
    const string folder = test::makeTempDirectory( "migration_driver_names" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0001_first.sql" ), "SELECT 1;" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0002_second.sqlite.sql" ), "SELECT 2;" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0002_second.postgres.sql" ), "SELECT 2;" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0003_third.postgres.sql" ), "SELECT 3;" ) );

    vector<string> listDriverName{ string( "sqlite" ) }; // 등록 표에서 받은 이름
    string         error;
    SW_ASSERT_TRUE_MSG( SQLMigrationRunner::collectDialectDriverNames( folder, listDriverName, error ), error.c_str() );
    SW_ASSERT_EQUAL( size_t( 2 ), listDriverName.size() );
    SW_EXPECT_EQUAL( string( "sqlite" ), listDriverName[0] );
    SW_EXPECT_EQUAL( string( "postgres" ), listDriverName[1] );
}

SW_TEST_CASE( SQLMigrationDriverNameTest, MissingDialectBranchIsCaughtForTheAddedDriver )
{
    // 1 번은 postgres 분기만 있다 — 등록 표에 postgres 가 없어도 폴더가 그 이름을 내므로 sqlite 로 읽기가 실패해 드러난다.
    const string folder = test::makeTempDirectory( "migration_missing_branch" );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "0001_first.postgres.sql" ), "SELECT 1;" ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, SQLMigrationRunner::kListFileName ), R"(<SQLMigrationList><Migration _name="0001_first" /></SQLMigrationList>)" ) );

    vector<string> listDriverName{ string( "sqlite" ) };
    string         error;
    SW_ASSERT_TRUE( SQLMigrationRunner::collectDialectDriverNames( folder, listDriverName, error ) );
    SW_ASSERT_EQUAL( size_t( 2 ), listDriverName.size() );
    vector<SQLMigration> listMigration;
    SW_EXPECT_TRUE( SQLMigrationRunner::loadMigrations( ::test::makeTempResourceID( folder ), "postgres", listMigration, error ) );
    listMigration.clear();
    SW_EXPECT_FALSE( SQLMigrationRunner::loadMigrations( ::test::makeTempResourceID( folder ), "sqlite", listMigration, error ) );
}
