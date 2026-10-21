#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/Pack/PackPatch.h"
#include "Engine/Resource/Pack/ResourcePakManager.h"

#include "EngineTest/ResourcePakTestUtil.h"

#include "TestFramework/TestFramework.h"

// 팩 단위 차분 배포(docs/plans/PatchDeltaDelivery.md) — 차이 셈, 패치 목록 형식, 패치 사슬, 지움 목록으로 아래 팩 가리기.

/**
 * @brief [PackPatchTest] 두 빌드의 차이는 새 · 바뀐 경로와 지운 경로다 — 같은 내용은 싣지 않고, 결과는 경로 사전순이다
 */
SW_TEST_CASE( PackPatchTest, DeltaListsChangedAndRemovedPathsInOrder )
{
    const sw::vector<sw::PackContentEntry> listBase = {
        { "maps/a.scene", 1},
        { "maps/b.scene", 2},
        {"ui/old.ui.xml", 3},
    };
    const sw::vector<sw::PackContentEntry> listTarget = {
        {  "maps/b.scene", 20},
        {  "maps/a.scene",  1},
        {"audio/new.bank",  4},
    };
    sw::PackDelta delta;
    SW_ASSERT_TRUE( sw::PackPatchUtil::computeDelta( listBase, listTarget, delta ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, delta._listChangedPath.size() );
    SW_EXPECT_TRUE( delta._listChangedPath[0] == "audio/new.bank" );
    SW_EXPECT_TRUE( delta._listChangedPath[1] == "maps/b.scene" );
    SW_ASSERT_EQUAL( size_t{ 1 }, delta._listRemovedPath.size() );
    SW_EXPECT_TRUE( delta._listRemovedPath[0] == "ui/old.ui.xml" );

    SW_TEST_DEFENSIVE_SCOPE( "a build that lists one path twice is refused" );
    const sw::vector<sw::PackContentEntry> listTwice = {
        {"maps/a.scene", 1},
        {"maps/a.scene", 5},
    };
    SW_EXPECT_FALSE( sw::PackPatchUtil::computeDelta( listTwice, listTarget, delta ) );
}

/**
 * @brief [PackPatchTest] 패치 목록은 쓴 그대로 다시 읽히고, 필수 속성이 없거나 같은 빌드로 가는 목록은 거절된다
 */
SW_TEST_CASE( PackPatchTest, ManifestRoundTripsAndRefusesBrokenOnes )
{
    sw::PackPatchManifest manifest;
    manifest._fromBuild       = "1.4.0";
    manifest._toBuild         = "1.4.1";
    manifest._packFileName    = "patch_1.4.1.pack";
    manifest._listRemovedPath = { "ui/old.ui.xml", "maps/c.scene" };
    sw::PackPatchManifest readBack;
    SW_ASSERT_TRUE( sw::PackPatchUtil::parseManifest( sw::PackPatchUtil::makeManifestText( manifest ), "roundtrip", readBack ) );
    SW_EXPECT_TRUE( readBack._fromBuild == "1.4.0" );
    SW_EXPECT_TRUE( readBack._toBuild == "1.4.1" );
    SW_EXPECT_TRUE( readBack._packFileName == "patch_1.4.1.pack" );
    SW_ASSERT_EQUAL( size_t{ 2 }, readBack._listRemovedPath.size() );
    SW_EXPECT_TRUE( readBack._listRemovedPath[1] == "maps/c.scene" );

    SW_TEST_DEFENSIVE_SCOPE( "a manifest without 'to' or one that goes to its own build is refused" );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseManifest( "<PackPatch from=\"1\" pack=\"p.pack\"/>", "noTo", readBack ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseManifest( "<PackPatch from=\"1\" to=\"1\" pack=\"p.pack\"/>", "self", readBack ) );
}

/**
 * @brief [PackPatchTest] 패치 사슬은 설치된 빌드에서 시작하는 하나씩을 따라간다 — 끊기거나 갈리면 거절하고, 이미 목표면 빈 순서다
 */
SW_TEST_CASE( PackPatchTest, PatchChainFollowsFromBuildsAndRefusesGapsAndForks )
{
    const sw::vector<sw::PackPatchManifest> listManifest = {
        {"1.1", "1.2", "p12.pack", {}},
        {"1.0", "1.1", "p11.pack", {}},
        {"1.2", "1.3", "p13.pack", {}},
    };
    sw::vector<size_t> listOrder;
    SW_ASSERT_TRUE( sw::PackPatchUtil::orderPatchChain( "1.0", "1.3", listManifest, listOrder ) );
    SW_ASSERT_EQUAL( size_t{ 3 }, listOrder.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOrder[0] );
    SW_EXPECT_EQUAL( size_t{ 0 }, listOrder[1] );
    SW_EXPECT_EQUAL( size_t{ 2 }, listOrder[2] );
    SW_ASSERT_TRUE( sw::PackPatchUtil::orderPatchChain( "1.3", "1.3", listManifest, listOrder ) );
    SW_EXPECT_TRUE( listOrder.empty() );

    SW_TEST_DEFENSIVE_SCOPE( "a gap or a fork in the patch chain is refused" );
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChain( "0.9", "1.3", listManifest, listOrder ) );
    sw::vector<sw::PackPatchManifest> listFork = listManifest;
    listFork.push_back( { "1.1", "1.2b", "p12b.pack", {} } );
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChain( "1.0", "1.3", listFork, listOrder ) );
}

/**
 * @brief [PackPatchTest] 패치 팩은 바뀐 파일로 아래 팩을 덮고, 지움 목록의 경로는 아래 팩에 있어도 없는 것으로 보이게 한다(폴더 나열 포함)
 * @details 전역 VFS 를 건드리지 않게 매니저를 따로 만든다. 낱개 파일은 끈다 — 패치는 팩만 읽는 배포 구성의 일이다.
 */
SW_TEST_CASE( PackPatchTest, PatchPackOverridesAndHidesRemovedPaths )
{
    const sw::string folder    = test::makeTempDirectory( "packpatch" );
    const sw::string basePack  = sw::FileUtil::joinPath( folder, "base.pack" );
    const sw::string patchPack = sw::FileUtil::joinPath( folder, "patch_1.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( basePack, 0, sw::PackCompressionType::None, {
                                                                                                                   {  "data/keep.txt", "KEEP"},
                                                                                                                   {"data/change.txt",  "OLD"},
                                                                                                                   {  "data/gone.txt", "GONE"},
    } ) );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( patchPack, 0, sw::PackCompressionType::None, {
                                                                                                                    { "data/change.txt", "NEW" },
    } ) );

    sw::ResourcePakManager packManager;
    packManager.setAllowLooseFiles( false );
    SW_ASSERT_TRUE( packManager.mountPack( basePack, 1000 ) );
    SW_ASSERT_TRUE( packManager.mountPatchPack( patchPack, 2000, { "data/gone.txt" } ) );

    sw::string text;
    SW_ASSERT_TRUE( packManager.readTextFile( "data/change.txt", text ) );
    SW_EXPECT_TRUE( text == "NEW" );
    SW_ASSERT_TRUE( packManager.readTextFile( "data/keep.txt", text ) );
    SW_EXPECT_TRUE( text == "KEEP" );
    SW_EXPECT_FALSE( packManager.hasFile( "data/gone.txt" ) );

    sw::vector<sw::string> listEntry;
    packManager.collectFolderEntries( "data", listEntry );
    for ( const sw::string& entryPath : listEntry )
    {
        SW_EXPECT_TRUE_MSG( entryPath != "data/gone.txt", entryPath.c_str() );
    }

    // 패치를 내리면 지운 파일이 다시 보인다(되돌리기).
    SW_ASSERT_TRUE( packManager.unmountPack( patchPack ) );
    SW_EXPECT_TRUE( packManager.hasFile( "data/gone.txt" ) );
    packManager.unmountAll();
}
