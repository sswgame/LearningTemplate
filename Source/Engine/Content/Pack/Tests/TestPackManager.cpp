#include "pch.h"

#include "Core/File/ContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"

// 팩 가상 파일 시스템 — 도메인 팩은 도메인을 뗀 경로로, 패치 팩은 전체 경로로 담는다. 도메인이 붙은 키와 없는 키 모두에서 패치의 교체 · 지움이 보이고, dlc 팩도 찾는다.
// 도메인은 팩이 적은 마운트 도메인이다 — 파일 이름과 상관없다.

using namespace sw;

namespace
{
    struct TestPackManagerInternal
    {
        /** @brief @p packPath 에 항목들을 담은 팩을 씁니다. @p mountDomain 이 비면 패치 팩이다(항목이 전체 경로). */
        static bool writePack( const AbsolutePath& packPath, string_view mountDomain, const vector<std::pair<string, string>>& listEntry )
        {
            return PackTestUtil::writeTextPack( packPath, mountDomain, listEntry );
        }

        static string read( const PackManager& packs, string_view path )
        {
            string text;
            return packs.readTextFile( path, text ) ? text : string{ "<missing>" };
        }
    };
} // namespace

SW_TEST_CASE( PackManagerTest, PatchReplacesAndRemovesForDomainAndDomainlessKeys )
{
    using Internal           = TestPackManagerInternal;
    const AbsolutePath root  = test::makeScratchPath( "Packs" );
    const AbsolutePath base  = root.join( "engine.pack" );
    const AbsolutePath patch = root.join( "patch_1.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( base, "engine", {
                                                              {"textures/a.txt",  "old"},
                                                              {"textures/b.txt", "gone"},
                                                              {"textures/c.txt", "kept"}
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( patch, {
    },
                                          { { "engine/textures/a.txt", "new" } } ) );

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( base.str(), 1 ) );
    SW_REQUIRE_TRUE( packs.mountPatchPack( patch.str(), 100, { "engine/textures/b.txt" } ) );

    SW_EXPECT_EQUAL( string( "new" ), Internal::read( packs, "engine/textures/a.txt" ) );
    SW_EXPECT_EQUAL( string( "new" ), Internal::read( packs, "textures/a.txt" ) ); // 도메인 없는 키도 패치를 본다
    SW_EXPECT_FALSE( packs.hasFile( "engine/textures/b.txt" ) );
    SW_EXPECT_FALSE( packs.hasFile( "textures/b.txt" ) ); // 지움도 같다
    SW_EXPECT_EQUAL( string( "kept" ), Internal::read( packs, "textures/c.txt" ) );
}

SW_TEST_CASE( PackManagerTest, FindsDLCDomainPacks )
{
    using Internal          = TestPackManagerInternal;
    const AbsolutePath root = test::makeScratchPath( "Packs" );
    const AbsolutePath dlc  = root.join( "seasonal.pack" ); // 이름은 도메인을 말하지 않는다
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( dlc, "dlc/winter", {
                                                                 { "maps/snow.txt", "snow" }
    } ) );

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( dlc.str(), 1 ) );
    SW_EXPECT_EQUAL( string( "snow" ), Internal::read( packs, "dlc/winter/maps/snow.txt" ) );
}

SW_TEST_CASE( PackManagerTest, RejectsPackMountedAgainstItsDomain )
{
    // 도메인 팩을 패치로, 패치 팩을 도메인 팩으로 붙이면 항목 경로의 뜻이 어긋난다 — 붙이지 않는다.
    using Internal            = TestPackManagerInternal;
    const AbsolutePath root   = test::makeScratchPath( "Packs" );
    const AbsolutePath domain = root.join( "engine.pack" );
    const AbsolutePath patch  = root.join( "patch_1.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( domain, "engine", {
                                                                { "a.txt", "a" }
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( patch, {
    },
                                          { { "engine/a.txt", "b" } } ) );

    SW_EXPECT_LOG( "Pack.MountDomainMismatch" );
    PackManager packs;
    SW_EXPECT_FALSE( packs.mountPatchPack( domain.str(), 100, {} ) );
    SW_EXPECT_FALSE( packs.mountPack( patch.str(), 1 ) );
}

// 팩은 콘텐츠 소스로만 읽는다 — 디스크에 없는 메모리 팩(웹이 내려받은 팩)도 같은 길로 훑고 마운트하고 읽는다.
SW_TEST_CASE( PackManagerTest, ScansAndReadsPacksThroughTheContentFileSource )
{
    using Internal              = TestPackManagerInternal;
    const AbsolutePath diskRoot = test::makeScratchPath( "Packs" );
    const AbsolutePath diskPack = diskRoot.join( "engine.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( diskRoot ) );
    SW_REQUIRE_TRUE( Internal::writePack( diskPack, "engine", {
                                                                  { "data/a.txt", "from memory" }
    } ) );
    vector<uint8> packBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( diskPack, packBytes ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( diskPack ) );

    MemoryContentFileSource memorySource;
    memorySource.addFile( "/downloaded/engine.pack", std::move( packBytes ) );
    const IContentFileSource* const pPrevious = ContentFileSource::bindCurrent( &memorySource );
    {
        PackManager packs;
        SW_EXPECT_TRUE( packs.scanAndMountPacks( "/downloaded" ) );
        SW_EXPECT_TRUE( Internal::read( packs, "engine/data/a.txt" ) == "from memory" );
    }
    (void)ContentFileSource::bindCurrent( pPrevious );
}

SW_TEST_CASE( PackManagerTest, PackOutsideTheSearchPriorityNeedsAnExplicitPriority )
{
    // 우선순위 목록(game · common · engine · editor)에 없는 도메인은 기본 우선순위를 정할 수 없다 — 말없이 가장 낮게 붙이지 않는다.
    using Internal          = TestPackManagerInternal;
    const AbsolutePath root = test::makeScratchPath( "Packs" );
    const AbsolutePath dlc  = root.join( "seasonal.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( dlc, "dlc/winter", {
                                                                 { "maps/snow.txt", "snow" }
    } ) );

    SW_EXPECT_LOG( "Pack.NoPriority" );
    PackManager packs;
    SW_EXPECT_FALSE( packs.mountPack( dlc.str() ) );
    SW_EXPECT_TRUE( packs.mountPack( dlc.str(), 1 ) ); // 밝힌 우선순위로는 붙는다
}

// 도메인 팩이 하나도 없는 구성(소스 구성의 모드)에서도 패치 팩의 전체 경로 항목을 찾는다 — 예전에는 붙은 도메인 팩이 없는 도메인의 키를 도메인 없는 키로만 다뤄 놓쳤다.
SW_TEST_CASE( PackManagerTest, PatchPackAloneAnswersItsFullPaths )
{
    using Internal          = TestPackManagerInternal;
    const AbsolutePath root = test::makeScratchPath( "Packs" );
    const AbsolutePath mod  = root.join( "mod.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( mod, {
    },
                                          { { "mods/lantern/data/lantern.xml", "lamp" }, { "game/playground/data/welcome.xml", "modded" } } ) );

    PackManager packs;
    SW_EXPECT_FALSE( packs.hasPatchPacks() );
    SW_EXPECT_EQUAL( 0, packs.getTopPriority() );
    SW_REQUIRE_TRUE( packs.mountPatchPack( mod.str(), 3000, {} ) );
    SW_EXPECT_TRUE( packs.hasPatchPacks() );
    SW_EXPECT_EQUAL( 3000, packs.getTopPriority() );
    SW_EXPECT_EQUAL( string( "lamp" ), Internal::read( packs, "mods/lantern/data/lantern.xml" ) );
    SW_EXPECT_EQUAL( string( "modded" ), Internal::read( packs, "game/playground/data/welcome.xml" ) );
    SW_EXPECT_TRUE( packs.hasPatchFile( "game/playground/data/welcome.xml" ) );
    SW_EXPECT_FALSE( packs.hasPatchFile( "data/welcome.xml" ) ); // 패치 질의는 전체 경로만 본다
    vector<string> listEntry;
    packs.collectFolderEntries( "mods/lantern/data", listEntry );
    SW_EXPECT_EQUAL( size_t{ 1 }, listEntry.size() );

    SW_REQUIRE_TRUE( packs.unmountPack( mod.str() ) );
    SW_EXPECT_FALSE( packs.hasPatchPacks() );
    SW_EXPECT_FALSE( packs.hasFile( "mods/lantern/data/lantern.xml" ) );
}

/**
 * @brief [PackManagerTest] DLC 팩(AppID > 0)은 소유권 검사기가 없으면 붙지 않고, 검사기가 소유를 답할 때만 붙는다
 * @details 예전에는 검사기가 없으면 확인을 건너뛰어 유료 DLC 팩이 누구에게나 붙었다.
 */
SW_TEST_CASE( PackManagerTest, DLCPackNeedsAnEntitlementValidator )
{
    const AbsolutePath root = test::makeScratchPath( "Packs" );
    const AbsolutePath dlc  = root.join( "paid.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    PackWriterDesc desc = PackTestUtil::makeDesc( "dlc/paid" );
    desc._dlcAppID      = 4242;
    SW_REQUIRE_TRUE( PackTestUtil::writePack( dlc, desc, {
                                                             { "maps/gold.txt", vector<uint8>{ 'g', 'o', 'l', 'd' } }
    } ) );

    PackManager packs;
    {
        SW_EXPECT_LOG( "PackManager.NoDLCValidator" );
        SW_EXPECT_FALSE( packs.mountPack( dlc.str(), 1 ) );
    }
    packs.setDLCEntitlementValidator( SW_DELEGATE_LAMBDA( DLCEntitlementDelegate, []( uint32 appID )
    { return appID != 4242; } ) );
    {
        SW_EXPECT_LOG( "PackManager.AccessDeniedDLCPack" );
        SW_EXPECT_FALSE( packs.mountPack( dlc.str(), 1 ) );
    }
    packs.setDLCEntitlementValidator( SW_DELEGATE_LAMBDA( DLCEntitlementDelegate, []( uint32 appID )
    { return appID == 4242; } ) );
    SW_EXPECT_TRUE( packs.mountPack( dlc.str(), 1 ) );
    SW_EXPECT_EQUAL( string( "gold" ), TestPackManagerInternal::read( packs, "dlc/paid/maps/gold.txt" ) );
}

/**
 * @brief [PackManagerTest] 같은 도메인의 형제 팩(나눈 팩 · 청크)은 항목이 겹치면 붙지 않는다 — 어느 쪽이 이길지 조용히 정하지 않는다(뒤가 이기는 것은 패치뿐)
 * @details 겹치지 않는 형제는 함께 붙어 한 도메인으로 읽히고, 같은 파일을 다시 붙이는 것은 형제가 아니다.
 */
SW_TEST_CASE( PackManagerTest, SiblingPacksOverlapIsAnError )
{
    using Internal            = TestPackManagerInternal;
    const AbsolutePath root   = test::makeScratchPath( "SplitPacks" );
    const AbsolutePath first  = root.join( "game_split.pack" );
    const AbsolutePath second = root.join( "game_split.1.pack" );
    const AbsolutePath clash  = root.join( "game_split.2.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( first, "game/split", {
                                                                   { "maps/a.txt", "a" }
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( second, "game/split", {
                                                                    { "maps/b.txt", "b" }
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( clash, "game/split", {
                                                                   { "maps/a.txt", "again" }
    } ) );

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( first.str(), 1 ) );
    SW_REQUIRE_TRUE( packs.mountPack( second.str(), 1 ) );
    SW_EXPECT_EQUAL( string( "a" ), Internal::read( packs, "game/split/maps/a.txt" ) );
    SW_EXPECT_EQUAL( string( "b" ), Internal::read( packs, "game/split/maps/b.txt" ) );
    {
        SW_EXPECT_LOG( "PackManager.SiblingPacksOverlap" );
        SW_EXPECT_FALSE( packs.mountPack( clash.str(), 1 ) );
    }
    SW_EXPECT_TRUE( packs.mountPack( first.str(), 1 ) ); // 같은 파일을 다시 붙인다 — 앞 마운트를 바꿔 끼운다
    SW_EXPECT_EQUAL( size_t{ 2 }, packs.getMountedPackCount() );
}
