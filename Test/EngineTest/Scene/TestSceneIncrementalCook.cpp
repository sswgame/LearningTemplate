#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/ScenePreloadList.h"

#include "TestFramework/TestFramework.h"

// 씬 · 프리팹 쿠킹이 작업마다 입력을 기록하고(`CookJob`), 기록이 새것이면 DDC 의 출력을 다시 쓰는지 본다(docs/09_Decisions.md 5-2 증분 쿠킹).

namespace
{
    struct TestSceneIncrementalCookInternal
    {
        /** @brief 씬 컴포넌트 하나(위치 @p x)를 가진 프리팹을 @p path 에 씁니다. */
        static void writePrefab( const sw::string& path, float32 x )
        {
            sw::GameObjectManager authoring;
            sw::GameObject*       pSource = authoring.createGameObject( sw::hashed_string( "Crate" ) );
            sw::SceneComponent*   pRoot   = pSource->addComponent<sw::SceneComponent>();
            pRoot->setLocalPosition( sw::float3( x, 0.0f, 0.0f ) );
            sw::PrefabAsset asset;
            asset.setFromGameObject( pSource );
            SW_EXPECT_TRUE( sw::FileUtil::ensureParentDirectoryExists( path ) );
            SW_EXPECT_TRUE( asset.saveToXMLFile( path ) );
            sw::ContentHash::clearCache(); // 같은 크기 · 시각으로 다시 쓴 파일도 새로 해시하게
        }

        /** @brief 프리팹 인스턴스 하나(@p prefabPath, 비면 상태를 실은 엔티티)를 둔 씬을 @p path 에 씁니다. */
        static void writeScene( const sw::string& path, const sw::string& prefabPath )
        {
            sw::SceneDocument doc;
            doc._name = sw::FileUtil::getFileNamePart( path );
            sw::SceneDocument::SceneObjectNode node{};
            node._name   = "Entity";
            node._fileID = 7;
            if ( prefabPath.empty() == false )
                node._prefab = prefabPath;
            else
            {
                sw::GameObjectManager scratch;
                sw::GameObject*       pObject = scratch.createGameObject( sw::hashed_string( "Entity" ) );
                SW_EXPECT_TRUE( pObject->addComponent<sw::SceneComponent>() != nullptr );
                node._embeddedXML = sw::ObjectStateSerializer::saveToXMLString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
            }
            doc._listSceneObjectNode.push_back( node );
            SW_EXPECT_TRUE( sw::FileUtil::ensureParentDirectoryExists( path ) );
            SW_EXPECT_TRUE( doc.saveXML( path ) );
        }

        static sw::vector<uint8> readBytes( const sw::string& path )
        {
            sw::vector<uint8> bytes;
            SW_EXPECT_TRUE( sw::FileUtil::readFile( path, bytes ) );
            return bytes;
        }
    };
} // namespace

/**
 * @brief [SceneCookTest] 프리팹 하나를 고치면 그 프리팹을 쓰는 씬만 다시 베이크한다 · 쿠커 버전을 올리면 기록이 낡는다 · DDC 를 지우고 다시 베이크해도 같은 바이트다
 * @details 씬 작업의 입력은 씬이 지어지며 읽은 모든 것이다(씬, 프리팹). 작업은 빈 에셋 캐시로 시작하므로 앞 씬이 읽어 캐시에 둔 프리팹도 다시 읽혀 기록된다.
 */
SW_TEST_CASE( SceneCookTest, PrefabEditCooksOnlyTheScenesThatUseIt )
{
    using Internal = TestSceneIncrementalCookInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::string root      = test::makeTempDirectory( "incremental_cook_root" );
    const sw::string cooked    = test::makeTempDirectory( "incremental_cook_out" );
    const sw::string recordDir = test::makeTempDirectory( "incremental_cook_record" );
    const sw::string ddcDir    = test::makeTempDirectory( "incremental_cook_ddc" );

    const sw::string prefabA = sw::FileUtil::joinPath( root, "game/demo/prefabs/a.prefab.xml" );
    const sw::string prefabB = sw::FileUtil::joinPath( root, "game/demo/prefabs/b.prefab.xml" );
    Internal::writePrefab( prefabA, 1.0f );
    Internal::writePrefab( prefabB, 2.0f );
    Internal::writeScene( sw::FileUtil::joinPath( root, "game/demo/maps/usesa.scene.xml" ), ::test::makeTempResourceID( prefabA ) );
    Internal::writeScene( sw::FileUtil::joinPath( root, "game/demo/maps/usesb.scene.xml" ), ::test::makeTempResourceID( prefabB ) );
    Internal::writeScene( sw::FileUtil::joinPath( root, "game/demo/maps/plain.scene.xml" ), {} );

    const sw::SceneEngineServices services = sw::SceneEngineServices::makeFromBoundEngine();
    const sw::CookStore           store( recordDir, ddcDir );
    uint32                        failedCount = 0;
    SW_EXPECT_EQUAL( 3u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    SW_EXPECT_EQUAL( 6u, store.getTally()._cookedCount ); // 씬 셋 + 씬마다 옆에 굽는 프리로드 목록 셋

    // 바뀐 것이 없으면 모두 DDC 에서 다시 쓴다.
    SW_EXPECT_EQUAL( 3u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 6u, store.getTally()._cookedCount );
    SW_EXPECT_EQUAL( 6u, store.getTally()._reusedCount );

    // 프리팹 a 를 고치면 그것을 쓰는 씬 하나만 다시 베이크한다.
    Internal::writePrefab( prefabA, 5.0f );
    SW_EXPECT_EQUAL( 3u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    SW_EXPECT_EQUAL( 8u, store.getTally()._cookedCount ); // 프리팹 a 를 쓰는 씬과 그 프리로드 목록
    SW_EXPECT_EQUAL( 10u, store.getTally()._reusedCount );

    // 쿠커 버전을 올리면 기록이 낡는다 — 모든 씬이 다시 베이크된다.
    sw::CookRecord   record;
    const sw::string usesBOutput = "game/demo/maps/usesb.scene.bin";
    SW_ASSERT_TRUE( sw::CookRecord::read( store.makeRecordPath( usesBOutput ), record ) );
    sw::CookJobDesc desc = sw::SceneCooker::makeCookJobDesc( *services._pTypeRegistry );
    SW_EXPECT_TRUE( record.isFresh( desc ) );
    desc._cookerVersion += 1;
    SW_EXPECT_FALSE( record.isFresh( desc ) );
    // 씬 b 의 입력에는 그 프리팹이 있고 다른 씬의 프리팹은 없다.
    bool bReadsPrefabB = false;
    bool bReadsPrefabA = false;
    for ( const sw::CookInput& input : record._listInput )
    {
        bReadsPrefabB = bReadsPrefabB || sw::StringUtil::endsWith( input._resourceID, "prefabs/b.prefab.xml", true );
        bReadsPrefabA = bReadsPrefabA || sw::StringUtil::endsWith( input._resourceID, "prefabs/a.prefab.xml", true );
    }
    SW_EXPECT_TRUE( bReadsPrefabB );
    SW_EXPECT_FALSE( bReadsPrefabA );

    // DDC 와 기록을 지우고 다시 베이크해도 같은 바이트다(쿠킹은 결정적이다 — DDC 의 키로 다시 쓰는 것이 옳다).
    sw::vector<sw::vector<uint8>> listBefore;
    for ( const utf8* pOutput : { "game/demo/maps/usesa.scene.bin", "game/demo/maps/usesb.scene.bin", "game/demo/maps/plain.scene.bin" } )
    {
        listBefore.push_back( Internal::readBytes( sw::FileUtil::joinPath( cooked, pOutput ) ) );
    }
    SW_ASSERT_TRUE( sw::FileUtil::removeDirectory( ddcDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeDirectory( recordDir ) );
    SW_EXPECT_EQUAL( 3u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 14u, store.getTally()._cookedCount );
    size_t outputIndex = 0;
    for ( const utf8* pOutput : { "game/demo/maps/usesa.scene.bin", "game/demo/maps/usesb.scene.bin", "game/demo/maps/plain.scene.bin" } )
    {
        SW_EXPECT_TRUE_MSG( Internal::readBytes( sw::FileUtil::joinPath( cooked, pOutput ) ) == listBefore[outputIndex], pOutput );
        ++outputIndex;
    }
    manager.shutdown();
}

/**
 * @brief [SceneCookTest] 소스가 사라진 씬의 옛 쿠킹본은 다음 쿠킹이 지운다(쿠킹 폴더의 파일은 모두 팩에 들어간다) — 기록이 없는 파일과 요청한 씬의 쿠킹본은 남는다
 * @details 쿠킹 프로세스가 끝나면 요청 목록이 사라지므로 두 번째 쿠킹은 새 저장소(같은 기록 폴더)로 한다.
 */
SW_TEST_CASE( SceneCookTest, CookRemovesOutputsOfVanishedSources )
{
    using Internal = TestSceneIncrementalCookInternal;
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::string root      = test::makeTempDirectory( "stale_cook_root" );
    const sw::string cooked    = test::makeTempDirectory( "stale_cook_out" );
    const sw::string recordDir = test::makeTempDirectory( "stale_cook_record" );
    const sw::string ddcDir    = test::makeTempDirectory( "stale_cook_ddc" );

    const sw::string keepSource = sw::FileUtil::joinPath( root, "game/demo/maps/keep.scene.xml" );
    const sw::string goneSource = sw::FileUtil::joinPath( root, "game/demo/maps/gone.scene.xml" );
    Internal::writeScene( keepSource, {} );
    Internal::writeScene( goneSource, {} );
    const sw::SceneEngineServices services    = sw::SceneEngineServices::makeFromBoundEngine();
    uint32                        failedCount = 0;
    {
        const sw::CookStore store( recordDir, ddcDir );
        SW_EXPECT_EQUAL( 2u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
        SW_EXPECT_EQUAL( 0u, failedCount );
    }
    const sw::string keepOutput  = sw::FileUtil::joinPath( cooked, "game/demo/maps/keep.scene.bin" );
    const sw::string goneOutput  = sw::FileUtil::joinPath( cooked, "game/demo/maps/gone.scene.bin" );
    const sw::string gonePreload = sw::ScenePreloadList::makeListPath( goneOutput );
    const sw::string strayFile   = sw::FileUtil::joinPath( cooked, "game/demo/maps/note.txt" ); // 쿠커가 만든 적 없는 파일
    SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( strayFile, "keep me" ) );
    SW_ASSERT_TRUE( sw::FileUtil::isRegularFile( goneOutput ) );
    SW_ASSERT_TRUE( sw::FileUtil::isRegularFile( gonePreload ) );

    SW_ASSERT_TRUE( sw::FileUtil::removeFile( goneSource ) );
    const sw::CookStore store( recordDir, ddcDir );
    SW_EXPECT_EQUAL( 1u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    uint32 removeFailedCount = 0;
    SW_EXPECT_EQUAL( 2u, store.removeUnrequestedOutputs( cooked, removeFailedCount ) ); // 사라진 씬의 쿠킹본과 프리로드 목록
    SW_EXPECT_EQUAL( 0u, removeFailedCount );
    SW_EXPECT_FALSE( sw::FileUtil::isRegularFile( goneOutput ) );
    SW_EXPECT_FALSE( sw::FileUtil::isRegularFile( gonePreload ) );
    SW_EXPECT_TRUE( sw::FileUtil::isRegularFile( keepOutput ) );
    SW_EXPECT_TRUE( sw::FileUtil::isRegularFile( strayFile ) );
    SW_EXPECT_EQUAL( 0u, store.removeUnrequestedOutputs( cooked, removeFailedCount ) ); // 한 번 치웠으면 더 지울 것이 없다
    manager.shutdown();
}

/**
 * @brief [SceneCookTest] 프리팹 쿠킹도 프리팹마다 기록을 남긴다 — 바뀐 프리팹만 다시 베이크하고, 나머지는 DDC 에서 다시 쓴다
 */
SW_TEST_CASE( SceneCookTest, PrefabCookReusesUnchangedPrefabs )
{
    using Internal             = TestSceneIncrementalCookInternal;
    const sw::string    root   = test::makeTempDirectory( "incremental_prefab_root" );
    const sw::string    cooked = test::makeTempDirectory( "incremental_prefab_out" );
    const sw::CookStore store( test::makeTempDirectory( "incremental_prefab_record" ), test::makeTempDirectory( "incremental_prefab_ddc" ) );
    Internal::writePrefab( sw::FileUtil::joinPath( root, "a.prefab.xml" ), 1.0f );
    Internal::writePrefab( sw::FileUtil::joinPath( root, "b.prefab.xml" ), 2.0f );

    uint32 failedCount = 0;
    SW_EXPECT_EQUAL( 2u, sw::PrefabCache::cookAllPrefabs( root, cooked, store, failedCount ) );
    const sw::vector<uint8> cookedB = Internal::readBytes( sw::FileUtil::joinPath( cooked, "b.prefab.bin" ) );
    Internal::writePrefab( sw::FileUtil::joinPath( root, "a.prefab.xml" ), 3.0f );
    SW_EXPECT_EQUAL( 2u, sw::PrefabCache::cookAllPrefabs( root, cooked, store, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    SW_EXPECT_EQUAL( 3u, store.getTally()._cookedCount );
    SW_EXPECT_EQUAL( 1u, store.getTally()._reusedCount );
    SW_EXPECT_TRUE( Internal::readBytes( sw::FileUtil::joinPath( cooked, "b.prefab.bin" ) ) == cookedB );
}
