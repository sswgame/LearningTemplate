#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Resource/Cook/CookStore.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/ScenePreloadList.h"

#include "TestFramework/TestFramework.h"

// 씬 프리로드 목록(`<씬>.preload.json`) — 컴포넌트의 에셋 경로 프로퍼티를 모은 목록이 정렬 · 중복 없이 나오고, 파일로 왕복하며, 쿠킹이 씬 옆에 굽는다(PreloadAndLod P8).

namespace
{
    /** @brief 메시 경로 @p meshPath · 머티리얼 경로 @p materialPath(비면 적지 않는다)를 가진 메시 오브젝트를 @p manager 에 만듭니다. */
    sw::GameObject* addMeshObject( sw::GameObjectManager& manager, const utf8* pName, sw::string_view meshPath, sw::string_view materialPath )
    {
        sw::GameObject*    pObject = manager.createGameObject( sw::hashed_string( pName ) );
        sw::MeshComponent* pMesh   = pObject->addComponent<sw::MeshComponent>();
        pMesh->setMeshID( meshPath );
        if ( materialPath.empty() == false )
            pMesh->setMaterialPath( materialPath );
        return pObject;
    }
} // namespace

/**
 * @brief [ScenePreloadListTest] 모은 목록은 오름차순 · 중복 없음이고, 확장자 없는 내장 id 와 씬 경로는 빠진다
 */
SW_TEST_CASE( ScenePreloadListTest, CollectsAssetPathsSortedAndUnique )
{
    sw::GameObjectManager manager;
    addMeshObject( manager, "A", "game/demo/models/b.mesh", "game/demo/materials/x.material" );
    addMeshObject( manager, "B", "game/demo/models/a.mesh", "game/demo/materials/x.material" ); // 같은 머티리얼 — 한 번만
    addMeshObject( manager, "C", "cube", {} );                                                  // 내장 메시 id 는 파일이 아니다

    sw::vector<sw::string> listPath;
    sw::ScenePreloadList::collect( manager, listPath );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( listPath, sw::string( "game/demo/models/a.mesh" ) ) );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( listPath, sw::string( "game/demo/models/b.mesh" ) ) );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( listPath, sw::string( "game/demo/materials/x.material" ) ) );
    SW_EXPECT_FALSE( sw::VectorUtil::contains( listPath, sw::string( "cube" ) ) );
    size_t materialCount = 0;
    for ( size_t index = 0; index < listPath.size(); ++index )
    {
        materialCount += listPath[index] == "game/demo/materials/x.material" ? 1u : 0u;
        if ( index > 0 )
            SW_EXPECT_TRUE( listPath[index - 1] < listPath[index] ); // 오름차순 · 중복 없음
    }
    SW_EXPECT_EQUAL( size_t( 1 ), materialCount );

    SW_EXPECT_STREQ( "game/demo/maps/x.preload.json", sw::ScenePreloadList::makeListPath( "game/demo/maps/x.scene.xml" ).c_str() );
    SW_EXPECT_STREQ( "game/demo/maps/x.preload.json", sw::ScenePreloadList::makeListPath( "game/demo/maps/x.scene.bin" ).c_str() );
    SW_EXPECT_TRUE( sw::ScenePreloadList::makeListPath( "game/demo/prefabs/x.prefab.xml" ).empty() );
}

/**
 * @brief [ScenePreloadListTest] 목록은 파일로 왕복하고, 깨진 파일 · 낯선 버전은 오류로 거절한다
 */
SW_TEST_CASE( ScenePreloadListTest, RoundTripsThroughAFileAndRefusesBrokenOnes )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string path = sw::FileUtil::joinPath( test::makeTempPath( "preloadlist" ), "level.preload.json" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( path ) );
    const sw::vector<sw::string> listWritten{ "game/demo/materials/x.material", "game/demo/models/a.mesh" };
    SW_ASSERT_TRUE( sw::ScenePreloadList::write( path, listWritten ) );
    sw::vector<sw::string> listRead;
    SW_ASSERT_TRUE( sw::ScenePreloadList::read( sw::ResourceID{ ::test::makeTempResourceID( path ) }, listRead ) );
    SW_EXPECT_TRUE( listRead == listWritten );

    SW_TEST_DEFENSIVE_SCOPE( "a preload list with another version or a non-string path is refused" );
    const sw::string brokenPath = sw::FileUtil::joinPath( test::makeTempPath( "preloadlist" ), "broken.preload.json" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, R"({"version": 99, "paths": []})" ) );
    SW_EXPECT_FALSE( sw::ScenePreloadList::read( sw::ResourceID{ ::test::makeTempResourceID( brokenPath ) }, listRead ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, R"({"version": 1, "paths": [3]})" ) );
    SW_EXPECT_FALSE( sw::ScenePreloadList::read( sw::ResourceID{ ::test::makeTempResourceID( brokenPath ) }, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );
}

/**
 * @brief [ScenePreloadListTest] 씬 쿠킹은 씬마다 옆에 프리로드 목록을 굽고, 목록은 씬이 쓰는 에셋 경로를 든다
 */
SW_TEST_CASE( ScenePreloadListTest, SceneCookWritesTheListBesideTheScene )
{
    sw::SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initializeSubsystem() == sw::SubsystemInitResult::Succeeded );
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string root   = test::makeTempDirectory( "preloadlist_cook_root" );
    const sw::string cooked = test::makeTempDirectory( "preloadlist_cook_out" );

    sw::SceneDocument doc;
    doc._name = "level";
    sw::SceneDocument::SceneObjectNode node{};
    node._name   = "Rock";
    node._fileID = 7;
    {
        sw::GameObjectManager scratch;
        sw::GameObject*       pObject = addMeshObject( scratch, "Rock", "game/demo/models/rock.mesh", "game/demo/materials/rock.material" );
        node._embeddedXML             = sw::ObjectStateSerializer::saveToXMLString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    }
    doc._listSceneObjectNode.push_back( node );
    const sw::string scenePath = sw::FileUtil::joinPath( root, "game/demo/maps/level.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureParentDirectoryExists( scenePath ) );
    SW_ASSERT_TRUE( doc.saveXML( scenePath ) );

    const sw::SceneEngineServices services = sw::SceneEngineServices::makeFromBoundEngine();
    const sw::CookStore           store( test::makeTempDirectory( "preloadlist_cook_record" ), test::makeTempDirectory( "preloadlist_cook_ddc" ) );
    uint32                        failedCount = 0;
    SW_EXPECT_EQUAL( 1u, sw::SceneCooker::cookAllScenes( services, root, cooked, store, failedCount ) ); // 반환은 씬 수다 — 목록은 세지 않는다
    SW_EXPECT_EQUAL( 0u, failedCount );

    const sw::string listPath = sw::FileUtil::joinPath( cooked, "game/demo/maps/level.preload.json" );
    SW_ASSERT_TRUE( sw::FileUtil::exists( listPath ) );
    sw::vector<sw::string> listRead;
    SW_ASSERT_TRUE( sw::ScenePreloadList::read( sw::ResourceID{ ::test::makeTempResourceID( listPath ) }, listRead ) );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( listRead, sw::string( "game/demo/models/rock.mesh" ) ) );
    SW_EXPECT_TRUE( sw::VectorUtil::contains( listRead, sw::string( "game/demo/materials/rock.material" ) ) );
    sceneManager.shutdownSubsystem();
}
