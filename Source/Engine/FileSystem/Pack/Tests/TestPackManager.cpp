#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/Pack/PackManager.h"
#include "Engine/FileSystem/Pack/PackWriter.h"

// 팩 가상 파일 시스템 — 도메인 팩은 도메인을 뗀 경로로, 패치 팩은 전체 경로로 담는다. 도메인이 붙은 키와 없는 키 모두에서 패치의 교체 · 지움이 보이고, dlc 팩도 찾는다.

using namespace sw;

namespace
{
    struct TestPackManagerInternal
    {
        static vector<uint8> toBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }

        /** @brief @p packPath 에 항목들을 담은 팩을 씁니다. */
        static bool writePack( const string& packPath, const vector<std::pair<string, string>>& listEntry )
        {
            PackWriter writer( PackWriterDesc{} );
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
    SW_REQUIRE_TRUE( Internal::writePack( base, {
                                                    {"textures/a.txt",  "old"},
                                                    {"textures/b.txt", "gone"},
                                                    {"textures/c.txt", "kept"}
    } ) );
    SW_REQUIRE_TRUE( Internal::writePack( patch, {
                                                     { "engine/textures/a.txt", "new" }
    } ) );

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
    const string dlc  = FileUtil::joinPath( root, "dlc_winter.pack" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( root ) );
    SW_REQUIRE_TRUE( Internal::writePack( dlc, {
                                                   { "maps/snow.txt", "snow" }
    } ) );

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( dlc, 1 ) );
    SW_EXPECT_EQUAL( string( "snow" ), Internal::read( packs, "dlc/winter/maps/snow.txt" ) );
}
