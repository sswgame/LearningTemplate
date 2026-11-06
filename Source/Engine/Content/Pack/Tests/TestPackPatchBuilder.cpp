#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackPatch.h"
#include "Engine/Content/Pack/PackPatchBuilder.h"
#include "Engine/Content/Pack/PackWriter.h"

using namespace sw;

namespace
{
    struct PackPatchBuilderTestUtil
    {
        /** @brief @p folder 에 도메인 팩 하나를 씁니다. */
        static bool writePack( const string& folder, string_view fileName, string_view domain, const vector<std::pair<string, string>>& listEntry )
        {
            if ( FileUtil::ensureDirectoryExists( folder ) == false )
                return false;
            PackWriterDesc desc;
            desc._mountDomain = string{ domain };
            PackWriter writer( desc );
            for ( const std::pair<string, string>& entry : listEntry )
            {
                writer.addBytes( entry.first, vector<uint8>( entry.second.begin(), entry.second.end() ) );
            }
            return writer.writePackFile( FileUtil::joinPath( folder, fileName ) );
        }

        static PackPatchBuildParams makeParams( const string& newPacks, const string& record, const string& output, string_view build )
        {
            PackPatchBuildParams params{};
            params._packsDirectory     = newPacks;
            params._previousRecordPath = record;
            params._outputDirectory    = output;
            params._newBuild           = string{ build };
            return params;
        }

        static string read( const PackManager& packs, string_view path )
        {
            string text;
            return packs.readTextFile( path, text ) ? text : string{ "<missing>" };
        }
    };
    using Util = PackPatchBuilderTestUtil;
} // namespace

/** @brief [PackPatchBuilderTest] 빌드 번호는 파일 이름에 들어간다 — 경로 글자를 받지 않는다 */
SW_TEST_CASE( PackPatchBuilderTest, BuildNameIsFileNameSafe )
{
    SW_EXPECT_TRUE( PackPatchBuilder::isValidBuildName( "1.2.3-rc_1" ) );
    SW_EXPECT_FALSE( PackPatchBuilder::isValidBuildName( "" ) );
    SW_EXPECT_FALSE( PackPatchBuilder::isValidBuildName( "../up" ) );
    SW_EXPECT_FALSE( PackPatchBuilder::isValidBuildName( "a b" ) );
    SW_EXPECT_FALSE( PackPatchBuilder::isValidBuildName( "a\\b" ) );
    SW_EXPECT_EQUAL( string( "patch_7.pack" ), PackPatchBuilder::makePatchPackFileName( "7" ) );
    SW_EXPECT_EQUAL( string( "patch_7.packpatch.xml" ), PackPatchBuilder::makeManifestFileName( "7" ) );
}

/** @brief [PackPatchBuilderTest] 내용 목록은 전체 가상 경로 사전순이고 내용 해시를 담는다 */
SW_TEST_CASE( PackPatchBuilderTest, CollectsFullPathsInOrder )
{
    const string folder = test::makeScratchPath( "Base" );
    SW_REQUIRE_TRUE( Util::writePack( folder, "game.pack", "game", {
                                                                       {"z.txt", "zz"},
                                                                       {"a.txt", "aa"}
    } ) );
    SW_REQUIRE_TRUE( Util::writePack( folder, "engine.pack", "engine", {
                                                                           { "b.txt", "bb" }
    } ) );

    vector<PackContentEntry> listEntry;
    SW_REQUIRE_TRUE( PackPatchBuilder::collectBuildEntries( folder, listEntry ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( listEntry.size() ) );
    SW_EXPECT_EQUAL( string( "engine/b.txt" ), listEntry[0]._path );
    SW_EXPECT_EQUAL( string( "game/a.txt" ), listEntry[1]._path );
    SW_EXPECT_EQUAL( string( "game/z.txt" ), listEntry[2]._path );
    const string text = "bb";
    SW_EXPECT_EQUAL( PackContentEntry::computeHash( vector<uint8>( text.begin(), text.end() ) ), listEntry[0]._contentHash );
}

/** @brief [PackPatchBuilderTest] 같은 전체 경로가 두 팩에 있으면 어느 쪽이 배포되는지 정할 수 없어 오류다 */
SW_TEST_CASE( PackPatchBuilderTest, SamePathInTwoPacksIsRejected )
{
    SW_EXPECT_LOG( "PackPatchBuilder.PathMoreOnePack" );
    const string folder = test::makeScratchPath( "Dup" );
    SW_REQUIRE_TRUE( Util::writePack( folder, "one.pack", "game", {
                                                                      { "a.txt", "1" }
    } ) );
    SW_REQUIRE_TRUE( Util::writePack( folder, "two.pack", "game", {
                                                                      { "a.txt", "2" }
    } ) );
    vector<PackContentEntry> listEntry;
    SW_EXPECT_FALSE( PackPatchBuilder::collectBuildEntries( folder, listEntry ) );
}

/** @brief [PackPatchBuilderTest] 없는 폴더 · 패치 목록이 있는 폴더(이미 패치된 설치본)에서는 만들지 않는다 */
SW_TEST_CASE( PackPatchBuilderTest, RefusesFoldersThatAreNotABaseBuild )
{
    SW_EXPECT_LOG( "PackPatchBuilder.PackFolderDoesNot" );
    SW_EXPECT_LOG( "PackPatchBuilder.PackFolderHoldsPatch" );
    vector<PackContentEntry> listEntry;
    SW_EXPECT_FALSE( PackPatchBuilder::collectBuildEntries( test::makeScratchPath( "NoSuchFolder" ), listEntry ) );

    const string folder = test::makeScratchPath( "Patched" );
    SW_REQUIRE_TRUE( Util::writePack( folder, "game.pack", "game", {
                                                                       { "a.txt", "1" }
    } ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, PackPatchBuilder::makeManifestFileName( "2" ) ), "<x/>" ) );
    SW_EXPECT_FALSE( PackPatchBuilder::collectBuildEntries( folder, listEntry ) );
}

/** @brief [PackPatchBuilderTest] 두 빌드의 차이가 패치가 되고, 기본 팩에 얹으면 새 빌드의 내용이 보인다 */
SW_TEST_CASE( PackPatchBuilderTest, PatchTurnsTheOldBuildIntoTheNewOne )
{
    const string root       = test::makeScratchPath( "Chain" );
    const string oldFolder  = FileUtil::joinPath( root, "old" );
    const string newFolder  = FileUtil::joinPath( root, "new" );
    const string output     = FileUtil::joinPath( root, "out" );
    const string recordPath = FileUtil::joinPath( root, "1.packbuild.xml" );
    SW_REQUIRE_TRUE( Util::writePack( oldFolder, "game.pack", "game", {
                                                                          {"keep.txt", "same"},
                                                                          {"edit.txt",  "old"},
                                                                          {"gone.txt",  "bye"}
    } ) );
    SW_REQUIRE_TRUE( Util::writePack( newFolder, "game.pack", "game", {
                                                                          {"keep.txt", "same"},
                                                                          {"edit.txt",  "new"},
                                                                          {"made.txt",   "hi"}
    } ) );
    SW_REQUIRE_TRUE( PackPatchBuilder::writeBuildRecord( oldFolder, "1", recordPath ) );

    SW_REQUIRE_TRUE( PackPatchBuilder::makePatch( Util::makeParams( newFolder, recordPath, output, "2" ) ) );

    string manifestText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( output, PackPatchBuilder::makeManifestFileName( "2" ) ), manifestText ) );
    PackPatchManifest manifest;
    SW_REQUIRE_TRUE( PackPatchManifest::parse( manifestText, "manifest", manifest ) );
    SW_EXPECT_EQUAL( string( "1" ), manifest._fromBuild );
    SW_EXPECT_EQUAL( string( "2" ), manifest._toBuild );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( manifest._listRemovedPath.size() ) );
    SW_EXPECT_EQUAL( string( "game/gone.txt" ), manifest._listRemovedPath[0] );
    SW_EXPECT_TRUE( manifest._signatureHex.empty() ); // 서명 제공자가 없으면 서명하지 않는다

    PackManager packs;
    SW_REQUIRE_TRUE( packs.mountPack( FileUtil::joinPath( oldFolder, "game.pack" ), 1 ) );
    SW_REQUIRE_TRUE( packs.mountPatchPack( FileUtil::joinPath( output, manifest._packFileName ), 100, manifest._listRemovedPath ) );
    SW_EXPECT_EQUAL( string( "same" ), Util::read( packs, "game/keep.txt" ) );
    SW_EXPECT_EQUAL( string( "new" ), Util::read( packs, "game/edit.txt" ) );
    SW_EXPECT_EQUAL( string( "hi" ), Util::read( packs, "game/made.txt" ) );
    SW_EXPECT_FALSE( packs.hasFile( "game/gone.txt" ) );

    // 함께 쓴 새 내용 목록은 다음 패치의 이전 기록이다
    string recordText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( output, "2.packbuild.xml" ), recordText ) );
    PackBuildRecord record;
    SW_REQUIRE_TRUE( PackBuildRecord::parse( recordText, "record", record ) );
    SW_EXPECT_EQUAL( string( "2" ), record._build );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( record._listEntry.size() ) );
}

/** @brief [PackPatchBuilderTest] 내용이 같으면 패치를 만들지 않는다 — 빈 패치는 배포할 이유가 없다 */
SW_TEST_CASE( PackPatchBuilderTest, SameContentIsRejected )
{
    SW_EXPECT_LOG( "PackPatchBuilder.BuildsSameContentNothing" );
    const string root       = test::makeScratchPath( "Same" );
    const string folder     = FileUtil::joinPath( root, "packs" );
    const string recordPath = FileUtil::joinPath( root, "1.packbuild.xml" );
    SW_REQUIRE_TRUE( Util::writePack( folder, "game.pack", "game", {
                                                                       { "a.txt", "1" }
    } ) );
    SW_REQUIRE_TRUE( PackPatchBuilder::writeBuildRecord( folder, "1", recordPath ) );
    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( folder, recordPath, FileUtil::joinPath( root, "out" ), "2" ) ) );
}

/** @brief [PackPatchBuilderTest] 입력이 어긋나면 만들지 않는다: 같은 빌드 번호, 쓸 수 없는 이름, 없는 기록, 패치를 쓰지 않는 빌드 */
SW_TEST_CASE( PackPatchBuilderTest, BadInputIsRejected )
{
    SW_EXPECT_LOG( "PackPatchBuilder.NewBuildSamePrevious" );
    SW_EXPECT_LOG( "PackPatchBuilder.BuildNameNotUsable" );
    SW_EXPECT_LOG( "PackPatchBuilder.PreviousBuildRecordCould" );
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "PackPatchBuilder.PreviousBuildRecordSays" );
    SW_EXPECT_LOG( "PackPatchBuilder.MakingPatchNeedsNew" );
    const string root       = test::makeScratchPath( "Bad" );
    const string folder     = FileUtil::joinPath( root, "packs" );
    const string output     = FileUtil::joinPath( root, "out" );
    const string recordPath = FileUtil::joinPath( root, "1.packbuild.xml" );
    const string noPatching = FileUtil::joinPath( root, "none.packbuild.xml" );
    SW_REQUIRE_TRUE( Util::writePack( folder, "game.pack", "game", {
                                                                       { "a.txt", "1" }
    } ) );
    SW_REQUIRE_TRUE( PackPatchBuilder::writeBuildRecord( folder, "1", recordPath ) );
    SW_REQUIRE_TRUE( PackPatchBuilder::writeNoPatchingRecord( noPatching ) );

    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( folder, recordPath, output, "1" ) ) );
    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( folder, recordPath, output, "../2" ) ) );
    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( folder, FileUtil::joinPath( root, "absent.xml" ), output, "2" ) ) );
    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( folder, noPatching, output, "2" ) ) );
    SW_EXPECT_FALSE( PackPatchBuilder::makePatch( Util::makeParams( "", recordPath, output, "2" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( output ) ); // 실패한 입력은 아무것도 쓰지 않는다
}
