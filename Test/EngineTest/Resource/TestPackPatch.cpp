#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/Pack/PackPatch.h"
#include "Engine/Resource/Pack/PackPatchBuilder.h"
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

namespace
{
    /** @brief 팩 폴더에 팩 하나를 씁니다. 내용은 (팩 안 경로, 글) 쌍입니다. */
    [[nodiscard]] bool writeTestPack( const sw::string& folder, const utf8* pFileName, const sw::vector<sw::pair<sw::string, sw::string>>& listContent )
    {
        return sw::test::ResourcePakTestUtil::createPackFile( sw::FileUtil::joinPath( folder, pFileName ), 0, sw::PackCompressionType::None, listContent );
    }

    /** @brief 패치 목록 · 빌드 기록 같은 글 파일을 폴더에 씁니다. */
    [[nodiscard]] bool writeTestText( const sw::string& folder, const utf8* pFileName, const sw::string& text )
    {
        return sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, pFileName ), text );
    }

    sw::string makeManifest( const utf8* pFrom, const utf8* pTo, const utf8* pPack, const sw::vector<sw::string>& listRemoved )
    {
        sw::PackPatchManifest manifest;
        manifest._fromBuild       = pFrom;
        manifest._toBuild         = pTo;
        manifest._packFileName    = pPack;
        manifest._listRemovedPath = listRemoved;
        return sw::PackPatchUtil::makeManifestText( manifest );
    }

    sw::string makeRecord( const utf8* pBuild )
    {
        sw::PackBuildRecord record;
        record._build = pBuild;
        return sw::PackPatchUtil::makeBuildRecordText( record );
    }
} // namespace

/**
 * @brief [PackPatchTest] 빌드 내용 목록은 쓴 그대로(경로 사전순으로) 다시 읽히고, 해시 자리 수가 틀리거나 같은 경로가 둘이면 거절된다
 */
SW_TEST_CASE( PackPatchTest, BuildRecordRoundTripsAndRefusesBrokenOnes )
{
    sw::PackBuildRecord record;
    record._build     = "1.4.0";
    record._listEntry = {
        {            "engine/ui/b.xml", 0xFFFFFFFFFFFFFFFFull},
        {            "engine/ui/a.xml",               0x1Full},
        {"game/empty/maps/a.scene.xml", 0x0123456789ABCDEFull},
    };
    const sw::string    text = sw::PackPatchUtil::makeBuildRecordText( record );
    sw::PackBuildRecord readBack;
    SW_ASSERT_TRUE( sw::PackPatchUtil::parseBuildRecord( text, "roundtrip", readBack ) );
    SW_EXPECT_TRUE( readBack._build == "1.4.0" );
    SW_ASSERT_EQUAL( size_t{ 3 }, readBack._listEntry.size() );
    SW_EXPECT_TRUE( readBack._listEntry[0]._path == "engine/ui/a.xml" ); // 사전순으로 쓴다
    SW_EXPECT_EQUAL( uint64{ 0x1F }, readBack._listEntry[0]._contentHash );
    SW_EXPECT_TRUE( readBack._listEntry[1]._path == "engine/ui/b.xml" );
    SW_EXPECT_EQUAL( uint64{ 0xFFFFFFFFFFFFFFFFull }, readBack._listEntry[1]._contentHash );
    SW_EXPECT_TRUE( readBack._listEntry[2]._path == "game/empty/maps/a.scene.xml" );
    SW_EXPECT_EQUAL( uint64{ 0x0123456789ABCDEFull }, readBack._listEntry[2]._contentHash );
    SW_EXPECT_TRUE( sw::PackPatchUtil::makeBuildRecordText( readBack ) == text ); // 같은 입력은 같은 글이다

    SW_TEST_DEFENSIVE_SCOPE( "a record without a build, with a short hash or with a path twice is refused" );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord( "<PackBuild><Entry path=\"a\" hash=\"0000000000000001\"/></PackBuild>", "noBuild", readBack ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord( "<PackBuild build=\"1\"><Entry path=\"a\" hash=\"1\"/></PackBuild>", "shortHash", readBack ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord(
        "<PackBuild build=\"1\"><Entry path=\"a\" hash=\"0000000000000001\"/><Entry path=\"a\" hash=\"0000000000000002\"/></PackBuild>", "twice", readBack ) );
    sw::PackPatchManifest outsideManifest;
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseManifest( "<PackPatch from=\"1\" to=\"2\" pack=\"../p.pack\"/>", "outside", outsideManifest ) );
}

/**
 * @brief [PackPatchTest] 패치를 쓰지 않는 빌드는 기록에 `patching="none"` 을 명시하고, 값이 없는 옛 기록은 chain 으로 보지 않고 거절하며, none 폴더의 패치 목록은 시작 마운트가 거절한다
 */
SW_TEST_CASE( PackPatchTest, NoPatchingIsAnExplicitValueOfTheBuildRecord )
{
    sw::PackBuildRecord none;
    none._patching               = sw::PackPatching::None;
    const sw::string    noneText = sw::PackPatchUtil::makeBuildRecordText( none );
    sw::PackBuildRecord readBack;
    SW_ASSERT_TRUE( sw::PackPatchUtil::parseBuildRecord( noneText, "none", readBack ) );
    SW_EXPECT_TRUE( readBack._patching == sw::PackPatching::None );
    SW_EXPECT_TRUE( readBack._build.empty() && readBack._listEntry.empty() );

    SW_TEST_DEFENSIVE_SCOPE( "a record without patching, with an unknown value, or none carrying a build is refused" );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord( "<PackBuild build=\"1\"/>", "noPatching", readBack ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord( "<PackBuild patching=\"maybe\" build=\"1\"/>", "unknown", readBack ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::parseBuildRecord( "<PackBuild patching=\"none\" build=\"1\"/>", "noneWithBuild", readBack ) );

    sw::ResourcePakManager packManager;
    const sw::string       folder = test::makeTempDirectory( "packpatchnone" );
    SW_ASSERT_TRUE( writeTestPack( folder, "engine.pack", {
                                                              { "ui/a.xml", "A0" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( folder, "base.packbuild.xml", noneText ) );
    SW_ASSERT_TRUE( writeTestPack( folder, "patch_1.1.pack", {
                                                                 { "engine/ui/a.xml", "A1" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( folder, "patch_1.1.packpatch.xml", makeManifest( "1.0", "1.1", "patch_1.1.pack", {} ) ) );
    SW_EXPECT_FALSE( packManager.scanAndMountPacks( folder, { "engine" } ) ); // 패치를 쓰지 않는다고 밝힌 빌드에 패치 목록이 있다
    SW_EXPECT_EQUAL( size_t{ 0 }, packManager.getMountedPackCount() );
}

/**
 * @brief [PackPatchTest] 패치 사슬 끝까지 셈은 설치된 빌드부터 이어지는 패치를 모두 순서대로 내고, 갈림 · 고리 · 닿지 않는 패치는 거절한다
 */
SW_TEST_CASE( PackPatchTest, ChainToLatestFollowsEveryPatchAndRefusesOrphans )
{
    const sw::vector<sw::PackPatchManifest> listManifest = {
        {"1.1", "1.2", "p12.pack", {}},
        {"1.0", "1.1", "p11.pack", {}},
    };
    sw::vector<size_t> listOrder;
    sw::string         latest;
    SW_ASSERT_TRUE( sw::PackPatchUtil::orderPatchChainToLatest( "1.0", listManifest, listOrder, latest ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, listOrder.size() );
    SW_EXPECT_EQUAL( size_t{ 1 }, listOrder[0] );
    SW_EXPECT_EQUAL( size_t{ 0 }, listOrder[1] );
    SW_EXPECT_TRUE( latest == "1.2" );
    SW_ASSERT_TRUE( sw::PackPatchUtil::orderPatchChainToLatest( "1.0", {}, listOrder, latest ) );
    SW_EXPECT_TRUE( listOrder.empty() && latest == "1.0" );

    SW_TEST_DEFENSIVE_SCOPE( "a fork, a loop and a patch that cannot be reached are refused" );
    sw::vector<sw::PackPatchManifest> listFork = listManifest;
    listFork.push_back( { "1.1", "1.2b", "p12b.pack", {} } );
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChainToLatest( "1.0", listFork, listOrder, latest ) );
    const sw::vector<sw::PackPatchManifest> listLoop = {
        {"1.0", "1.1", "p11.pack", {}},
        {"1.1", "1.0", "p10.pack", {}},
    };
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChainToLatest( "1.0", listLoop, listOrder, latest ) );
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChainToLatest( "0.9", listManifest, listOrder, latest ) ); // 설치된 빌드에서 시작하는 패치가 없어 둘 다 닿지 않는다
    SW_EXPECT_FALSE( sw::PackPatchUtil::orderPatchChainToLatest( "1.1", listManifest, listOrder, latest ) ); // 1.0 -> 1.1 패치는 이미 설치된 빌드 앞이라 닿지 않는다
}

/**
 * @brief [PackPatchTest] 시작 마운트는 패치 목록의 사슬을 기본 팩 위에 적용한다 — 패치 팩은 기본 팩으로 마운트되지 않고, 바뀐 파일이 덮고 지운 파일이 가려진다
 * @details 변이: `scanAndMountPacks` 가 패치 목록이 가리키는 팩도 기본 팩으로 마운트하면 우선순위가 어긋나 읽는 값이 틀린다.
 */
SW_TEST_CASE( PackPatchTest, StartupMountAppliesThePatchChainOnTopOfTheBasePacks )
{
    const sw::string folder = test::makeTempDirectory( "packpatchstartup" );
    SW_ASSERT_TRUE( writeTestPack( folder, "engine.pack", {
                                                              {   "ui/a.xml",   "A0"},
                                                              { "ui/old.xml",  "OLD"},
                                                              {"ui/keep.xml", "KEEP"}
    } ) );
    SW_ASSERT_TRUE( writeTestText( folder, "base.packbuild.xml", makeRecord( "1.0" ) ) );
    SW_ASSERT_TRUE( writeTestPack( folder, "patch_1.1.pack", {
                                                                 { "engine/ui/a.xml", "A1" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( folder, "patch_1.1.packpatch.xml", makeManifest( "1.0", "1.1", "patch_1.1.pack", { "engine/ui/old.xml" } ) ) );
    SW_ASSERT_TRUE( writeTestPack( folder, "patch_1.2.pack", {
                                                                 {  "engine/ui/a.xml",  "A2"},
                                                                 {"engine/ui/new.xml", "NEW"}
    } ) );
    SW_ASSERT_TRUE( writeTestText( folder, "patch_1.2.packpatch.xml", makeManifest( "1.1", "1.2", "patch_1.2.pack", {} ) ) );

    sw::ResourcePakManager packManager;
    packManager.setAllowLooseFiles( false );
    SW_ASSERT_TRUE( packManager.scanAndMountPacks( folder, { "engine" } ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, packManager.getMountedPackCount() ); // 기본 팩 하나와 패치 둘
    sw::string text;
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/a.xml", text ) );
    SW_EXPECT_TRUE( text == "A2" ); // 뒤 패치가 앞 패치를 덮는다
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/new.xml", text ) );
    SW_EXPECT_TRUE( text == "NEW" );
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/keep.xml", text ) );
    SW_EXPECT_TRUE( text == "KEEP" );
    SW_EXPECT_FALSE( packManager.hasFile( "engine/ui/old.xml" ) );
    packManager.unmountAll();
}

/**
 * @brief [PackPatchTest] 패치를 적용하지 못하면(빌드 기록 없음 · 패치 팩 없음 · 사슬 끊김) 기본 팩도 마운트하지 않고 실패한다 — 절반만 패치된 채 돌지 않는다
 */
SW_TEST_CASE( PackPatchTest, StartupMountMountsNothingWhenAPatchCannotBeApplied )
{
    SW_TEST_DEFENSIVE_SCOPE( "an unusable patch folder fails the whole mount" );
    const sw::string noRecord = test::makeTempDirectory( "packpatchnorecord" );
    SW_ASSERT_TRUE( writeTestPack( noRecord, "engine.pack", {
                                                                { "ui/a.xml", "A0" }
    } ) );
    SW_ASSERT_TRUE( writeTestPack( noRecord, "patch_1.1.pack", {
                                                                   { "engine/ui/a.xml", "A1" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( noRecord, "patch_1.1.packpatch.xml", makeManifest( "1.0", "1.1", "patch_1.1.pack", {} ) ) );
    sw::ResourcePakManager packManager;
    packManager.setAllowLooseFiles( false );
    SW_EXPECT_FALSE( packManager.scanAndMountPacks( noRecord, { "engine" } ) ); // 설치된 빌드를 모른다
    SW_EXPECT_EQUAL( size_t{ 0 }, packManager.getMountedPackCount() );

    const sw::string missingPack = test::makeTempDirectory( "packpatchmissingpack" );
    SW_ASSERT_TRUE( writeTestPack( missingPack, "engine.pack", {
                                                                   { "ui/a.xml", "A0" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( missingPack, "base.packbuild.xml", makeRecord( "1.0" ) ) );
    SW_ASSERT_TRUE( writeTestText( missingPack, "patch_1.1.packpatch.xml", makeManifest( "1.0", "1.1", "patch_1.1.pack", {} ) ) );
    SW_EXPECT_FALSE( packManager.scanAndMountPacks( missingPack, { "engine" } ) ); // 목록이 가리키는 팩이 없다
    SW_EXPECT_EQUAL( size_t{ 0 }, packManager.getMountedPackCount() );

    const sw::string gap = test::makeTempDirectory( "packpatchgap" );
    SW_ASSERT_TRUE( writeTestPack( gap, "engine.pack", {
                                                           { "ui/a.xml", "A0" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( gap, "base.packbuild.xml", makeRecord( "1.0" ) ) );
    SW_ASSERT_TRUE( writeTestPack( gap, "patch_1.2.pack", {
                                                              { "engine/ui/a.xml", "A2" }
    } ) );
    SW_ASSERT_TRUE( writeTestText( gap, "patch_1.2.packpatch.xml", makeManifest( "1.1", "1.2", "patch_1.2.pack", {} ) ) );
    SW_EXPECT_FALSE( packManager.scanAndMountPacks( gap, { "engine" } ) ); // 1.0 -> 1.1 패치가 빠졌다
    SW_EXPECT_EQUAL( size_t{ 0 }, packManager.getMountedPackCount() );
}

/**
 * @brief [PackPatchTest] 패치 만들기: 이전 빌드의 기록과 새 팩의 차이로 만든 패치를 이전 빌드 위에 적용하면 새 빌드와 같은 내용이 보인다
 * @details 새 빌드는 바뀐 파일 · 새 파일 · 지운 파일 · 안 바뀐 파일을 두 도메인(engine, game/empty)에 나눠 담는다. 패치 팩에는 바뀐 파일과 새 파일만 들어간다.
 */
SW_TEST_CASE( PackPatchTest, MadePatchOnTheOldBuildShowsTheNewBuildContent )
{
    const sw::string oldFolder = test::makeTempDirectory( "packpatchbuildold" );
    SW_ASSERT_TRUE( writeTestPack( oldFolder, "engine.pack", {
                                                                 {   "ui/a.xml",   "A0"},
                                                                 { "ui/old.xml",  "OLD"},
                                                                 {"ui/keep.xml", "KEEP"}
    } ) );
    SW_ASSERT_TRUE( writeTestPack( oldFolder, "game_empty.pack", {
                                                                     { "maps/m.scene.xml", "M0" }
    } ) );
    const sw::string oldRecordPath = sw::FileUtil::joinPath( oldFolder, sw::PackPatchUtil::kBuildRecordFileName );
    SW_ASSERT_TRUE( sw::PackPatchBuilder::writeBuildRecord( oldFolder, "1.0", oldRecordPath ) );

    const sw::string newFolder = test::makeTempDirectory( "packpatchbuildnew" );
    SW_ASSERT_TRUE( writeTestPack( newFolder, "engine.pack", {
                                                                 {   "ui/a.xml",   "A1"},
                                                                 {"ui/keep.xml", "KEEP"},
                                                                 { "ui/new.xml",  "NEW"}
    } ) );
    SW_ASSERT_TRUE( writeTestPack( newFolder, "game_empty.pack", {
                                                                     { "maps/m.scene.xml", "M1" }
    } ) );

    const sw::string         outFolder = test::makeTempDirectory( "packpatchbuildout" );
    sw::PackPatchBuildParams params;
    params._packsDirectory     = newFolder;
    params._previousRecordPath = oldRecordPath;
    params._newBuild           = "1.1";
    params._outputDirectory    = outFolder;
    SW_ASSERT_TRUE( sw::PackPatchBuilder::makePatch( params ) );

    sw::string            manifestText;
    sw::PackPatchManifest manifest;
    const sw::string      manifestFileName = sw::PackPatchBuilder::makeManifestFileName( "1.1" );
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( outFolder, manifestFileName ), manifestText ) );
    SW_ASSERT_TRUE( sw::PackPatchUtil::parseManifest( manifestText, "made", manifest ) );
    SW_EXPECT_TRUE( manifest._fromBuild == "1.0" && manifest._toBuild == "1.1" );
    SW_EXPECT_TRUE( manifest._packFileName == sw::PackPatchBuilder::makePatchPackFileName( "1.1" ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, manifest._listRemovedPath.size() );
    SW_EXPECT_TRUE( manifest._listRemovedPath[0] == "engine/ui/old.xml" );

    // 패치 팩에는 바뀐 파일 · 새 파일 셋만 있다: engine/ui/a.xml, engine/ui/new.xml, game/empty/maps/m.scene.xml.
    sw::ResourcePakReader patchReader;
    SW_ASSERT_TRUE( patchReader.open( sw::FileUtil::joinPath( outFolder, manifest._packFileName ) ) );
    SW_EXPECT_EQUAL( uint32{ 3 }, patchReader.getFileCount() );
    patchReader.close();

    // 이전 빌드를 설치한 폴더에 패치 두 파일을 추가하면 새 빌드가 보인다.
    for ( const sw::string& fileName : { manifest._packFileName, manifestFileName } )
    {
        sw::vector<uint8> bytes;
        SW_ASSERT_TRUE( sw::FileUtil::readFile( sw::FileUtil::joinPath( outFolder, fileName ), bytes ) );
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( sw::FileUtil::joinPath( oldFolder, fileName ), bytes.data(), bytes.size() ) );
    }
    sw::ResourcePakManager packManager;
    packManager.setAllowLooseFiles( false );
    SW_ASSERT_TRUE( packManager.scanAndMountPacks( oldFolder, { "engine" } ) );
    sw::string text;
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/a.xml", text ) );
    SW_EXPECT_TRUE( text == "A1" );
    SW_ASSERT_TRUE( packManager.readTextFile( "game/empty/maps/m.scene.xml", text ) );
    SW_EXPECT_TRUE( text == "M1" );
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/new.xml", text ) );
    SW_EXPECT_TRUE( text == "NEW" );
    SW_EXPECT_FALSE( packManager.hasFile( "engine/ui/old.xml" ) );
    SW_ASSERT_TRUE( packManager.readTextFile( "engine/ui/keep.xml", text ) );
    SW_EXPECT_TRUE( text == "KEEP" );
    packManager.unmountAll();

    // 만든 새 빌드의 기록은 새 팩의 내용과 같다(다음 패치의 기준이 된다).
    sw::PackBuildRecord madeRecord;
    sw::string          madeText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( outFolder, "1.1" + sw::string( sw::PackPatchUtil::kBuildRecordExtension ) ), madeText ) );
    SW_ASSERT_TRUE( sw::PackPatchUtil::parseBuildRecord( madeText, "made record", madeRecord ) );
    sw::vector<sw::PackContentEntry> listNewEntry;
    SW_ASSERT_TRUE( sw::PackPatchBuilder::collectBuildEntries( newFolder, listNewEntry ) );
    SW_ASSERT_EQUAL( listNewEntry.size(), madeRecord._listEntry.size() );
    for ( size_t entryIndex = 0; entryIndex < listNewEntry.size(); ++entryIndex )
    {
        SW_EXPECT_TRUE( listNewEntry[entryIndex]._path == madeRecord._listEntry[entryIndex]._path );
        SW_EXPECT_EQUAL( listNewEntry[entryIndex]._contentHash, madeRecord._listEntry[entryIndex]._contentHash );
    }
}

/**
 * @brief [PackPatchTest] 패치 만들기는 바뀐 것이 없거나 · 같은 빌드 번호이거나 · 쓸 수 없는 빌드 이름이거나 · 이미 패치된 폴더이면 거절한다
 */
SW_TEST_CASE( PackPatchTest, MakePatchRefusesNothingToPatchAndUnusableInputs )
{
    const sw::string folder = test::makeTempDirectory( "packpatchrefuse" );
    SW_ASSERT_TRUE( writeTestPack( folder, "engine.pack", {
                                                              { "ui/a.xml", "A0" }
    } ) );
    const sw::string recordPath = sw::FileUtil::joinPath( folder, sw::PackPatchUtil::kBuildRecordFileName );
    SW_ASSERT_TRUE( sw::PackPatchBuilder::writeBuildRecord( folder, "1.0", recordPath ) );

    sw::PackPatchBuildParams params;
    params._packsDirectory     = folder;
    params._previousRecordPath = recordPath;
    params._newBuild           = "1.1";
    params._outputDirectory    = test::makeTempDirectory( "packpatchrefuseout" );

    SW_TEST_DEFENSIVE_SCOPE( "an empty patch, a repeated build, a bad name and a patched folder are refused" );
    SW_EXPECT_FALSE( sw::PackPatchBuilder::makePatch( params ) ); // 같은 내용이다
    params._newBuild = "1.0";
    SW_EXPECT_FALSE( sw::PackPatchBuilder::makePatch( params ) );
    params._newBuild = "1/1";
    SW_EXPECT_FALSE( sw::PackPatchBuilder::makePatch( params ) );
    SW_EXPECT_FALSE( sw::PackPatchBuilder::isValidBuildName( "" ) );
    SW_EXPECT_TRUE( sw::PackPatchBuilder::isValidBuildName( "1.4.0-rc_2" ) );
    SW_ASSERT_TRUE( writeTestText( folder, "patch_1.packpatch.xml", makeManifest( "1.0", "1.1", "patch_1.pack", {} ) ) );
    params._newBuild = "1.2";
    SW_EXPECT_FALSE( sw::PackPatchBuilder::makePatch( params ) ); // 패치 목록이 있는 폴더는 기본 팩의 폴더가 아니다
}
