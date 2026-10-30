#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"

using namespace sw;

SW_TEST_CASE( CollectContentFilesTest, LeavesTheScratchDomainOut )
{
    const string root = test::makeScratchPath( "Content" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "game/empty/maps" ) ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, AssetPaths::kScratchDomain ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( root, "game/empty/maps/a.scene.xml" ), "<Scene/>" ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( FileUtil::joinPath( root, AssetPaths::kScratchDomain ), "left.scene.xml" ), "<broken" ) );

    vector<string> listFile;
    SW_REQUIRE_TRUE( AssetPaths::collectContentFiles( root, ".xml", listFile ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFile.size() );
    SW_EXPECT_TRUE( StringUtil::endsWith( FileUtil::normalizeSeparators( listFile[0] ), "game/empty/maps/a.scene.xml" ) );
}

SW_TEST_CASE( CollectContentFilesTest, DomainlessFolderListsEveryDomain )
{
    // 도메인 없는 폴더 나열은 검색 순서의 모든 도메인을 합친다 — 팩 나열과 같은 답(예전엔 이긴 도메인 폴더 하나만 나열했다).
    vector<AssetID> listID;
    SW_REQUIRE_TRUE( AssetIO::collectAssets( AssetID{ "materials" }, ".material", listID ) == AssetFolderListing::Listed );
    bool bEngine = false;
    bool bCommon = false;
    for ( const AssetID& id : listID )
    {
        bEngine = bEngine || id.view() == "materials/defaultmaterial.material";
        bCommon = bCommon || id.view() == "materials/blaster_palette.material";
    }
    SW_EXPECT_TRUE( bEngine );
    SW_EXPECT_TRUE( bCommon );
}

SW_TEST_CASE( CollectContentFilesTest, GamePackDomainsAreListedByName )
{
    // 게임 팩 나열 하나 — 이름 순 `game/<팩>`, game/ 이 없으면 빈 목록(실패가 아니다).
    const string root = test::makeScratchPath( "Content" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "game/zeta" ) ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( root, "game/alpha" ) ) );
    vector<string> listDomain;
    SW_REQUIRE_TRUE( AssetPaths::collectGamePackDomains( root, listDomain ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listDomain.size() );
    SW_EXPECT_EQUAL( string( "game/alpha" ), listDomain[0] );
    SW_EXPECT_EQUAL( string( "game/zeta" ), listDomain[1] );

    vector<string> listNone;
    SW_EXPECT_TRUE( AssetPaths::collectGamePackDomains( FileUtil::joinPath( root, "game/alpha" ), listNone ) );
    SW_EXPECT_TRUE( listNone.empty() );
}
