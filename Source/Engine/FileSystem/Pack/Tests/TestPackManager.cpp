#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/Pack/PackManager.h"
#include "Engine/FileSystem/Pack/PackWriter.h"

// 팩 가상 파일 시스템 — 도메인 팩은 도메인을 뗀 경로로, 패치 팩은 전체 경로로 담는다. 도메인이 붙은 키와 없는 키 모두에서 패치의 교체 · 지움이 보이고, dlc 팩도 찾는다.
// 도메인은 팩이 적은 마운트 도메인이다 — 파일 이름과 상관없다.

using namespace sw;

namespace
{
    struct TestPackManagerInternal
    {
        static vector<uint8> toBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }

        /** @brief @p packPath 에 항목들을 담은 팩을 씁니다. @p mountDomain 이 비면 패치 팩이다(항목이 전체 경로). */
        static bool writePack( const string& packPath, string_view mountDomain, const vector<std::pair<string, string>>& listEntry )
        {
            PackWriterDesc desc;
            desc._mountDomain = string{ mountDomain };
            PackWriter writer( desc );
            for ( const std::pair<string, string>& entry : listEntry )
            {
                writer.addBytes( entry.first, toBytes( entry.second ) );
            }
            return writer.writePackFile( packPath );
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
    using Internal     = TestPackManagerInternal;
    const string root  = test::makeScratchPath( "Packs" );
    const string base  = FileUtil::joinPath( root, "engine.pack" );
    const string patch = FileUtil::joinPath( root, "patch_1.pack" );
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
    SW_REQUIRE_TRUE( packs.mountPack( base, 1 ) );
    SW_REQUIRE_TRUE( packs.mountPatchPack( patch, 100, { "engine/textures/b.txt" } ) );

    SW_EXPECT_EQUAL( string( "new" ), Internal::read( packs, "engine/textures/a.txt" ) );
    SW_EXPECT_EQUAL( string( "new" ), Internal::read( packs, "textures/a.txt" ) ); // 도메인 없는 키도 패치를 본다
    SW_EXPECT_FALSE( packs.hasFile( "engine/textures/b.txt" ) );
    SW_EXPECT_FALSE( packs.hasFile( "textures/b.txt" ) ); // 지움도 같다
    SW_EXPECT_EQUAL( string( "kept" ), Internal::read( packs, "textures/c.txt" ) );
}

SW_TEST_CASE( PackManagerTest, FindsDlcDomainPacks )
{
    using Internal    = TestPackManagerInternal;
    const string root = test::makeScratchPath( "Packs" );
    const string dlc  = FileUtil::joinPath( root, "seasonal.pack" ); // 이름은 도메인을 말하지 않는다
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( dlc, "dlc/winter", {
                                                                 { "maps/snow.txt", "snow" }
    } ) );

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( dlc, 1 ) );
    SW_EXPECT_EQUAL( string( "snow" ), Internal::read( packs, "dlc/winter/maps/snow.txt" ) );
}

SW_TEST_CASE( PackManagerTest, RejectsPackMountedAgainstItsDomain )
{
    // 도메인 팩을 패치로, 패치 팩을 도메인 팩으로 붙이면 항목 경로의 뜻이 어긋난다 — 붙이지 않는다.
    using Internal      = TestPackManagerInternal;
    const string root   = test::makeScratchPath( "Packs" );
    const string domain = FileUtil::joinPath( root, "engine.pack" );
    const string patch  = FileUtil::joinPath( root, "patch_1.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( domain, "engine", {
                                                                { "a.txt", "a" }
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( patch, {
    },
                                          { { "engine/a.txt", "b" } } ) );

    SW_EXPECT_LOG( "Pack.MountDomainMismatch" );
    PackManager packs;
    SW_EXPECT_FALSE( packs.mountPatchPack( domain, 100, {} ) );
    SW_EXPECT_FALSE( packs.mountPack( patch, 1 ) );
}

SW_TEST_CASE( PackManagerTest, PackOutsideTheSearchPriorityNeedsAnExplicitPriority )
{
    // 우선순위 목록(game · common · engine · editor)에 없는 도메인은 기본 우선순위를 정할 수 없다 — 말없이 가장 낮게 붙이지 않는다.
    using Internal    = TestPackManagerInternal;
    const string root = test::makeScratchPath( "Packs" );
    const string dlc  = FileUtil::joinPath( root, "seasonal.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( dlc, "dlc/winter", {
                                                                 { "maps/snow.txt", "snow" }
    } ) );

    SW_EXPECT_LOG( "Pack.NoPriority" );
    PackManager packs;
    SW_EXPECT_FALSE( packs.mountPack( dlc ) );
    SW_EXPECT_TRUE( packs.mountPack( dlc, 1 ) ); // 밝힌 우선순위로는 붙는다
}
