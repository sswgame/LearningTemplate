#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

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
