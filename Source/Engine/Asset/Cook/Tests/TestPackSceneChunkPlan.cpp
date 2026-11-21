// PackSceneChunkPlanTest — 씬 단위 청크 나누기(언리얼 청크 pak · 유니티 Addressables 자리): 결정성, 씬이 쓰는 에셋이 기동 필수나 그 씬의 청크에 반드시 있는지(빠지면 쿠킹 오류),
// 나누기가 씬 청크를 따르는지, 청크 목록(`packchunks.xml`) 읽고 쓰기.
#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/PackCooker.h"
#include "Engine/Asset/Cook/PackSceneChunkPlan.h"
#include "Engine/Content/Pack/PackChunkManifest.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Content/Pack/PackWriter.h"

using namespace sw;

namespace
{
    struct PackSceneChunkPlanTestUtil
    {
        static constexpr const utf8* kSceneA = "game/playground/maps/alpha.scene.xml";
        static constexpr const utf8* kSceneB = "game/playground/maps/beta.scene.xml";

        /** @brief 두 씬 — 각자만 쓰는 에셋, 둘 다 쓰는 에셋, 기동 필수(엔진 셰이더)를 함께 쓴다. */
        static vector<PackSceneReach> makeScenes()
        {
            vector<PackSceneReach> listScene( 2 );
            listScene[0]._sceneID    = kSceneA;
            listScene[0]._setReached = { kSceneA, "game/playground/textures/alpha.dds", "common/meshes/rock.mesh", "engine/shaders/pbr.hlsl" };
            listScene[1]._sceneID    = kSceneB;
            listScene[1]._setReached = { kSceneB, "game/playground/textures/beta.dds", "common/meshes/rock.mesh", "engine/shaders/pbr.hlsl" };
            return listScene;
        }

        static unordered_set<string> makeStartup() { return { "engine/shaders/pbr.hlsl", "game/playground/data/gamesettings.xml" }; }

        /** @brief 계획에서 에셋의 청크입니다(계획 밖이면 `-`). */
        static string findChunk( const PackSceneChunkPlan& plan, const string& assetID )
        {
            const auto itChunk = plan._mapAssetChunk.find( assetID );
            return itChunk != plan._mapAssetChunk.end() ? itChunk->second : string( "-" );
        }

        /** @brief 두 계획이 같은 에셋을 같은 청크에 두면 true 입니다. */
        static bool isSamePlan( const PackSceneChunkPlan& left, const PackSceneChunkPlan& right )
        {
            if ( left._mapAssetChunk.size() != right._mapAssetChunk.size() )
                return false;
            for ( const auto& [assetID, chunk] : left._mapAssetChunk )
            {
                if ( findChunk( right, assetID ) != chunk )
                    return false;
            }
            return true;
        }
    };
} // namespace

/**
 * @brief [PackSceneChunkPlanTest] 한 씬만 쓰는 것은 그 씬의 청크, 둘 이상이 쓰는 것은 공유 청크, 기동 필수는 계획 밖이다 — 씬 순서가 달라도 같은 계획이다
 */
SW_TEST_CASE( PackSceneChunkPlanTest, PlanIsDeterministicAndSeparatesSharedAssets )
{
    using Util                                   = PackSceneChunkPlanTestUtil;
    const vector<PackSceneReach>       listScene = Util::makeScenes();
    const expected<PackSceneChunkPlan> plan      = PackSceneChunkPlan::plan( Util::makeStartup(), listScene );
    SW_REQUIRE_TRUE( plan.has_value() );
    const vector<PackSceneReach>       listReversed( listScene.rbegin(), listScene.rend() );
    const expected<PackSceneChunkPlan> reversedPlan = PackSceneChunkPlan::plan( Util::makeStartup(), listReversed );
    SW_REQUIRE_TRUE( reversedPlan.has_value() );
    SW_EXPECT_TRUE( Util::isSamePlan( *plan, *reversedPlan ) );

    const string chunkA = PackSceneChunkPlan::makeSceneChunkName( Util::kSceneA );
    SW_EXPECT_EQUAL( string( "scene_playground_alpha" ), chunkA );
    SW_EXPECT_EQUAL( chunkA, Util::findChunk( *plan, "game/playground/textures/alpha.dds" ) );
    SW_EXPECT_EQUAL( chunkA, Util::findChunk( *plan, Util::kSceneA ) );
    SW_EXPECT_EQUAL( string( PackSceneChunkPlan::kSharedChunk ), Util::findChunk( *plan, "common/meshes/rock.mesh" ) );
    SW_EXPECT_EQUAL( string( "-" ), Util::findChunk( *plan, "engine/shaders/pbr.hlsl" ) ); // 기동 필수

    // 파일 이름에 쓸 수 없는 글자를 `_` 로 바꾸면 같은 이름이 되는 두 씬 — 섞이지 않게 쿠킹을 멈춘다.
    vector<PackSceneReach> listClash( 2 );
    listClash[0]._sceneID = "game/playground/maps/a-b.scene.xml";
    listClash[1]._sceneID = "game/playground/maps/a_b.scene.xml";
    SW_EXPECT_FALSE( PackSceneChunkPlan::plan( Util::makeStartup(), listClash ).has_value() );
}

/**
 * @brief [PackSceneChunkPlanTest] 씬이 쓰는 에셋이 기동 필수도 그 씬의 청크도 아닌 곳에 있거나 팩에 없으면 빠진 것으로 알린다 — 계획대로 놓였으면 빠진 것이 없다
 */
SW_TEST_CASE( PackSceneChunkPlanTest, UncoveredAssetsAreReported )
{
    using Util                                   = PackSceneChunkPlanTestUtil;
    const vector<PackSceneReach>       listScene = Util::makeScenes();
    const expected<PackSceneChunkPlan> plan      = PackSceneChunkPlan::plan( Util::makeStartup(), listScene );
    SW_REQUIRE_TRUE( plan.has_value() );

    // 놓인 청크 — 계획의 것, 기동 필수는 기본 청크(빈 글).
    unordered_map<string, string> mapPlaced = plan->_mapAssetChunk;
    for ( const string& assetID : Util::makeStartup() )
    {
        mapPlaced.emplace( assetID, string{} );
    }
    const unordered_set<string>  setStartup{ string{} };
    const vector<PackSceneEntry> listSceneChunks = PackSceneChunkPlan::makeSceneChunks( listScene, mapPlaced, setStartup );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSceneChunks.size() );
    SW_EXPECT_EQUAL( string( Util::kSceneA ), listSceneChunks[0]._sceneID );
    SW_EXPECT_TRUE( listSceneChunks[0]._listChunk == ( vector<string>{ "scene_playground_alpha", "shared" } ) );
    SW_EXPECT_TRUE( PackSceneChunkPlan::findUncovered( listScene, mapPlaced, setStartup, listSceneChunks ).empty() );

    // 알파만 쓰는 텍스처가 베타의 청크에 놓였다(잘못 놓음) · 공유 메시가 팩에서 빠졌다.
    unordered_map<string, string> mapWrong         = mapPlaced;
    mapWrong["game/playground/textures/alpha.dds"] = "scene_playground_beta";
    mapWrong.erase( "common/meshes/rock.mesh" );
    const vector<PackChunkIssue> listIssue = PackSceneChunkPlan::findUncovered( listScene, mapWrong, setStartup, listSceneChunks );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listIssue.size() ); // 알파의 텍스처, 두 씬의 메시
    SW_EXPECT_EQUAL( string( Util::kSceneA ), listIssue[0]._sceneID );
    SW_EXPECT_EQUAL( string( "common/meshes/rock.mesh" ), listIssue[0]._assetID );
    SW_EXPECT_TRUE( listIssue[0]._chunk.empty() );
    SW_EXPECT_EQUAL( string( "game/playground/textures/alpha.dds" ), listIssue[1]._assetID );
    SW_EXPECT_EQUAL( string( "scene_playground_beta" ), listIssue[1]._chunk );
}

/**
 * @brief [PackSceneChunkPlanTest] 나누기는 씬 청크를 따르고(패턴 청크가 먼저 이긴다) 입력 순서와 상관없이 같은 몫이다. 씬 청크가 패턴 청크 이름과 같으면 오류다
 */
SW_TEST_CASE( PackSceneChunkPlanTest, SplitFollowsSceneChunksDeterministically )
{
    PackSplitConfig split;
    split._maxPackBytes = 1024 * 1024;
    PackChunkRule& rule = split._listChunkRule.emplace_back();
    rule._name          = "music";
    rule._listPattern.push_back( "audio/music/**" );

    const vector<PackSplitEntry> listEntry{
        { "maps/alpha.scene.xml", 100},
        { "maps/alpha.scene.bin", 100},
        {   "textures/alpha.dds", 100},
        {    "textures/rock.dds", 100},
        {"audio/music/alpha.ogg", 100},
        {"data/gamesettings.xml", 100}
    };
    const unordered_map<string, string> mapAssigned{
        { "maps/alpha.scene.xml", "scene_playground_alpha"},
        { "maps/alpha.scene.bin", "scene_playground_alpha"},
        {   "textures/alpha.dds", "scene_playground_alpha"},
        {    "textures/rock.dds",                 "shared"},
        {"audio/music/alpha.ogg", "scene_playground_alpha"}
    };
    const expected<vector<PackPart>> parts = PackCooker::splitIntoParts( listEntry, split, mapAssigned );
    SW_REQUIRE_TRUE( parts.has_value() );
    const vector<PackSplitEntry>     listReversed( listEntry.rbegin(), listEntry.rend() );
    const expected<vector<PackPart>> reversedParts = PackCooker::splitIntoParts( listReversed, split, mapAssigned );
    SW_REQUIRE_TRUE( reversedParts.has_value() );
    SW_REQUIRE_EQUAL( size_t{ 4 }, parts->size() ); // 기본 · music · scene_playground_alpha · shared
    SW_REQUIRE_EQUAL( parts->size(), reversedParts->size() );
    for ( size_t index = 0; index < parts->size(); ++index )
    {
        SW_EXPECT_TRUE( ( *parts )[index]._chunk == ( *reversedParts )[index]._chunk );
        SW_EXPECT_TRUE( ( *parts )[index]._listPathHash == ( *reversedParts )[index]._listPathHash );
    }
    SW_EXPECT_TRUE( ( *parts )[0].contains( PackStreamWriter::makePathHash( "data/gamesettings.xml" ) ) );
    SW_EXPECT_TRUE( ( *parts )[1]._chunk == "music" && ( *parts )[1].contains( PackStreamWriter::makePathHash( "audio/music/alpha.ogg" ) ) ); // 패턴이 이긴다
    SW_EXPECT_TRUE( ( *parts )[2]._chunk == "scene_playground_alpha" && ( *parts )[2]._listPathHash.size() == 3 );
    SW_EXPECT_TRUE( ( *parts )[3]._chunk == "shared" && ( *parts )[3].contains( PackStreamWriter::makePathHash( "textures/rock.dds" ) ) );

    const unordered_map<string, string> mapClash{
        { "textures/alpha.dds", "music" }
    };
    SW_EXPECT_FALSE( PackCooker::splitIntoParts( listEntry, split, mapClash ).has_value() );
}

/**
 * @brief [PackSceneChunkPlanTest] 청크 목록은 쓴 그대로 읽히고, 목록에 든 팩만 미룬 팩이며, 씬이 마운트할 팩을 청크 순서로 준다. 폴더를 벗어나는 팩 이름은 읽기 오류,
 *        합친 목록에 없는 청크를 가리키는 씬은 열 때 오류, 같은 청크를 두 목록이 적으면 합치기 오류다
 */
SW_TEST_CASE( PackSceneChunkPlanTest, ManifestRoundTripsAndNamesScenePacks )
{
    PackChunkManifest gameManifest;
    gameManifest._listChunk.push_back( PackChunkFiles{ "game_playground.scene_playground_alpha", { "game_playground.scene_playground_alpha.0.pack" } } );
    gameManifest._listScene.push_back( PackSceneEntry{
        "game/playground/maps/alpha.scene.xml",
        { "common.shared", "game_playground.scene_playground_alpha" }
    } );
    PackChunkManifest commonManifest;
    commonManifest._listChunk.push_back( PackChunkFiles{ "common.shared", { "common.shared.0.pack" } } );

    const string      text = PackChunkManifest::makeText( gameManifest );
    PackChunkManifest read;
    SW_REQUIRE_TRUE( PackChunkManifest::parse( text, "game", read ) );
    SW_EXPECT_EQUAL( text, PackChunkManifest::makeText( read ) );

    SW_EXPECT_LOG( "PackChunkManifest.UnknownChunk" ); // 공용 목록을 합치기 전에는 `common.shared` 를 모른다
    vector<string> listPack;
    SW_EXPECT_FALSE( read.appendScenePackFiles( "game/playground/maps/alpha.scene.xml", listPack ) );

    SW_REQUIRE_TRUE( read.merge( commonManifest, "common" ) );
    SW_EXPECT_TRUE( read.isDeferredPack( "COMMON.SHARED.0.PACK" ) );
    SW_EXPECT_FALSE( read.isDeferredPack( "common.pack" ) );
    listPack.clear();
    SW_EXPECT_TRUE( read.appendScenePackFiles( "Game/Playground/Maps/Alpha.scene.xml", listPack ) );
    SW_EXPECT_TRUE( listPack == ( vector<string>{ "common.shared.0.pack", "game_playground.scene_playground_alpha.0.pack" } ) );
    listPack.clear();
    SW_EXPECT_TRUE( read.appendScenePackFiles( "game/playground/maps/editortest.scene.xml", listPack ) ); // 기동 필수 씬 — 목록에 없다
    SW_EXPECT_TRUE( listPack.empty() );

    SW_EXPECT_LOG( "PackChunkManifest.DuplicateChunk" );
    SW_EXPECT_FALSE( read.merge( commonManifest, "common again" ) );

    SW_EXPECT_EQUAL( string( "packchunks.game_playground.xml" ), PackChunkManifest::makeFileName( "game/playground" ) );
    SW_EXPECT_TRUE( PackChunkManifest::isFileName( "PackChunks.Common.xml" ) );
    SW_EXPECT_FALSE( PackChunkManifest::isFileName( "packchunks.xml" ) );

    SW_EXPECT_LOG( "PackChunkManifest.PackName" );
    PackChunkManifest bad;
    SW_EXPECT_FALSE( PackChunkManifest::parse( "<PackChunks><Chunk name=\"x\"><Pack>../escape.pack</Pack></Chunk></PackChunks>", "test", bad ) );
}

/**
 * @brief [PackSceneChunkPlanTest] 청크 목록은 도메인마다 따로 쓴다 — 게임 A 를 굽고 게임 B 를 구워도 A 의 목록이 남고, 폴더의 목록을 합치면 두 게임의 씬이 모두 있다
 */
SW_TEST_CASE( PackSceneChunkPlanTest, CookingAnotherGameKeepsTheChunkManifest )
{
    const string root = test::makeScratchPath( "PackChunkManifests" );

    PackChunkManifest cookA;
    cookA._listChunk.push_back( PackChunkFiles{ "common.shared", { "common.shared.0.pack" } } );
    cookA._listChunk.push_back( PackChunkFiles{ "game_a.scene_a_one", { "game_a.scene_a_one.0.pack" } } );
    cookA._listScene.push_back( PackSceneEntry{
        "game/a/maps/one.scene.xml",
        { "common.shared", "game_a.scene_a_one" }
    } );
    SW_REQUIRE_TRUE( PackCooker::writeChunkManifests( root, { "engine", "common", "game/a" }, cookA ) );

    PackChunkManifest cookB;
    cookB._listChunk.push_back( PackChunkFiles{ "game_b.scene_b_two", { "game_b.scene_b_two.0.pack" } } );
    cookB._listScene.push_back( PackSceneEntry{ "game/b/maps/two.scene.xml", { "game_b.scene_b_two" } } );
    SW_REQUIRE_TRUE( PackCooker::writeChunkManifests( root, { "engine", "common", "game/b" }, cookB ) );

    // A 의 목록은 그대로다. 공용 · 엔진은 마지막 쿠킹(B)의 것이다(팩 파일과 같은 단위).
    string textA;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, "packchunks.game_a.xml" ) ), textA ) );
    PackChunkManifest readA;
    SW_REQUIRE_TRUE( PackChunkManifest::parse( textA, "a", readA ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, readA._listScene.size() );
    SW_EXPECT_EQUAL( string( "game/a/maps/one.scene.xml" ), readA._listScene[0]._sceneID );

    PackChunkManifest merged;
    vector<string>    listFile;
    SW_REQUIRE_TRUE( FileUtil::collectFiles( FileUtil::toDiskPath( root ), "", listFile, false ) );
    uint32 manifestCount = 0;
    for ( const string& path : listFile )
    {
        string_view fileName;
        FileUtil::getFileNamePart( path, fileName );
        if ( PackChunkManifest::isFileName( fileName ) == false )
            continue;
        ++manifestCount;
        string            text;
        PackChunkManifest one;
        SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( path ), text ) && PackChunkManifest::parse( text, path, one ) && merged.merge( one, path ) );
    }
    SW_EXPECT_EQUAL( 4u, manifestCount ); // engine · common · game_a · game_b
    SW_EXPECT_EQUAL( size_t{ 2 }, merged._listScene.size() );
    vector<string> listPack;
    SW_EXPECT_TRUE( merged.appendScenePackFiles( "game/b/maps/two.scene.xml", listPack ) );
    SW_EXPECT_TRUE( listPack == ( vector<string>{ "game_b.scene_b_two.0.pack" } ) );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( FileUtil::toDiskPath( root ) ) );
}
