#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/TagID.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/MissingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        struct SceneTestInternal
        {
            /** @brief 매니저의 오브젝트마다 "오브젝트 이름: 컴포넌트 이름표…" 한 줄을 이름 순으로 모읍니다. 모르는 타입(`MissingComponent`)은 뺍니다. */
            static vector<string> collectComponentNames( const GameObjectManager* pManager )
            {
                vector<string>      listLine;
                vector<GameObject*> listObject;
                pManager->getAllGameObjects( listObject );
                for ( const GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr )
                        continue;
                    string line = pObject->getName().c_str();
                    line += ':';
                    for ( const Component* pComp : pObject->getComponents() )
                    {
                        if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->getTypeInfo() == MissingComponent::StaticType() )
                            continue;
                        line += ' ';
                        line += pComp->getComponentName().c_str();
                    }
                    listLine.push_back( std::move( line ) );
                }
                std::sort( listLine.begin(), listLine.end() );
                return listLine;
            }

            /** @brief 파일이 이름표(`_componentName`)를 적었으면 true 입니다 — 적지 않은 옛 파일만 기본값(타입 이름)으로 읽혀야 한다. */
            static bool isLabelledFile( const string& path )
            {
                string text;
                return FileUtil::readTextFile( path, text ) && text.find( "_componentName" ) != string::npos;
            }

            /**
             * @brief 매니저의 컴포넌트마다 이름표를 `Named<번호>` 로 바꿉니다(모르는 타입은 뺍니다).
             * @return 바꾸기 전 이름표가 타입 이름이 **아니던** 컴포넌트 수입니다 — 이름표가 없는 옛 파일이면 0 이어야 합니다.
             */
            static uint32 renameEveryComponent( const GameObjectManager* pManager )
            {
                uint32              nonDefaultCount = 0;
                uint32              nameIndex       = 0;
                vector<GameObject*> listObject;
                pManager->getAllGameObjects( listObject );
                for ( const GameObject* pObject : listObject )
                {
                    if ( pObject == nullptr )
                        continue;
                    for ( Component* pComp : pObject->getComponents() )
                    {
                        if ( pComp == nullptr || pComp->isPendingDestroy() || pComp->getTypeInfo() == MissingComponent::StaticType() )
                            continue;
                        if ( pComp->getComponentName() != pComp->getTypeName() )
                            ++nonDefaultCount;
                        const string name = "Named" + to_string( nameIndex++ );
                        pComp->setComponentName( hashed_string( name.c_str() ) );
                    }
                }
                return nonDefaultCount;
            }
        };

        /** @brief 그림자 볼륨 맞춤 시험의 도우미입니다(ThemePark 의 직교 리그 · 해와 같은 모양). */
        struct ShadowFitTestInternal
        {
            static constexpr uint32  kResolution = 2048;
            static constexpr float32 kPitch      = 0.5235988f; ///< 30°
            static constexpr float32 kYaw        = 0.7853982f; ///< 45°
            static constexpr float32 kDistance   = 250.0f;

            /** @brief 행벡터 규약으로 점을 행렬에 곱하고 w 로 나눕니다(셰이더의 `mul( float4( p, 1 ), m )`). */
            static sw::float3 projectToNdc( const sw::float3& worldPos, const sw::float4x4& matrix )
            {
                const sw::float4 clip = sw::float4::transform( sw::float4{ worldPos._x, worldPos._y, worldPos._z, 1.0f }, matrix );
                return sw::float3{ clip._x / clip._w, clip._y / clip._w, clip._z / clip._w };
            }

            /** @brief 초점을 내려다보는 직교 카메라의 뷰-투영입니다(16:9, 근 0.1 · 원 625 m — ThemePark 리그와 같다). */
            static sw::float4x4 makeCameraViewProj( const sw::float3& focus, float32 orthoHeight )
            {
                const sw::float3 forward{ sw::MathUtil::cos( kPitch ) * sw::MathUtil::sin( kYaw ), -sw::MathUtil::sin( kPitch ),
                                          sw::MathUtil::cos( kPitch ) * sw::MathUtil::cos( kYaw ) };
                const sw::float3 eye = focus - forward * kDistance;
                return sw::float4x4::createLookAt( eye, focus, sw::float3::Up ) * sw::float4x4::createOrthographic( orthoHeight * 16.0f / 9.0f, orthoHeight, 0.1f, 625.0f );
            }

            /** @brief 카메라가 보는 바닥(y = 0) 점들 — NDC 격자의 광선이 바닥과 만나는 곳입니다. */
            static sw::vector<sw::float3> collectVisibleGroundPoints( const sw::float4x4& viewProj )
            {
                const sw::float4x4     inverse = viewProj.invert();
                sw::vector<sw::float3> listPoint;
                for ( uint32 yIndex = 0; yIndex <= 8; ++yIndex )
                {
                    for ( uint32 xIndex = 0; xIndex <= 8; ++xIndex )
                    {
                        const float32    ndcX     = -0.95f + 1.9f * static_cast<float32>( xIndex ) / 8.0f;
                        const float32    ndcY     = -0.95f + 1.9f * static_cast<float32>( yIndex ) / 8.0f;
                        const sw::float3 nearSide = projectToNdc( sw::float3{ ndcX, ndcY, 0.0f }, inverse );
                        const sw::float3 farSide  = projectToNdc( sw::float3{ ndcX, ndcY, 1.0f }, inverse );
                        if ( ( nearSide._y > 0.0f ) == ( farSide._y > 0.0f ) )
                            continue;
                        const float32 t = nearSide._y / ( nearSide._y - farSide._y );
                        listPoint.push_back( nearSide + ( farSide - nearSide ) * t );
                    }
                }
                return listPoint;
            }

            /** @brief ThemePark 의 해 — 고도 약 20°, 볼륨을 카메라에 맞춘다(띠 -2 ~ 40 m, 빛 쪽 120 m). */
            static sw::DirectionalLightComponent* addParkSun( sw::Scene& scene )
            {
                sw::GameObject*                pSun   = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sun" ) );
                sw::DirectionalLightComponent* pLight = pSun != nullptr ? pSun->addComponent<sw::DirectionalLightComponent>() : nullptr;
                if ( pLight == nullptr )
                    return nullptr;
                pLight->setLocalRotation( sw::float3{ 0.9f, 0.8f, 0.0f } );
                pLight->setShadowExtent( 180.0f );
                pLight->setShadowDistance( 120.0f );
                pLight->setShadowViewDistance( 1000.0f );
                pLight->setShadowReceiverHeightRange( -2.0f, 40.0f );
                return pLight;
            }
        };
    } // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) SceneTest — 활성 씬·비동기 로드
// ------------------------------------------------------------------------------
/**
 * @brief [SceneTest] 쿠킹된 바이너리 엔티티 상태가 파일을 건너 살아남고, 로더가 그것을 쓰는지 검증
 *
 * @details `SceneCooker` 는 엔티티마다 든 `<GameObject ...>` XML 을 리플렉션 바이너리로 쿠킹하고 XML 을 비운다.
 *          쿠킹된 씬이 XML 문자열을 그대로 담으면 바깥 파싱만 줄고 비싼 생성 단계는 그대로 남는다.
 *
 *          **`_embeddedXml` 이 비어 있다는 것이 이 테스트의 핵심이다** — 마지막에 컴포넌트가
 *          되살아났다면 그것은 바이너리 경로로만 올 수 있다.
 */
SW_TEST_CASE( SceneTest, CookedBinaryEntityStateSurvivesFileAndIsUsedOnLoad )
{
    const sw::TagID  kTagCooked    = sw::TagID::request( "Cook.Marked" );
    const sw::string tempScenePath = test::makeTempPath( "temp_cooked_scene.bin" );

    // 1) 엔티티 하나 분량의 XML 상태를 만든다.
    sw::string sourceXml;
    {
        sw::GameObjectManager scratch;
        sw::GameObject*       pSource = scratch.createGameObject( sw::hashed_string( "CookedHero" ) );
        SW_ASSERT_NOT_NULL( pSource );
        pSource->addTag( kTagCooked );
        sw::MeshComponent* pMesh = pSource->addComponent<sw::MeshComponent>();
        SW_ASSERT_NOT_NULL( pMesh );
        pMesh->setLocalPosition( sw::float3{ 7.0f, 8.0f, 9.0f } );

        sourceXml = sw::ObjectStateSerializer::saveToXmlString( pSource );
        SW_ASSERT_TRUE( sourceXml.empty() == false );
    }

    // 2) 문서에 싣고 쿠킹한다.
    sw::SceneDocument doc{};
    doc._name = "CookedScene";
    sw::SceneDocument::SceneObjectNode node{};
    node._name        = "CookedHero";
    node._fileId      = 1;
    node._embeddedXml = sourceXml;
    doc._listSceneObjectNode.push_back( node );

    SW_EXPECT_EQUAL( 1u, sw::SceneCooker::cookEntityState( doc ) );
    SW_ASSERT_TRUE( doc._listSceneObjectNode[0]._embeddedStateBytes.empty() == false );
    SW_EXPECT_TRUE( doc._listSceneObjectNode[0]._embeddedXml.empty() );

    // 3) 파일을 건너도 상태가 남는지 — 여기서 실패하면 SCN1 이 그 필드를 안 싣는 것이다.
    SW_ASSERT_TRUE( doc.saveBinary( tempScenePath ) );

    sw::SceneDocument loaded{};
    SW_ASSERT_TRUE( loaded.loadBinary( tempScenePath ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( loaded._listSceneObjectNode.size() ) );
    SW_ASSERT_TRUE( loaded._listSceneObjectNode[0]._embeddedStateBytes.empty() == false );
    SW_EXPECT_TRUE( loaded._listSceneObjectNode[0]._embeddedXml.empty() );

    // 4) 로더가 그 바이너리를 실제로 쓰는지 — XML 이 비었으니 다른 길은 없다.
    sw::Scene scene{ "CookedScene" };
    SW_ASSERT_TRUE( scene.instantiate( loaded ) );

    sw::GameObjectManager* pManager = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );
    sw::GameObject* pRestored = pManager->findGameObjectByName( sw::hashed_string( "CookedHero" ) );
    SW_ASSERT_NOT_NULL( pRestored );

    SW_EXPECT_TRUE( pRestored->hasTag( kTagCooked ) );
    sw::MeshComponent* pRestoredMesh = pRestored->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pRestoredMesh );
    SW_EXPECT_NEAR_EQUAL( 7.0f, pRestoredMesh->getLocalPosition()._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, pRestoredMesh->getLocalPosition()._z, 0.001f );
}

/**
 * @brief [SceneTest] 빈 매니저에서 createScene 이 활성 씬을 설정
 */
SW_TEST_CASE( SceneTest, CreateSceneSetsActiveWhenEmpty )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SW_EXPECT_NULL( manager.getActiveScene() );
    SW_EXPECT_EQUAL( size_t( 0 ), manager.getLoadedScenes().size() );

    sw::Scene* scene = manager.createScene( "Main" );
    SW_ASSERT_NOT_NULL( scene );
    SW_EXPECT_STREQ( "Main", scene->getName() );
    SW_EXPECT_EQUAL( scene, manager.getActiveScene() );
    SW_EXPECT_EQUAL( size_t( 1 ), manager.getLoadedScenes().size() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 월드가 플레이 중이면 활성 씬이 시작하고, 활성 씬이 바뀌면 나가는 씬은 끝나고 들어오는 씬이 시작한다
 * @details `SceneManager` 가 "월드 플레이 중" 을 들고 활성 씬 교체 때 넘긴다. 에디터 Play 버튼이 활성 씬의 매니저에 직접
 *          `beginPlay` 를 부르면 App · Shipping 에서는 아무도 부르지 않고, 플레이 중에 연 씬은 시작하지 않는다.
 */
SW_TEST_CASE( SceneTest, WorldPlayingFollowsTheActiveScene )
{
    sw::SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    sw::Scene* pFirst = sceneManager.createScene( "PlayFirst" );
    SW_ASSERT_NOT_NULL( pFirst );
    sw::RegisterMockComponents();

    int32                  endCount = 0;
    sw::GameObject*        pActor   = pFirst->getObjectManager()->createGameObject( sw::hashed_string( "Actor" ) );
    sw::MockMeshComponent* pMesh    = pActor->addComponent<sw::MockMeshComponent>();
    SW_ASSERT_NOT_NULL( pMesh );
    pMesh->_pEndPlayCount = &endCount;
    SW_EXPECT_EQUAL( 0, pMesh->_beginPlayCount );

    sceneManager.setWorldPlaying( true );
    SW_EXPECT_TRUE( sceneManager.isWorldPlaying() );
    SW_EXPECT_EQUAL( 1, pMesh->_beginPlayCount );

    // 새 씬을 활성으로 — 나가는 씬은 끝나고(한 번, 이어진 언로드의 해체가 두 번 끝내지 않는다) 들어오는 씬은 시작한다.
    sw::Scene* pSecond = sceneManager.createEmptyActiveScene( "PlaySecond" );
    SW_ASSERT_NOT_NULL( pSecond );
    SW_EXPECT_EQUAL( 1, endCount );
    SW_EXPECT_TRUE( pSecond->getObjectManager()->hasBegunPlay() );

    sceneManager.setWorldPlaying( false );
    SW_EXPECT_FALSE( pSecond->getObjectManager()->hasBegunPlay() );

    // 플레이 중에 내리면 활성 씬이 **살아 있는 동안** 끝난다. 씬을 지운 뒤 활성을 비우는 순서면 풀린 씬을 만진다.
    sw::RegisterMockComponents();
    int32                  shutdownEndCount = 0;
    sw::MockMeshComponent* pLast            = pSecond->getObjectManager()->createGameObject( sw::hashed_string( "Last" ) )->addComponent<sw::MockMeshComponent>();
    pLast->_pEndPlayCount                   = &shutdownEndCount;
    sceneManager.setWorldPlaying( true );
    SW_EXPECT_EQUAL( 1, pLast->_beginPlayCount );
    sceneManager.shutdown();
    SW_EXPECT_EQUAL( 1, shutdownEndCount );
    SW_EXPECT_FALSE( sceneManager.isWorldPlaying() );
}

/**
 * @brief [SceneTest] 여러 씬이 있어도 첫 활성 씬 유지
 */
SW_TEST_CASE( SceneTest, MultipleScenesKeepFirstActive )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::Scene* first  = manager.createScene( "Level_A" );
    sw::Scene* second = manager.createScene( "Level_B" );
    SW_ASSERT_NOT_NULL( first );
    SW_ASSERT_NOT_NULL( second );
    SW_EXPECT_EQUAL( first, manager.getActiveScene() );
    SW_EXPECT_EQUAL( size_t( 2 ), manager.getLoadedScenes().size() );
    SW_EXPECT_STREQ( "Level_B", second->getName() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] GameObjectManager 소유
 */
SW_TEST_CASE( SceneTest, OwnsGameObjectManager )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::Scene* scene = manager.createScene( "World" );
    SW_ASSERT_NOT_NULL( scene );
    SW_ASSERT_NOT_NULL( scene->getObjectManager() );

    sw::GameObject* obj = scene->getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( obj );
    SW_EXPECT_EQUAL( obj, scene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Hero" ) ) );

    manager.shutdown();
}

/**
 * @brief [SceneTest] RHI 없이 update 안전
 */
SW_TEST_CASE( SceneTest, UpdateWithoutRhiIsSafe )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::Scene* scene = manager.createScene( "TickWorld" );
    SW_ASSERT_NOT_NULL( scene );
    scene->getObjectManager()->createGameObject( sw::hashed_string( "EmptyActor" ) );

    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( size_t( 1 ), scene->getObjectManager()->getAllGameObjects().size() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 내 엔티티 수명 및 셧다운 후 정리 검증
 */
SW_TEST_CASE( SceneTest, SceneEntityLifecycleAndShutdownCleanup )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::Scene* sceneA = manager.createScene( "DungeonLevel" );
    SW_ASSERT_NOT_NULL( sceneA );

    // 오브젝트 여러 개 생성
    sw::GameObject* hero    = sceneA->getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
    sw::GameObject* monster = sceneA->getObjectManager()->createGameObject( sw::hashed_string( "Monster" ) );
    SW_ASSERT_NOT_NULL( hero );
    SW_ASSERT_NOT_NULL( monster );

    SW_EXPECT_EQUAL( 2u, sceneA->getObjectManager()->getAllGameObjects().size() );

    // 틱 실행 및 정상 상태 검증
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( 2u, sceneA->getObjectManager()->getAllGameObjects().size() );

    // 셧다운 시 모든 씬 및 오브젝트 정리
    manager.shutdown();
    SW_EXPECT_NULL( manager.getActiveScene() );
    SW_EXPECT_EQUAL( 0u, manager.getLoadedScenes().size() );
}

/**
 * @brief [SceneTest] serializeToDocument 가 프리팹 소스 경로를 씁니다
 */
SW_TEST_CASE( SceneTest, SerializeWritesPrefabSourcePath )
{
    sw::Scene scene{ "PrefabRoundTrip" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );

    sw::GameObject* pHero = scene.getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pHero );
    scene.setEntityPrefabPath( pHero->getObjectId(), "prefabs/hero.prefab.xml" );

    sw::SceneDocument doc{};
    SW_ASSERT_TRUE( scene.serializeToDocument( doc ) );
    SW_EXPECT_FALSE( doc._listSceneObjectNode.empty() );

    bool bFoundPrefab{ false };
    for ( const sw::SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
    {
        if ( node._name != "Hero" )
            continue;
        SW_EXPECT_STREQ( "prefabs/hero.prefab.xml", node._prefab.c_str() );
        bFoundPrefab = true;
    }
    SW_EXPECT_TRUE( bFoundPrefab );
}

/**
 * @brief [SceneTest] createEmptyActiveScene 이 세대를 올리고 이전 씬을 내립니다
 */
SW_TEST_CASE( SceneTest, CreateEmptyActiveSceneBumpsGeneration )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    sw::Scene* pFirst = manager.createScene( "First" );
    SW_ASSERT_NOT_NULL( pFirst );
    const uint64 firstGeneration = manager.getSceneGeneration();
    SW_EXPECT_TRUE( firstGeneration > 0 );

    sw::Scene* pEmpty = manager.createEmptyActiveScene( "Untitled" );
    SW_ASSERT_NOT_NULL( pEmpty );
    SW_EXPECT_EQUAL( pEmpty, manager.getActiveScene() );
    SW_EXPECT_TRUE( manager.getSceneGeneration() > firstGeneration );
    SW_EXPECT_STREQ( "Untitled", pEmpty->getName() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] SceneDocument가 prefabGuid를 정상 직렬화하고 AssetDatabase를 통해 새 경로로 자동 복원합니다
 */
SW_TEST_CASE( SceneTest, PrefabGuidRoundtripAndResolve )
{
    sw::SceneDocument doc{};
    doc._name = "GuidTestScene";

    sw::SceneDocument::SceneObjectNode node{};
    node._name              = "HeroInstance";
    node._fileId            = 2;
    node._prefab            = "prefabs/old_hero.prefab.xml";
    const sw::Uuid heroGuid = sw::Uuid::generate();
    node._prefabGuid        = heroGuid.toString();
    doc._listSceneObjectNode.push_back( node );

    const sw::string tempSceneXml = test::makeTempPath( "guid_scene.scene.xml" );
    SW_ASSERT_TRUE( doc.saveXml( tempSceneXml ) );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getAssetManager().getAssetDatabase().registerMapping( "prefabs/new_hero.prefab.xml", heroGuid );

    sw::SceneDocument loadedDoc{};
    SW_ASSERT_TRUE( loadedDoc.loadXml( tempSceneXml ) );
    SW_ASSERT_FALSE( loadedDoc._listSceneObjectNode.empty() );
    SW_EXPECT_STREQ( "HeroInstance", loadedDoc._listSceneObjectNode[0]._name.c_str() );
    SW_EXPECT_STREQ( heroGuid.toString().c_str(), loadedDoc._listSceneObjectNode[0]._prefabGuid.c_str() );
    if ( sw::engine::areEngineServicesBound() )
        SW_EXPECT_STREQ( "prefabs/new_hero.prefab.xml", loadedDoc._listSceneObjectNode[0]._prefab.c_str() );

    // 바이너리(SCN1)도 같은 풀이를 지난다 — 두 로더가 GUID 풀이를 각자 들면 한쪽만 어긋나도 Dev 테스트로는 드러나지 않는다.
    const sw::string tempSceneBin = test::makeTempPath( "guid_scene.scene.bin" );
    SW_ASSERT_TRUE( doc.saveBinary( tempSceneBin ) );
    sw::SceneDocument loadedBinaryDoc{};
    SW_ASSERT_TRUE( loadedBinaryDoc.loadBinary( tempSceneBin ) );
    SW_ASSERT_FALSE( loadedBinaryDoc._listSceneObjectNode.empty() );
    SW_EXPECT_STREQ( heroGuid.toString().c_str(), loadedBinaryDoc._listSceneObjectNode[0]._prefabGuid.c_str() );
    if ( sw::engine::areEngineServicesBound() )
        SW_EXPECT_STREQ( "prefabs/new_hero.prefab.xml", loadedBinaryDoc._listSceneObjectNode[0]._prefab.c_str() );
}

/**
 * @brief [SceneTest] 커밋된 테스트 씬의 TestProp 은 옛 경로(old/)를 가리키지만 GUID 로 실제 프리팹을 찾는다.
 * @details 시작 시점 AssetDatabase(.meta 스캔 / 배포본은 assetregistry.txt)가 있어야 통과한다 — 로드된 적 없는
 *          에셋의 GUID 를 알아야 하므로. 배포본의 같은 경로는 SCN1 이 prefabGuid 를 실어야 열린다(쿠커가 그것을
 *          빠뜨리면 배포본에서만 깨진다). GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( SceneTest, EditorTestSceneResolvesMovedPrefabByGuid )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::SceneDocument doc{};
    SW_ASSERT_TRUE( doc.load( "game/empty/maps/editortest.scene.xml" ) );

    bool bFound = false;
    for ( const sw::SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
    {
        if ( node._name != "TestProp" )
            continue;
        bFound = true;
        SW_EXPECT_STREQ( "game/empty/prefabs/testprop.prefab.xml", node._prefab.c_str() );
        SW_EXPECT_STREQ( "deb66c15-3534-4b79-b3de-a2080d7c5ffe", node._prefabGuid.c_str() );
    }
    SW_EXPECT_TRUE_MSG( bFound, "테스트 씬에 TestProp 이 없다 — 이 검증이 아무것도 보지 않았다" );
}

/**
 * @brief [SceneTest] 주광 조회가 등록부를 보고, 활성/파괴를 따라간다
 * @details 주광은 매 프레임 **모든 GameObject** 를 훑지 않고 등록부에서 찾는다. 조회 결과가 씬을 훑은 것과 같은지
 *          고정한다 — 없음 · 있음 · 비활성 · 파괴 네 상태다.
 */
SW_TEST_CASE( SceneTest, DirectionalLightLookupFollowsRegistry )
{
    sw::Scene scene{ "LightLookup" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );

    // 빛이 하나도 없으면 nullptr 이다.
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );

    // 빛과 무관한 오브젝트가 아무리 많아도 결과는 그대로다.
    for ( uint32 index = 0; index < 16; ++index )
        SW_ASSERT_NOT_NULL( scene.getObjectManager()->createGameObject( sw::hashed_string( "Filler" ) ) );
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );

    sw::GameObject* pSun = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sun" ) );
    SW_ASSERT_NOT_NULL( pSun );
    sw::DirectionalLightComponent* pLight = pSun->addComponent<sw::DirectionalLightComponent>();
    SW_ASSERT_NOT_NULL( pLight );
    SW_EXPECT_EQUAL( pLight, scene.findActiveDirectionalLight() );

    // 컴포넌트를 끄면 안 보인다.
    pLight->setActive( false );
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );
    pLight->setActive( true );
    SW_EXPECT_EQUAL( pLight, scene.findActiveDirectionalLight() );

    // 소유 오브젝트를 끄면 역시 안 보인다.
    pSun->setActive( false );
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );
    pSun->setActive( true );
    SW_EXPECT_EQUAL( pLight, scene.findActiveDirectionalLight() );

    // 파괴되면 등록이 풀린다 — 등록부에 죽은 포인터가 남으면 여기서 잡힌다.
    scene.getObjectManager()->destroyObject( pSun, true );
    scene.getObjectManager()->tick( 0.016f );
    SW_EXPECT_NULL( scene.findActiveDirectionalLight() );
}

/**
 * @brief [SceneTest] 게임 카메라는 프레임마다 등록부의 규칙으로 다시 골라진다 — 늦게 생긴 높은 우선순위, 끄기, 역할 변경, 파괴 대기, 동률, 직접 지정
 * @details 처음 한 번 고른 결과를 캐시하면 나중에 생긴 더 높은 우선순위의 카메라는 선택되지 않고, 꺼 둔 카메라가 계속 선택된다.
 *          첫 호출이 이미 있는 "GameCamera" 의 위치 · 렌즈를 기본값으로 되돌리면 씬 파일에 둔 카메라가 첫 프레임에 옮겨진다.
 *          에디터 카메라도 같은 규칙(`CameraRegistry::selectCamera`)을 쓴다.
 */
SW_TEST_CASE( SceneTest, GameCameraSelectionFollowsTheRegistry )
{
    sw::Scene              scene{ "CameraSelection" };
    sw::GameObjectManager* pObjects = scene.getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    // 씬 파일에 있던 것처럼 "GameCamera" 를 먼저 둔다 — 첫 선택이 그 자리 · 렌즈를 되돌리면 안 된다.
    sw::GameObject*      pDefaultObj = pObjects->createGameObject( sw::hashed_string( "GameCamera" ) );
    sw::CameraComponent* pDefault    = pDefaultObj->addComponent<sw::CameraComponent>();
    SW_ASSERT_NOT_NULL( pDefault );
    pDefault->setRole( sw::CameraRole::Game );
    pDefault->setLocalPosition( sw::float3( 5.0f, 6.0f, 7.0f ) );
    pDefault->setFieldOfViewY( 1.1f );
    SW_ASSERT_TRUE( scene.ensureDefaultCameras() );
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pDefault );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pDefault->getLocalPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.1f, pDefault->getFieldOfViewY(), 1e-4f );

    // 늦게 생긴 높은 우선순위의 게임 카메라가 다음 선택에서 이긴다.
    sw::GameObject*      pHighObj = pObjects->createGameObject( sw::hashed_string( "HighCamera" ) );
    sw::CameraComponent* pHigh    = pHighObj->addComponent<sw::CameraComponent>();
    SW_ASSERT_NOT_NULL( pHigh );
    pHigh->setRole( sw::CameraRole::Game );
    pHigh->setPriority( 10 );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pHigh );

    // 끄면 다음 것으로, 켜면 돌아온다. 역할을 바꿔도 따라간다.
    pHigh->setActive( false );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pDefault );
    pHigh->setActive( true );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pHigh );
    pHigh->setRole( sw::CameraRole::Editor );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pDefault );
    SW_EXPECT_TRUE( pObjects->getCameraRegistry().selectCamera( sw::CameraRole::Editor ) == pHigh );
    pHigh->setRole( sw::CameraRole::Game );

    // 직접 고른 카메라가 살아 있고 켜져 있는 동안은 그것이 먼저다.
    scene.setActiveGameCamera( pDefault );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pDefault );
    scene.setActiveGameCamera( nullptr );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pHigh );

    // 파괴 대기가 되면 그 프레임부터 빠진다(메모리는 아직 살아 있다). 규칙 자체가 거르는지 등록부 선택으로 본다.
    pObjects->destroyObject( pHighObj );
    SW_EXPECT_TRUE( pObjects->getCameraRegistry().selectCamera( sw::CameraRole::Game ) == pDefault );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pDefault );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pDefault->getLocalPosition()._x, 1e-4f );
    pObjects->processDeferredDestruction();
    SW_EXPECT_EQUAL( size_t( 1 ), pObjects->getCameraRegistry().getAll().size() );

    // 우선순위가 같으면 나중에 만든 것(컴포넌트 id 가 큰 것)이 이긴다.
    sw::GameObject*      pTieObj = pObjects->createGameObject( sw::hashed_string( "TieCamera" ) );
    sw::CameraComponent* pTie    = pTieObj->addComponent<sw::CameraComponent>();
    pTie->setRole( sw::CameraRole::Game );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pTie );

    // 진 쪽을 제자리에서 다시 읽어도(되돌리기 · 플레이 종료 복원 — 다시 등록돼 목록 끝으로 간다) 선택은 그대로다. 등록 순서로 가르면
    // 여기서 뒤집힌다.
    const sw::ObjectIdentity identity = sw::ObjectStateSerializer::captureIdentity( pDefaultObj );
    SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXmlString( pDefaultObj, sw::ObjectStateSerializer::saveToXmlString( pDefaultObj ), { &identity } ) );
    scene.ensureDefaultCameras();
    SW_EXPECT_TRUE( scene.getActiveGameCamera() == pTie );
}

/**
 * @brief [SceneTest] 그림자 행렬의 **깊이 범위**가 씬을 실제로 담는다
 * @details 라이트 카메라는 원점에서 **거리만큼 떨어져** 원점을 보므로 씬의 뷰 z 는 거리 언저리다. 직교 투영의
 *          near/far 를 `(-거리, +거리)` 로 잡으면 `z' = (z_view - near)/(far - near)` 로 씬 전체가 z' ≈ 1(원평면)에
 *          뭉치고, 깊이 비교가 늘 "가려지지 않음" 이 되어 그림자가 지지 않는다. 로그도 경고도 없고 그림에서만
 *          드러나므로(그림자를 받을 바닥이 없으면 그마저도 안 보인다) **행렬 자체**를 여기서 고정한다.
 * @note 검사는 "원점이 깊이 구간 한가운데로 간다" 다 — 볼륨의 중심이 원점이므로 정의상 0.5 여야 한다.
 */
SW_TEST_CASE( SceneTest, ShadowMatrixDepthRangeContainsScene )
{
    sw::Scene scene{ "ShadowMatrix" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );

    sw::GameObject* pSun = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sun" ) );
    SW_ASSERT_NOT_NULL( pSun );
    sw::DirectionalLightComponent* pLight = pSun->addComponent<sw::DirectionalLightComponent>();
    SW_ASSERT_NOT_NULL( pLight );

    // 벤치가 쓰는 것과 같은 모양 — 볼륨이 씬보다 크고, 카메라가 그보다 더 뒤로 빠져 있다.
    constexpr float32 kExtent   = 6.0f;
    constexpr float32 kDistance = 9.0f;
    pLight->setShadowExtent( kExtent );
    pLight->setShadowDistance( kDistance );

    const sw::float4x4 lightViewProj = pLight->buildShadowViewProj();

    auto ndcDepthOf = [&lightViewProj]( const sw::float3& worldPos ) -> float32
    {
        // 이 엔진은 **행벡터** 규약이다(셰이더도 `mul( 벡터, 행렬 )`). 벡터×행렬 연산자가 없어 여기서 쓴다 —
        // 깊이만 필요하므로 z·w 성분 둘뿐이다.
        const float32 clipZ = worldPos._x * lightViewProj._13 + worldPos._y * lightViewProj._23 +
                              worldPos._z * lightViewProj._33 + lightViewProj._43;
        const float32 clipW = worldPos._x * lightViewProj._14 + worldPos._y * lightViewProj._24 +
                              worldPos._z * lightViewProj._34 + lightViewProj._44;
        return ( sw::MathUtil::abs( clipW ) > sw::MathUtil::Epsilon ) ? ( clipZ / clipW ) : clipZ;
    };

    // 원점은 볼륨의 한가운데다.
    const float32 originDepth = ndcDepthOf( sw::float3::Zero );
    SW_EXPECT_TRUE_MSG( sw::MathUtil::abs( originDepth - 0.5f ) < 0.01f,
                        "원점이 그림자 깊이 구간의 한가운데(0.5)에 있어야 한다 — 1 에 붙어 있으면 씬 전체가 원평면에 뭉친 것이다" );

    // 빛을 향해 볼륨 끝까지 올라간 점과 그 반대쪽 점이 **서로 다른 깊이**로 갈라져야 비교가 의미를 갖는다.
    const sw::float3 lightDir = pLight->getLightDirection();
    const float32    nearSide = ndcDepthOf( lightDir * -( kExtent * 0.5f ) );
    const float32    farSide  = ndcDepthOf( lightDir * ( kExtent * 0.5f ) );
    SW_EXPECT_TRUE_MSG( nearSide > 0.0f && farSide < 1.0f, "볼륨 안의 점은 깊이 구간 안에 들어와야 한다" );
    SW_EXPECT_TRUE_MSG( ( farSide - nearSide ) > 0.2f,
                        "빛 방향으로 떨어진 두 점의 깊이가 뚜렷이 갈려야 한다 — 붙어 있으면 그림자 비교가 무의미하다" );
}

/**
 * @brief [SceneTest] 그림자 바이어스는 볼륨 크기와 상관없이 텍셀 한두 개 길이다
 * @details 바이어스를 NDC 상수(0.02)로 두면 깊이 360 m 볼륨(ThemePark)에서 월드 7.2 m 가 되어, 받는 면 위 2.5 m 미만의 물체는 그림자가
 *          통째로 사라지고 나무 그림자는 밑동에서 떨어진다. 기본 볼륨(2.2 m)과 큰 볼륨(360 m) 둘 다 월드 바이어스가 텍셀 두 개 안이어야 한다.
 */
SW_TEST_CASE( SceneTest, ShadowBiasStaysInWorldUnits )
{
    sw::Scene scene{ "ShadowBias" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::GameObject* pSun = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sun" ) );
    SW_ASSERT_NOT_NULL( pSun );
    sw::DirectionalLightComponent* pLight = pSun->addComponent<sw::DirectionalLightComponent>();
    SW_ASSERT_NOT_NULL( pLight );

    constexpr uint32 kResolution = 2048;
    for ( const float32 extent : { 2.2222223f, 180.0f } )
    {
        pLight->setShadowExtent( extent );
        pLight->setShadowDistance( extent * 2.0f );
        const sw::DirectionalShadowProjection projection     = pLight->buildShadowProjection( kResolution );
        const sw::float4                      params         = projection.computeShaderParams();
        const float32                         depthBiasWorld = params._x * projection._depthRange;
        const sw::string                      label          = "extent " + sw::to_string( extent ) + ": depth bias " + sw::to_string( depthBiasWorld ) + " m, normal offset " +
                                 sw::to_string( params._z ) + " m, texel " + sw::to_string( projection._texelWorldSize ) + " m";
        SW_EXPECT_TRUE_MSG( depthBiasWorld > 0.0f && depthBiasWorld <= projection._texelWorldSize * 2.0f, label.c_str() );
        SW_EXPECT_TRUE_MSG( params._z > 0.0f && params._z <= projection._texelWorldSize * 3.0f, label.c_str() );
        SW_EXPECT_NEAR_EQUAL( 1.0f / static_cast<float32>( kResolution ), params._w, 1e-9f );
    }
}

/**
 * @brief [SceneTest] 그림자 볼륨이 직교 카메라가 보는 바닥을 덮고, 줌에 따라 텍셀이 작아진다
 * @details 볼륨을 원점에 고정하면 공원 안쪽(초점 60,0,230)은 볼륨 밖이라 그림자가 하나도 없고, 확대해도 텍셀이 그대로다(ThemePark).
 *          보이는 바닥 점이 모두 볼륨 안이어야 하고, 그 점에서 빛 쪽으로 30 m 높이까지 올라간 가리는 점도 깊이 구간 안이어야 한다(가까운 면에 잘리면
 *          레일 · 나무 그림자가 빠진다). 텍셀은 줌 25 에서 6 cm, 줌 110 에서 20 cm 아래 — 고정 볼륨 360 m 는 17.6 cm 다.
 */
SW_TEST_CASE( SceneTest, ShadowVolumeFollowsOrthoCamera )
{
    using Internal = sw::ShadowFitTestInternal;
    sw::Scene scene{ "ShadowFit" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addParkSun( scene );
    SW_ASSERT_NOT_NULL( pLight );
    const sw::float3 lightDir = pLight->getLightDirection();
    SW_ASSERT_TRUE( lightDir._y < -0.1f );

    struct ViewCase
    {
        sw::float3 _focus;
        float32    _orthoHeight;
        float32    _maxTexel;
    };
    const ViewCase kArrCase[] = {
        { sw::float3{ 20.0f, 0.0f, 20.0f }, 110.0f,  0.2f},
        {sw::float3{ 60.0f, 0.0f, 230.0f }, 110.0f,  0.2f},
        {sw::float3{ 20.0f, 0.0f, -35.0f },  25.0f, 0.06f},
    };
    for ( const ViewCase& viewCase : kArrCase )
    {
        const sw::float4x4                    viewProj   = Internal::makeCameraViewProj( viewCase._focus, viewCase._orthoHeight );
        const sw::DirectionalShadowProjection projection = pLight->buildShadowProjectionForView( viewProj, Internal::kResolution );
        const sw::vector<sw::float3>          listGround = Internal::collectVisibleGroundPoints( viewProj );
        SW_ASSERT_TRUE( listGround.size() > 40 );

        const sw::string label = "focus (" + sw::to_string( viewCase._focus._x ) + ", " + sw::to_string( viewCase._focus._z ) + ") ortho " +
                                 sw::to_string( viewCase._orthoHeight ) + ": ";
        uint32 outsideCount = 0;
        for ( const sw::float3& ground : listGround )
        {
            const sw::float3 ndc       = Internal::projectToNdc( ground, projection._viewProj );
            const sw::float3 caster    = ground - lightDir * ( 30.0f / -lightDir._y ); // 해 쪽으로 30 m 높이
            const sw::float3 casterNdc = Internal::projectToNdc( caster, projection._viewProj );
            const bool       bInside   = sw::MathUtil::abs( ndc._x ) <= 1.0f && sw::MathUtil::abs( ndc._y ) <= 1.0f && ndc._z >= 0.0f && ndc._z <= 1.0f &&
                                 casterNdc._z >= 0.0f && casterNdc._z <= 1.0f;
            if ( bInside == false )
                ++outsideCount;
        }
        SW_EXPECT_TRUE_MSG( outsideCount == 0,
                            ( label + sw::to_string( outsideCount ) + " visible ground points (or their 30 m casters) are outside the shadow volume" ).c_str() );
        SW_EXPECT_TRUE_MSG( projection._texelWorldSize < viewCase._maxTexel, ( label + "texel " + sw::to_string( projection._texelWorldSize ) + " m" ).c_str() );
    }
}

/**
 * @brief [SceneTest] 카메라를 팬해도 같은 월드 점이 그림자 맵의 같은 텍셀 자리에 떨어진다(텍셀 스냅)
 * @details 볼륨 원점을 카메라를 따라 연속으로 옮기면 래스터 격자가 매 프레임 미끄러져 그림자 가장자리가 기어 다닌다(언리얼 · 유니티 CSM 이 스냅하는 이유).
 */
SW_TEST_CASE( SceneTest, ShadowVolumeSnapsToTexelsWhilePanning )
{
    using Internal = sw::ShadowFitTestInternal;
    sw::Scene scene{ "ShadowSnap" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );
    sw::DirectionalLightComponent* pLight = Internal::addParkSun( scene );
    SW_ASSERT_NOT_NULL( pLight );

    const sw::float3 worldPoint{ 31.3f, 0.0f, 27.9f };
    auto             texelPhaseOf = [&]( const sw::float3& focus ) -> sw::float2
    {
        const sw::DirectionalShadowProjection projection =
            pLight->buildShadowProjectionForView( Internal::makeCameraViewProj( focus, 110.0f ), Internal::kResolution );
        const sw::float3 ndc    = Internal::projectToNdc( worldPoint, projection._viewProj );
        const float32    texelX = ( ndc._x * 0.5f + 0.5f ) * static_cast<float32>( Internal::kResolution );
        const float32    texelY = ( ndc._y * 0.5f + 0.5f ) * static_cast<float32>( Internal::kResolution );
        return sw::float2{ texelX - sw::MathUtil::floor( texelX ), texelY - sw::MathUtil::floor( texelY ) };
    };
    const sw::float2 before        = texelPhaseOf( sw::float3{ 20.0f, 0.0f, 20.0f } );
    const sw::float2 after         = texelPhaseOf( sw::float3{ 20.37f, 0.0f, 20.21f } );
    auto             phaseDistance = []( float32 a, float32 b )
    {
        const float32 distance = sw::MathUtil::abs( a - b );
        return sw::MathUtil::min( distance, 1.0f - distance );
    };
    SW_EXPECT_TRUE_MSG( phaseDistance( before._x, after._x ) < 0.02f && phaseDistance( before._y, after._y ) < 0.02f,
                        ( "texel phase moved: (" + sw::to_string( before._x ) + ", " + sw::to_string( before._y ) + ") -> (" + sw::to_string( after._x ) +
                          ", " + sw::to_string( after._y ) + ")" )
                            .c_str() );
}

/**
 * @brief [SceneTest] 씬 라이트 수집이 등록부를 보고, 타입·그림자 플래그를 제대로 싣는다
 * @details 렌더 스레드는 씬을 못 보므로 라이트는 **패킷으로만** 간다. 그 변환이 이 함수 하나라
 *          여기가 틀리면 "빛이 하나 조용히 엉뚱하게 계산된다" 로만 드러난다.
 */
SW_TEST_CASE( SceneTest, SceneLightCollectionCarriesTypeAndShadowFlag )
{
    sw::Scene scene{ "LightCollect" };
    SW_ASSERT_NOT_NULL( scene.getObjectManager() );

    sw::vector<sw::GpuLight> listLight;
    sw::collectSceneLights( &scene, listLight );
    SW_EXPECT_TRUE( listLight.empty() );

    sw::GameObject* pSun = scene.getObjectManager()->createGameObject( sw::hashed_string( "Sun" ) );
    SW_ASSERT_NOT_NULL( pSun );
    SW_ASSERT_NOT_NULL( pSun->addComponent<sw::DirectionalLightComponent>() );

    sw::GameObject* pPointObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Point" ) );
    SW_ASSERT_NOT_NULL( pPointObject );
    sw::PointLightComponent* pPoint = pPointObject->addComponent<sw::PointLightComponent>();
    SW_ASSERT_NOT_NULL( pPoint );
    pPoint->setRadius( 4.0f );
    pPoint->setLocalPosition( sw::float3{ 1.0f, 2.0f, 3.0f } );

    sw::GameObject* pSpotObject = scene.getObjectManager()->createGameObject( sw::hashed_string( "Spot" ) );
    SW_ASSERT_NOT_NULL( pSpotObject );
    sw::SpotLightComponent* pSpot = pSpotObject->addComponent<sw::SpotLightComponent>();
    SW_ASSERT_NOT_NULL( pSpot );
    pSpot->setOuterConeAngle( 0.5f );
    pSpot->setInnerConeAngle( 0.2f );

    sw::collectSceneLights( &scene, listLight );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), listLight.size() );

    // 순서는 방향광 → 점광 → 스폿이다(collectSceneLights 가 그 순서로 돈다).
    SW_EXPECT_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypeDirectional ), listLight[0]._directionType._w );
    SW_EXPECT_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypePoint ), listLight[1]._directionType._w );
    SW_EXPECT_EQUAL( static_cast<float32>( sw::shaderslot::kLightTypeSpot ), listLight[2]._directionType._w );

    // 그림자 맵이 하나뿐이라 그림자를 받는 빛도 하나 — 방향광이다.
    SW_EXPECT_EQUAL( 1.0f, listLight[0]._params._x );
    SW_EXPECT_EQUAL( 0.0f, listLight[1]._params._x );
    SW_EXPECT_EQUAL( 0.0f, listLight[2]._params._x );

    // 점광은 위치와 반경이 실린다.
    SW_EXPECT_EQUAL( 4.0f, listLight[1]._positionRadius._w );
    SW_EXPECT_EQUAL( 3.0f, listLight[1]._positionRadius._z );

    // 스폿 원뿔은 **코사인으로** 실린다 — 셰이더가 매 픽셀 acos 를 하지 않도록.
    SW_EXPECT_TRUE_MSG( listLight[2]._params._z > listLight[2]._params._y,
                        "cos(안쪽각) 이 cos(바깥각) 보다 커야 한다 — 뒤집히면 원뿔 감쇠 분모가 음수가 된다" );

    // 끄면 빠진다 — 활성 판정은 수집하는 쪽이 한다.
    pPoint->setActive( false );
    sw::collectSceneLights( &scene, listLight );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), listLight.size() );
}

/**
 * @brief [SceneTest] 엔티티 id 가 없는 씬은 읽지도 쓰지도 쿠킹하지도 않는다 — 파일의 엔티티는 늘 0 이 아닌 id 를 든다
 * @details 부착 · 핸들은 부모를 파일 id 로 가리키고 쿠커는 그 id 로 엔티티를 찾는다. 씬을 쓰는 쪽(`Scene::serializeToDocument`)은 모든 엔티티에
 *          id 를 주므로 id 없는 엔티티는 지금 형식이 아니다 — 읽는 쪽이 id 를 지어 주지 않고 거절한다.
 */
SW_TEST_CASE( SceneTest, SceneEntityWithoutAnIdIsRejected )
{
    const sw::string kHead  = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"1\" name=\"NoIds\">\n  <entities>\n";
    const sw::string kTail  = "  </entities>\n</Scene>\n";
    const sw::string withId = kHead + "    <entity id=\"1\" name=\"Kept\"/>\n" + kTail;
    const sw::string noId   = kHead + "    <entity id=\"1\" name=\"Kept\"/>\n    <entity name=\"NoId\"/>\n" + kTail;
    const sw::string zeroId = kHead + "    <entity id=\"0\" name=\"ZeroId\"/>\n" + kTail;

    const sw::string withIdPath = test::makeTempPath( "with_id.scene.xml" );
    const sw::string noIdPath   = test::makeTempPath( "no_id.scene.xml" );
    const sw::string zeroIdPath = test::makeTempPath( "zero_id.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( withIdPath, withId ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( noIdPath, noId ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( zeroIdPath, zeroId ) );

    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXml( withIdPath ) );
    SW_ASSERT_EQUAL( size_t( 1 ), doc._listSceneObjectNode.size() );
    SW_EXPECT_EQUAL( uint64( 1 ), doc._listSceneObjectNode[0]._fileId );

    sw::SceneDocument::SceneObjectNode noIdEntity{};
    noIdEntity._name = "NoId";
    {
        SW_TEST_DEFENSIVE_SCOPE( "a scene entity without an id" );
        SW_EXPECT_FALSE( doc.loadXml( noIdPath ) );
        SW_EXPECT_TRUE( doc._listSceneObjectNode.empty() ); // 반쯤 읽은 문서를 남기지 않는다
        SW_EXPECT_FALSE( doc.loadXml( zeroIdPath ) );

        SW_ASSERT_TRUE( doc.loadXml( withIdPath ) );
        doc._listSceneObjectNode.push_back( noIdEntity );
        SW_EXPECT_FALSE( doc.saveXml( test::makeTempPath( "no_id_written.scene.xml" ) ) );
        SW_EXPECT_EQUAL( 0u, sw::SceneCooker::cookEntityState( doc ) );
    }
}

/**
 * @brief [SceneTest] 바이너리 씬이 말하는 엔티티 수를 그대로 믿지 않는다
 * @details 개수는 **파일에서 온 값**이다. 검사 없이 `reserve` 로 넘기면 엔티티 하나가 문자열 넷이라
 *          4,294,967,295 개면 수백 기가짜리 요청이 된다. 읽기는 어차피 그 아래에서 실패하지만, 그 전에 할당이 먼저 터진다.
 */
SW_TEST_CASE( SceneTest, BinaryEntityCountIsBoundedByFileSize )
{
    const sw::string binPath = test::makeTempPath( "sw_test_scene_badcount.bin" );
    // 멀쩡한 씬 하나를 쿠킹하고, 헤더의 엔티티 수만 터무니없는 값으로 바꾼다.
    sw::SceneDocument doc{};
    doc._name = "BoundedScene";
    sw::SceneDocument::SceneObjectNode node{};
    node._name   = "Root";
    node._fileId = 3;
    doc._listSceneObjectNode.push_back( std::move( node ) );
    SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

    sw::vector<uint8> bytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( binPath, bytes ) );

    // magic(4) + version(4) + name(4 + len) 다음이 엔티티 수다.
    const size_t nameLengthOffset = 8;
    SW_ASSERT_TRUE( bytes.size() > nameLengthOffset + 4 );
    uint32 nameLength{ 0 };
    sw::Memory::copy( &nameLength, bytes.data() + nameLengthOffset, sizeof( uint32 ) );
    const size_t countOffset = nameLengthOffset + 4 + nameLength;
    SW_ASSERT_TRUE( bytes.size() >= countOffset + 4 );

    const uint32 absurdCount = 0xFFFFFFFFu;
    sw::Memory::copy( bytes.data() + countOffset, &absurdCount, sizeof( uint32 ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( binPath, bytes.data(), static_cast<uint64>( bytes.size() ) ) );

    sw::SceneDocument corrupted{};
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( corrupted.loadBinary( binPath ) );
    }
    SW_EXPECT_TRUE( corrupted._listSceneObjectNode.empty() );
}

/**
 * @brief [SceneTest] 씬을 저장하면 다른 오브젝트에 붙은 자식 오브젝트도 남고, 다시 읽으면 같은 부모 아래 같은 자리에 붙는다
 * @details 오브젝트 상태에는 자식 목록이 없다 — 자식은 자기 엔티티로 적히고 `_attachOwner` 로 되붙는다(`instantiate` 의 두 번째 단계).
 *          그래서 저장이 부모가 있는 오브젝트를 건너뛰면 계층 아래의 오브젝트는 저장할 때마다 파일에서 사라진다. 언리얼 레벨 · 유니티 씬
 *          (`m_Father`)처럼 계층 전체를 적는다.
 */
SW_TEST_CASE( SceneTest, SavedSceneKeepsChildObjects )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "HierarchyWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );

    sw::GameObject* pParent     = pObjects->createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject* pChild      = pObjects->createGameObject( sw::hashed_string( "Child" ) );
    sw::GameObject* pGrandChild = pObjects->createGameObject( sw::hashed_string( "GrandChild" ) );
    SW_ASSERT_NOT_NULL( pParent->addComponent<sw::SceneComponent>() );
    sw::SceneComponent* pChildSc = pChild->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pChildSc );
    SW_ASSERT_NOT_NULL( pGrandChild->addComponent<sw::SceneComponent>() );
    pChildSc->setLocalPosition( sw::float3{ 1.0f, 2.0f, 3.0f } );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    SW_ASSERT_TRUE( pGrandChild->attachToParent( pChild ) );

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );
    SW_EXPECT_EQUAL( size_t( 3 ), saved._listSceneObjectNode.size() );

    sw::Scene* pReloaded = manager.createScene( "HierarchyWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( saved ) );
    sw::GameObjectManager* pReloadedObjects = pReloaded->getObjectManager();
    sw::GameObject*        pReloadedChild   = pReloadedObjects->findGameObjectByName( sw::hashed_string( "Child" ) );
    sw::GameObject*        pReloadedGrand   = pReloadedObjects->findGameObjectByName( sw::hashed_string( "GrandChild" ) );
    SW_ASSERT_NOT_NULL( pReloadedChild );
    SW_ASSERT_NOT_NULL( pReloadedGrand );
    SW_ASSERT_NOT_NULL( pReloadedChild->getParent() );
    SW_EXPECT_TRUE( pReloadedChild->getParent()->getName() == sw::hashed_string( "Parent" ) );
    SW_EXPECT_TRUE( pReloadedGrand->getParent() == pReloadedChild );
    SW_ASSERT_NOT_NULL( pReloadedChild->getPrimarySceneComponent() );
    SW_EXPECT_TRUE( pReloadedChild->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 1.0f, 2.0f, 3.0f ) );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 쿠킹은 쿠킹하지 못한 씬을 센다 — 하나라도 있으면 쿠킹이 실패다
 * @details 읽거나 쓰지 못한 씬을 건너뛰기만 하고 "쿠킹된 씬이 0 개" 일 때만 실패로 보면, 깨진 씬 하나가 배포본에서 빠지고 그 씬을 열 때에야
 *          "Shipping requires cooked binary scene" 으로 멈춘다. 쿠킹본 이름은 로더와 같은 규칙(`AssetCookPath`)이다.
 */
SW_TEST_CASE( SceneTest, SceneCookCountsTheScenesItCouldNotCook )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::string root   = test::makeTempDirectory( "scene_cook_root" );
    const sw::string cooked = test::makeTempDirectory( "scene_cook_out" );

    sw::SceneDocument good;
    good._name                = "Good";
    const sw::string goodPath = sw::FileUtil::joinPath( root, "game/demo/maps/good.scene.xml" );
    sw::FileUtil::ensureParentDirectoryExists( goodPath );
    SW_ASSERT_TRUE( good.saveXml( goodPath ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "game/demo/maps/broken.scene.xml" ), "<Scene name=\"Broken\"><Entity" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( root, "game/demo/maps/good.scene.xml.bak" ), "<Scene" ) ); // 쿠킹하는 것이 아니다

    uint32 failedCount = 0;
    {
        test::ScopedDefensiveTestLog expected( "a scene file that is not XML" );
        SW_EXPECT_EQUAL( 1u, sw::SceneCooker::cookAllScenes( root, cooked, failedCount ) );
    }
    SW_EXPECT_EQUAL( 1u, failedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( cooked, "game/demo/maps/good.scene.bin" ) ) );
}

/**
 * @brief [SceneTest] 모든 타입 공급자가 등록을 끝내기 전(기동 단계 `ModuleTypes` 전)에는 씬을 읽지 않는다 — 로드 요청도 쿠킹도 거절한다
 * @details GameFramework 타입이 오르기 전에 씬을 읽으면 그 컴포넌트가 `MissingComponent` 로 지어진다(씬 쿠킹 · 에디터 시작 씬). 읽는 쪽이 "타입 등록
 *          완료" 를 확인하므로 기동 순서가 어긋나면 씬이 조용히 망가지는 대신 오류로 멈춘다. 등록이 끝나기 전의 레지스트리를 서비스 표에 꽂아 본다.
 */
SW_TEST_CASE( SceneTest, SceneIsNotReadBeforeEveryModuleRegisteredItsTypes )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    const sw::string root   = test::makeTempDirectory( "scene_type_gate_root" );
    const sw::string cooked = test::makeTempDirectory( "scene_type_gate_out" );

    sw::SceneDocument doc;
    doc._name                  = "Gate";
    const sw::string scenePath = sw::FileUtil::joinPath( root, "game/demo/maps/gate.scene.xml" );
    sw::FileUtil::ensureParentDirectoryExists( scenePath );
    SW_ASSERT_TRUE( doc.saveXml( scenePath ) );
    const sw::string cookedPath = sw::FileUtil::joinPath( cooked, "game/demo/maps/gate.scene.bin" );

    // 하네스는 앱과 같은 기동 표를 지났다 — `ModuleTypes` 단계가 적어 두었다.
    SW_ASSERT_TRUE( sw::engine::getTypeRegistry().areAllModuleTypesRegistered() );

    const sw::EngineServices saved = sw::engine::getBoundEngineServices();
    sw::TypeRegistry         registryBeforeModuleTypes;
    sw::EngineServices       early = saved;
    early._pTypeRegistry           = &registryBeforeModuleTypes;
    test::rebindEngineServices( early );
    bool   bRequestAccepted{ true };
    uint32 earlyCookedCount{ 0 };
    uint32 earlyFailedCount{ 0 };
    {
        test::ScopedDefensiveTestLog expected( "a scene read before the ModuleTypes startup step" );
        bRequestAccepted = manager.requestLoadFuture( scenePath ).isValid();
        earlyCookedCount = sw::SceneCooker::cookAllScenes( root, cooked, earlyFailedCount );
    }
    test::rebindEngineServices( saved );

    SW_EXPECT_FALSE( bRequestAccepted );
    SW_EXPECT_EQUAL( 0u, earlyCookedCount );
    SW_EXPECT_EQUAL( 1u, earlyFailedCount );
    SW_EXPECT_FALSE( sw::FileUtil::exists( cookedPath ) );

    // 단계를 지난 레지스트리로는 같은 쿠킹이 된다.
    uint32 failedCount{ 0 };
    SW_EXPECT_EQUAL( 1u, sw::SceneCooker::cookAllScenes( root, cooked, failedCount ) );
    SW_EXPECT_EQUAL( 0u, failedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( cookedPath ) );
    manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 쿠킹은 활성 게임 팩에서 모르는 타입의 컴포넌트가 든 씬을 쿠킹하지 않고 실패로 센다 · 다른 게임 팩의 그런 씬은 건너뛴다
 * @details 모르는 타입은 `MissingComponent` 가 원문을 맡아 바이너리 왕복 검증을 통과하므로, 경고 한 줄로 넘기면 배포본에 동작하지 않는
 *          컴포넌트가 실린다(GameFramework 타입 없이 쿠킹된 spriteui 의 `HealthBarComponent` · `DamageNumberComponent` 등). 그 씬은 쓰지 않고, 실패가 빌드를 세운다.
 *          다른 게임의 팩은 그 게임 모듈의 컴포넌트를 쓴다 — 이 빌드에 없는 모듈이라 실패로 세면 다른 게임을 고른 빌드가 모두 선다. 배포본은 활성 팩만 연다.
 */
SW_TEST_CASE( SceneTest, SceneCookFailsOnAComponentOfUnknownType )
{
    struct ScopedActivePack
    {
        sw::GameConfig _previous{ sw::GameConfig::getActive() };
        explicit ScopedActivePack( const utf8* pPackRoot )
        {
            sw::GameConfig config = _previous;
            config._packRoot      = pPackRoot;
            sw::GameConfig::setActive( config );
        }
        ~ScopedActivePack() { sw::GameConfig::setActive( _previous ); }
        ScopedActivePack( const ScopedActivePack& )            = delete;
        ScopedActivePack& operator=( const ScopedActivePack& ) = delete;
    };
    const ScopedActivePack activePack( "game/demo" );

    const sw::string root   = test::makeTempDirectory( "scene_cook_unknown_root" );
    const sw::string cooked = test::makeTempDirectory( "scene_cook_unknown_out" );

    sw::string xml;
    {
        sw::GameObjectManager scratch;
        sw::GameObject*       pSource = scratch.createGameObject( sw::hashed_string( "Lamp" ) );
        SW_ASSERT_TRUE( pSource->addComponent<sw::SceneComponent>() != nullptr );
        xml                   = sw::ObjectStateSerializer::saveToXmlString( pSource );
        const size_t listOpen = xml.find( "<_listComponent>" );
        SW_ASSERT_TRUE( listOpen != sw::string::npos );
        xml.insert( listOpen + sw::string_view( "<_listComponent>" ).size(), "<NotLoadedCookLampDriver _flicker=\"0.25\" />" );
    }
    sw::SceneDocument doc;
    doc._name = "UnknownType";
    sw::SceneDocument::SceneObjectNode node{};
    node._name        = "Lamp";
    node._fileId      = 4;
    node._embeddedXml = xml;
    doc._listSceneObjectNode.push_back( node );
    const sw::string scenePath = sw::FileUtil::joinPath( root, "game/demo/maps/unknown.scene.xml" );
    sw::FileUtil::ensureParentDirectoryExists( scenePath );
    SW_ASSERT_TRUE( doc.saveXml( scenePath ) );
    const sw::string otherGamePath = sw::FileUtil::joinPath( root, "game/othergame/maps/unknown.scene.xml" );
    sw::FileUtil::ensureParentDirectoryExists( otherGamePath );
    SW_ASSERT_TRUE( doc.saveXml( otherGamePath ) );

    uint32 missingComponentCount{ 0 };
    uint32 cookedCount{ 0 };
    uint32 failedCount{ 0 };
    {
        test::ScopedDefensiveTestLog expected( "a scene with a component type that is not loaded" );
        sw::SceneDocument            direct = doc;
        (void)sw::SceneCooker::cookEntityState( direct, &missingComponentCount );
        cookedCount = sw::SceneCooker::cookAllScenes( root, cooked, failedCount );
    }
    SW_EXPECT_EQUAL( 1u, missingComponentCount );
    SW_EXPECT_EQUAL( 0u, cookedCount );
    SW_EXPECT_EQUAL( 1u, failedCount );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( cooked, "game/demo/maps/unknown.scene.bin" ) ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( cooked, "game/othergame/maps/unknown.scene.bin" ) ) );
}

/**
 * @brief [SceneTest] 쿠킹한 씬에서도 부모보다 앞에 적힌 자식이 부모의 소켓에 붙는다 — 쿠커가 연결을 지우지 않는다
 * @details 쿠커가 엔티티를 하나씩 따로 읽으면 부모가 문서에서 뒤에 있는 자식은 부모를 찾지 못하고, 바이너리로 쓸 때 그 연결이 지워진다(저장은
 *          살아 있는 부모 포인터에서 부착 필드를 다시 만든다). 컴포넌트 타입 목록만 보는 검증은 통과하고, 쿠킹한 바이너리만 읽는 배포본에서 자식이
 *          루트가 된다 — 로컬 위치가 월드 위치로 읽힌다. 에디터에서 자식을 먼저 만들고 부모 밑에 끌어 놓는 것만으로 생긴다.
 */
SW_TEST_CASE( SceneTest, CookedSceneKeepsAChildWrittenBeforeItsParent )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "CookOrderWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();

    sw::GameObject*     pSword = pObjects->createGameObject( sw::hashed_string( "Sword" ) ); // 먼저 만든다 — 문서에서 부모보다 앞에 적힌다
    sw::GameObject*     pHero  = pObjects->createGameObject( sw::hashed_string( "Hero" ) );
    sw::SceneComponent* pBlade = pSword->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pBody  = pHero->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pHand  = pHero->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pHand->attachToComponent( pBody ) );
    SW_ASSERT_TRUE( pBlade->attachToComponent( pHand ) );
    pBlade->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
    pObjects->mergePendingAdds();

    sw::SceneDocument doc;
    SW_ASSERT_TRUE( pScene->serializeToDocument( doc ) );
    SW_ASSERT_EQUAL( size_t( 2 ), doc._listSceneObjectNode.size() );
    SW_EXPECT_STREQ( "Sword", doc._listSceneObjectNode[0]._name.c_str() );
    SW_EXPECT_EQUAL( 2u, sw::SceneCooker::cookEntityState( doc ) );
    SW_EXPECT_TRUE( doc._listSceneObjectNode[0]._embeddedXml.empty() ); // 쿠킹된 상태로만 읽힌다

    // 배포본처럼 바이너리 씬 파일을 건너 읽는다 — 엔티티의 파일 id 도 파일에 실려야 한다.
    const sw::string cookedPath = test::makeTempPath( "cook_order.scene.bin" );
    SW_ASSERT_TRUE( doc.saveBinary( cookedPath ) );
    sw::SceneDocument fromFile{};
    SW_ASSERT_TRUE( fromFile.loadBinary( cookedPath ) );

    sw::Scene* pCooked = manager.createScene( "CookOrderWorldCooked" );
    SW_ASSERT_NOT_NULL( pCooked );
    SW_ASSERT_TRUE( pCooked->instantiate( fromFile ) );
    sw::GameObject* pLoadedSword = pCooked->getObjectManager()->findGameObjectByName( sw::hashed_string( "Sword" ) );
    sw::GameObject* pLoadedHero  = pCooked->getObjectManager()->findGameObjectByName( sw::hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pLoadedSword );
    SW_ASSERT_NOT_NULL( pLoadedHero );
    const sw::SceneComponent* pLoadedBlade = pLoadedSword->getPrimarySceneComponent();
    SW_ASSERT_NOT_NULL( pLoadedBlade );
    SW_ASSERT_NOT_NULL( pLoadedBlade->getParent() );
    SW_EXPECT_TRUE( pLoadedBlade->getParent()->getOwner() == pLoadedHero );
    SW_EXPECT_TRUE( pLoadedBlade->getParent() != pLoadedHero->getPrimarySceneComponent() ); // 몸통이 아니라 손(소켓)
    SW_EXPECT_TRUE( pLoadedBlade->getLocalPosition() == sw::float3( 0.0f, 1.0f, 0.0f ) );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 파일을 건넌 여러 줄 글은 줄바꿈을 지킨다
 * @details 엔티티 상태 서브트리는 XML 문서가 쓴다(`XmlNode::toString`). 씬 문서가 서브트리를 손으로 다시 쓰며 속성 값의 줄바꿈을 그대로
 *          적으면, 다시 읽는 XML 이 속성 값을 정규화해 줄바꿈이 공백이 된다 — 여러 줄 대사 · 설명이 씬을 열 때마다 한 줄이 된다.
 */
SW_TEST_CASE( SceneTest, MultiLineTextKeepsItsLineBreaksThroughASceneFile )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "LineBreakWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObject* pSign = pScene->getObjectManager()->createGameObject( sw::hashed_string( "Sign" ) );
    SW_ASSERT_NOT_NULL( pSign->addComponent<sw::SceneComponent>() );
    sw::SpriteAnimatorComponent* pAnimator = pSign->addComponent<sw::SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pAnimator );
    pAnimator->setCurrentAnimation( "first line\nsecond line" );
    pScene->getObjectManager()->mergePendingAdds();

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );
    const sw::string scenePath = test::makeTempPath( "line_breaks.scene.xml" );
    SW_ASSERT_TRUE( saved.saveXml( scenePath ) );
    sw::SceneDocument fromFile{};
    SW_ASSERT_TRUE( fromFile.loadXml( scenePath ) );

    sw::Scene* pReloaded = manager.createScene( "LineBreakWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( fromFile ) );
    sw::GameObject* pReloadedSign = pReloaded->getObjectManager()->findGameObjectByName( sw::hashed_string( "Sign" ) );
    SW_ASSERT_NOT_NULL( pReloadedSign );
    const sw::SpriteAnimatorComponent* pReloadedAnimator = pReloadedSign->getComponent<sw::SpriteAnimatorComponent>();
    SW_ASSERT_NOT_NULL( pReloadedAnimator );
    SW_EXPECT_STREQ( "first line\nsecond line", pReloadedAnimator->getCurrentAnimation().c_str() );
}

/**
 * @brief [SceneTest] 씬을 다시 열어 저장해도 엔티티의 파일 id 와 자식의 부모 참조가 그대로다 — 저장할 때마다 파일이 흔들리지 않는다
 * @details 부모는 파일 안의 id 로 가리킨다(유니티 fileID). 런타임 오브젝트 id 를 그대로 적으면 실행마다 값이 달라 저장할 때마다 모든 엔티티가
 *          바뀐 것처럼 보인다 — 씬이 런타임 id ↔ 파일 id 표를 들고 같은 값을 다시 쓴다. 상태 본문까지 바이트 단위로 같아야 한다.
 */
SW_TEST_CASE( SceneTest, FileIdsStayTheSameAcrossSaveAndReload )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "StableIdWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();
    sw::GameObject*        pParent  = pObjects->createGameObject( sw::hashed_string( "Parent" ) );
    sw::GameObject*        pChild   = pObjects->createGameObject( sw::hashed_string( "Child" ) );
    SW_ASSERT_NOT_NULL( pParent->addComponent<sw::SceneComponent>() );
    SW_ASSERT_NOT_NULL( pChild->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    pObjects->mergePendingAdds();

    sw::SceneDocument first;
    SW_ASSERT_TRUE( pScene->serializeToDocument( first ) );
    // 다른 오브젝트를 몇 개 더 만들어 런타임 id 를 밀어 둔다 — 다시 읽은 씬의 오브젝트는 다른 런타임 id 를 받는다.
    for ( uint32 extraIndex = 0; extraIndex < 3; ++extraIndex )
        SW_ASSERT_NOT_NULL( pObjects->createGameObject( sw::hashed_string( "Spacer" ) ) );

    // 저작 파일(XML)을 건너 다시 연다 — 엔티티의 `id` 속성이 실려야 한다.
    const sw::string scenePath = test::makeTempPath( "stable_ids.scene.xml" );
    SW_ASSERT_TRUE( first.saveXml( scenePath ) );
    sw::SceneDocument fromFile{};
    SW_ASSERT_TRUE( fromFile.loadXml( scenePath ) );

    sw::Scene* pReloaded = manager.createScene( "StableIdWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( fromFile ) );
    sw::GameObject* pReloadedChild  = pReloaded->getObjectManager()->findGameObjectByName( sw::hashed_string( "Child" ) );
    sw::GameObject* pReloadedParent = pReloaded->getObjectManager()->findGameObjectByName( sw::hashed_string( "Parent" ) );
    SW_ASSERT_NOT_NULL( pReloadedChild );
    SW_EXPECT_TRUE( pReloadedChild->getParent() == pReloadedParent ); // 파일 id 로 실제로 이어졌다
    sw::SceneDocument second;
    SW_ASSERT_TRUE( pReloaded->serializeToDocument( second ) );

    SW_ASSERT_EQUAL( first._listSceneObjectNode.size(), second._listSceneObjectNode.size() );
    for ( const sw::SceneDocument::SceneObjectNode& before : first._listSceneObjectNode )
    {
        SW_EXPECT_TRUE( before._fileId != 0 );
        const sw::SceneDocument::SceneObjectNode* pAfter = nullptr;
        for ( const sw::SceneDocument::SceneObjectNode& candidate : second._listSceneObjectNode )
        {
            if ( candidate._name == before._name )
                pAfter = &candidate;
        }
        SW_ASSERT_NOT_NULL( pAfter );
        SW_EXPECT_EQUAL( before._fileId, pAfter->_fileId );
        SW_EXPECT_STREQ( before._embeddedXml.c_str(), pAfter->_embeddedXml.c_str() );
    }

    manager.shutdown();
}

/**
 * @brief [SceneTest] 이름이 같은 엔티티 둘의 자식이 저마다 제 부모에 붙는다
 * @details 매니저는 이름을 유일하게 바꾸므로(`Enemy` → `Enemy_2`) 이름이 같은 엔티티가 든 문서(병합 · 손 편집)를 읽을 때 자식이 이름으로
 *          부모를 찾으면 모두 앞의 것에 붙고, 그대로 저장하면 그것이 굳는다. 부착은 부모의 파일 id 로 가리킨다.
 */
SW_TEST_CASE( SceneTest, ChildrenOfSameNamedEntitiesFindTheirOwnParent )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "TwinWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();

    sw::GameObject* pEnemyA = pObjects->createGameObject( sw::hashed_string( "Enemy" ) );
    sw::GameObject* pArmA   = pObjects->createGameObject( sw::hashed_string( "ArmA" ) );
    sw::GameObject* pEnemyB = pObjects->createGameObject( sw::hashed_string( "Enemy" ) );
    sw::GameObject* pArmB   = pObjects->createGameObject( sw::hashed_string( "ArmB" ) );
    for ( sw::GameObject* pObj : { pEnemyA, pArmA, pEnemyB, pArmB } )
        SW_ASSERT_NOT_NULL( pObj->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pEnemyB->getName() != sw::hashed_string( "Enemy" ) ); // 매니저가 유일하게 바꿨다
    pEnemyA->getPrimarySceneComponent()->setLocalPosition( sw::float3{ 1.0f, 0.0f, 0.0f } );
    pEnemyB->getPrimarySceneComponent()->setLocalPosition( sw::float3{ 2.0f, 0.0f, 0.0f } );
    SW_ASSERT_TRUE( pArmA->attachToParent( pEnemyA ) );
    SW_ASSERT_TRUE( pArmB->attachToParent( pEnemyB ) );

    sw::SceneDocument doc;
    SW_ASSERT_TRUE( pScene->serializeToDocument( doc ) );
    // 두 적의 이름을 같게 만든다(병합 · 손 편집으로 생기는 문서) — 상태의 이름 · 자식의 부모 이름까지.
    const sw::string renamed = pEnemyB->getName().c_str();
    for ( sw::SceneDocument::SceneObjectNode& entity : doc._listSceneObjectNode )
    {
        if ( entity._name == renamed )
            entity._name = "Enemy";
        for ( size_t found = entity._embeddedXml.find( renamed ); found != sw::string::npos; found = entity._embeddedXml.find( renamed ) )
            entity._embeddedXml.replace( found, renamed.size(), "Enemy" );
    }

    sw::Scene* pReloaded = manager.createScene( "TwinWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( doc ) );
    sw::GameObject* pLoadedArmA = pReloaded->getObjectManager()->findGameObjectByName( sw::hashed_string( "ArmA" ) );
    sw::GameObject* pLoadedArmB = pReloaded->getObjectManager()->findGameObjectByName( sw::hashed_string( "ArmB" ) );
    SW_ASSERT_NOT_NULL( pLoadedArmA );
    SW_ASSERT_NOT_NULL( pLoadedArmB );
    SW_ASSERT_NOT_NULL( pLoadedArmA->getParent() );
    SW_ASSERT_NOT_NULL( pLoadedArmB->getParent() );
    SW_EXPECT_TRUE( pLoadedArmA->getParent() != pLoadedArmB->getParent() );
    SW_EXPECT_TRUE( pLoadedArmB->getParent()->getPrimarySceneComponent()->getLocalPosition() == sw::float3( 2.0f, 0.0f, 0.0f ) );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 프리팹을 찾지 못한 엔티티의 자식은 저장해도 부모 참조를 잃지 않는다 — 프리팹이 돌아오면 다시 붙는다
 * @details 프리팹을 찾지 못한 엔티티는 오브젝트로 만들지 않고 문서 그대로 들고 있다가 다시 쓴다("Missing Prefab"). 그 **자식**은 읽을 때 부모가 없어
 *          루트로 남는데, 저장이 살아 있는 부모 포인터(없음)에서 부착 필드를 다시 만들면 연결이 지워져 프리팹을 되찾아 다시 열어도 자식은 루트다.
 *          찾지 못한 참조는 그대로 남는다(유니티의 missing reference).
 */
SW_TEST_CASE( SceneTest, ChildOfAMissingPrefabEntityKeepsItsParentReference )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "GhostParentWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();
    sw::GameObject*        pGhost   = pObjects->createGameObject( sw::hashed_string( "Ghost" ) );
    sw::GameObject*        pGun     = pObjects->createGameObject( sw::hashed_string( "Gun" ) );
    SW_ASSERT_NOT_NULL( pGhost->addComponent<sw::SceneComponent>() );
    SW_ASSERT_NOT_NULL( pGun->addComponent<sw::SceneComponent>() );
    SW_ASSERT_TRUE( pGun->attachToParent( pGhost ) );

    sw::SceneDocument authored;
    SW_ASSERT_TRUE( pScene->serializeToDocument( authored ) );
    sw::SceneDocument withMissingPrefab = authored;
    for ( sw::SceneDocument::SceneObjectNode& entity : withMissingPrefab._listSceneObjectNode )
    {
        if ( entity._name == "Ghost" )
            entity._prefab = "prefabs/test_missing_ghost_parent.prefab.xml";
    }

    sw::Scene* pOpened = manager.createScene( "GhostParentWorldOpened" );
    SW_ASSERT_NOT_NULL( pOpened );
    // 고스트의 **파일 id** 와 같은 **런타임 id** 를 가진 다른 오브젝트가 이 씬에 있다 — 파일 id 는 파일 안에서만 뜻이 있으니 이것에 붙으면 안 된다.
    uint64 ghostFileId = 0;
    for ( const sw::SceneDocument::SceneObjectNode& entity : withMissingPrefab._listSceneObjectNode )
    {
        if ( entity._name == "Ghost" )
            ghostFileId = entity._fileId;
    }
    SW_ASSERT_TRUE( ghostFileId != 0 );
    sw::GameObject* pSameNumber = pOpened->getObjectManager()->createGameObjectWithId( sw::hashed_string( "SameNumber" ), ghostFileId );
    SW_ASSERT_NOT_NULL( pSameNumber );
    SW_ASSERT_EQUAL( ghostFileId, pSameNumber->getObjectId() );
    SW_ASSERT_NOT_NULL( pSameNumber->addComponent<sw::SceneComponent>() );
    {
        test::ScopedDefensiveTestLog expected( "prefab of 'Ghost' does not exist and Gun cannot find its parent" );
        SW_ASSERT_TRUE( pOpened->instantiate( withMissingPrefab ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), pOpened->getUnresolvedEntityCount() );
    sw::GameObject* pOpenedGun = pOpened->getObjectManager()->findGameObjectByName( sw::hashed_string( "Gun" ) );
    SW_ASSERT_NOT_NULL( pOpenedGun );
    SW_EXPECT_NULL( pOpenedGun->getParent() );

    // 열고 저장한다 — 자식의 부모 참조가 그대로 남아야 한다. 같은 번호의 오브젝트는 지운다(저장 문서에 엔티티가 늘지 않게).
    pOpened->getObjectManager()->destroyObject( pSameNumber );
    pOpened->getObjectManager()->processDeferredDestruction();
    sw::SceneDocument resaved;
    SW_ASSERT_TRUE( pOpened->serializeToDocument( resaved ) );

    // 프리팹이 돌아왔다(여기서는 고스트를 다시 상태만 든 엔티티로) — 다시 열면 자식이 부모에 붙는다.
    for ( sw::SceneDocument::SceneObjectNode& entity : resaved._listSceneObjectNode )
    {
        if ( entity._name == "Ghost" )
            entity._prefab.clear();
    }
    sw::Scene* pRestored = manager.createScene( "GhostParentWorldRestored" );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_ASSERT_TRUE( pRestored->instantiate( resaved ) );
    sw::GameObject* pRestoredGun   = pRestored->getObjectManager()->findGameObjectByName( sw::hashed_string( "Gun" ) );
    sw::GameObject* pRestoredGhost = pRestored->getObjectManager()->findGameObjectByName( sw::hashed_string( "Ghost" ) );
    SW_ASSERT_NOT_NULL( pRestoredGun );
    SW_ASSERT_NOT_NULL( pRestoredGhost );
    SW_EXPECT_TRUE( pRestoredGun->getParent() == pRestoredGhost );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 다시 연 씬의 메시가 저장된 머티리얼 참조를 받는다 — 참조가 없는 메시만 씬 기본 머티리얼이다
 * @details 메시의 머티리얼이 저장되지 않으면 씬을 다시 열 때 모든 메시가 씬 기본 머티리얼이 된다. 씬 초기화(`Scene::initialize`)가 메시마다
 *          `_materialPath` 를 풀고, 참조가 없는 것에만 기본을 건다.
 */
SW_TEST_CASE( SceneTest, InitializedSceneBindsSavedMeshMaterials )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    constexpr const utf8* kPath = "engine/materials/benchtextured.material";

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "PaintedWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();
    sw::MeshComponent*     pPainted = pObjects->createGameObject( sw::hashed_string( "Painted" ) )->addComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pPainted );
    SW_ASSERT_NOT_NULL( pObjects->createGameObject( sw::hashed_string( "Plain" ) )->addComponent<sw::MeshComponent>() );
    pPainted->setMaterialPath( kPath );
    SW_ASSERT_NOT_NULL( pPainted->getMaterial() );

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );

    sw::Scene* pReloaded = manager.createScene( "PaintedWorldReloaded" );
    SW_ASSERT_NOT_NULL( pReloaded );
    SW_ASSERT_TRUE( pReloaded->instantiate( saved ) );
    SW_ASSERT_TRUE( pReloaded->initialize( nullptr ) );

    sw::GameObjectManager* pReloadedObjects = pReloaded->getObjectManager();
    sw::GameObject*        pReloadedPainted = pReloadedObjects->findGameObjectByName( sw::hashed_string( "Painted" ) );
    sw::GameObject*        pReloadedPlain   = pReloadedObjects->findGameObjectByName( sw::hashed_string( "Plain" ) );
    SW_ASSERT_NOT_NULL( pReloadedPainted );
    SW_ASSERT_NOT_NULL( pReloadedPlain );
    const sw::MeshComponent* pPaintedMesh = pReloadedPainted->getComponent<sw::MeshComponent>();
    const sw::MeshComponent* pPlainMesh   = pReloadedPlain->getComponent<sw::MeshComponent>();
    SW_ASSERT_NOT_NULL( pPaintedMesh );
    SW_ASSERT_NOT_NULL( pPlainMesh );
    SW_EXPECT_TRUE( pPaintedMesh->getMaterial() == pPainted->getMaterial() );
    SW_EXPECT_TRUE( pPaintedMesh->getMaterial() != pReloaded->getMaterial() );
    SW_EXPECT_TRUE( pPlainMesh->getMaterial() == pReloaded->getMaterial() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 영속으로 표시한 루트는 플레이 중 씬 전환 너머로 자식 · 정체(id) · 소켓 부착 그대로 넘어가고, 나머지는 사라진다
 * @details 유니티 `Object.DontDestroyOnLoad`(루트를 영속 씬으로 옮긴다) · 언리얼 심리스 트래블의 액터 목록 자리다.
 *          `DontDestroyOnLoadComponent` 가 태그만 붙이고 씬 전환이 그것을 읽지 않으면 씬을 바꿀 때 그 오브젝트도 같이 사라진다.
 *          플레이를 멈추면 표시를 잊는다(유니티는 플레이 모드를 나가면 영속 씬을 비운다).
 */
SW_TEST_CASE( SceneTest, PersistentRootsCarryIntoTheNextScene )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    manager.setWorldPlaying( true );
    sw::Scene* pFirst = manager.createEmptyActiveScene( "First" );
    SW_ASSERT_NOT_NULL( pFirst );
    sw::GameObjectManager* pObjects = pFirst->getObjectManager();

    sw::GameObject*     pKeeper      = pObjects->createGameObject( sw::hashed_string( "MusicPlayer" ) );
    sw::SceneComponent* pKeeperRoot  = pKeeper->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pKeeperMount = pKeeper->addComponent<sw::SceneComponent>(); // 소켓(주 컴포넌트가 아닌 씬 컴포넌트)
    SW_ASSERT_NOT_NULL( pKeeperRoot );
    SW_ASSERT_NOT_NULL( pKeeperMount );
    pKeeperRoot->setLocalPosition( sw::float3{ 1.0f, 2.0f, 3.0f } );
    pKeeperMount->setLocalPosition( sw::float3{ 0.0f, 0.0f, 5.0f } );
    SW_ASSERT_TRUE( pKeeperMount->attachToComponent( pKeeperRoot ) );

    sw::GameObject*     pSpeaker      = pObjects->createGameObject( sw::hashed_string( "Speaker" ) );
    sw::SceneComponent* pSpeakerScene = pSpeaker->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSpeakerScene );
    pSpeakerScene->setLocalPosition( sw::float3{ 0.0f, 1.0f, 0.0f } );
    SW_ASSERT_TRUE( pSpeakerScene->attachToComponent( pKeeperMount ) );
    SW_ASSERT_NOT_NULL( pObjects->createGameObject( sw::hashed_string( "Enemy" ) ) );

    manager.markPersistent( pKeeper );
    SW_EXPECT_TRUE( manager.isPersistent( pKeeper ) );
    // 루트가 아닌 것은 표시하지 않는다(유니티도 루트만 받는다).
    manager.markPersistent( pSpeaker );
    SW_EXPECT_FALSE( manager.isPersistent( pSpeaker ) );

    const uint64              keeperId      = pKeeper->getObjectId();
    const uint64              speakerId     = pSpeaker->getObjectId();
    const sw::ComponentHandle mountHandle   = pKeeperMount->getHandle();
    const sw::ComponentHandle speakerHandle = pSpeakerScene->getHandle();

    sw::Scene* pSecond = manager.createEmptyActiveScene( "Second" );
    SW_ASSERT_NOT_NULL( pSecond );
    sw::GameObjectManager* pNext           = pSecond->getObjectManager();
    sw::GameObject*        pCarried        = pNext->findGameObjectById( keeperId );
    sw::GameObject*        pCarriedSpeaker = pNext->findGameObjectById( speakerId );
    SW_ASSERT_NOT_NULL( pCarried );
    SW_ASSERT_NOT_NULL( pCarriedSpeaker );
    SW_EXPECT_TRUE( pCarried->getName() == sw::hashed_string( "MusicPlayer" ) );
    SW_EXPECT_TRUE( pNext->findGameObjectByName( sw::hashed_string( "Enemy" ) ) == nullptr );
    SW_EXPECT_TRUE( manager.isPersistent( pCarried ) );

    // 핸들이 이어지고, 자식은 같은 소켓에 붙어 있다.
    sw::SceneComponent* pCarriedMount        = static_cast<sw::SceneComponent*>( pNext->resolveComponent( mountHandle ) );
    sw::SceneComponent* pCarriedSpeakerScene = static_cast<sw::SceneComponent*>( pNext->resolveComponent( speakerHandle ) );
    SW_ASSERT_NOT_NULL( pCarriedMount );
    SW_ASSERT_NOT_NULL( pCarriedSpeakerScene );
    SW_EXPECT_TRUE( pCarriedSpeakerScene->getParent() == pCarriedMount );
    pNext->flushSceneTransforms();
    const sw::float3 speakerWorld = pCarriedSpeakerScene->getWorldPosition();
    SW_EXPECT_TRUE( sw::MathUtil::nearEqual( speakerWorld._x, 1.0f ) && sw::MathUtil::nearEqual( speakerWorld._y, 3.0f ) &&
                    sw::MathUtil::nearEqual( speakerWorld._z, 8.0f ) );

    // 다음 전환에도 넘어간다.
    sw::Scene* pThird = manager.createEmptyActiveScene( "Third" );
    SW_ASSERT_NOT_NULL( pThird );
    SW_EXPECT_TRUE( pThird->getObjectManager()->findGameObjectById( keeperId ) != nullptr );

    // 플레이를 멈추면 표시를 잊는다. 편집 중에는 표시를 받지 않는다.
    manager.setWorldPlaying( false );
    sw::GameObject* pEditTime = pThird->getObjectManager()->createGameObject( sw::hashed_string( "EditTime" ) );
    manager.markPersistent( pEditTime );
    SW_EXPECT_FALSE( manager.isPersistent( pEditTime ) );
    // 다음 플레이에서 씬을 바꿔도 지난 플레이의 표시로 옮겨 가지 않는다.
    manager.setWorldPlaying( true );
    sw::Scene* pFourth = manager.createEmptyActiveScene( "Fourth" );
    SW_ASSERT_NOT_NULL( pFourth );
    SW_EXPECT_TRUE( pFourth->getObjectManager()->findGameObjectById( keeperId ) == nullptr );
    manager.setWorldPlaying( false );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 저장된 상태가 있는 프리팹 인스턴스는 그 상태로 **한 번** 짓는다 — 프리팹 상태를 지었다가 통째로 덮지 않는다
 * @details 씬은 프리팹 인스턴스도 전체 상태를 저장하고, 읽을 때 그 상태가 기준이다(덮어쓴 값 · 지운 컴포넌트까지). 프리팹을 먼저 스폰한
 *          (컴포넌트를 모두 만든) 위에 저장된 상태를 읽으면 컴포넌트를 모두 지우고 다시 만들어 인스턴스마다 두 번 짓는다.
 *          프리팹이 있는지는 본다(없으면 "Missing Prefab" 으로 남긴다), 저장할 때 프리팹 경로도 그대로 적는다.
 */
SW_TEST_CASE( SceneTest, PrefabInstanceWithSavedStateIsBuiltOnce )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string prefabPath = test::makeTempPath( "lifecycle.prefab.xml" );
    sw::string       savedState;
    {
        sw::GameObjectManager authoring;
        sw::RegisterMockComponents();
        sw::GameObject* pSource = authoring.createGameObject( sw::hashed_string( "Lifecycle" ) );
        SW_ASSERT_NOT_NULL( pSource->addComponent<sw::MockPoolLifecycleComponent>() );
        sw::PrefabAsset asset;
        asset.setFromGameObject( pSource );
        SW_ASSERT_TRUE( asset.saveToXmlFile( prefabPath ) );
        // 배포본은 쿠킹본만 읽는다.
        SW_ASSERT_TRUE( asset.saveToBinaryFile( sw::FileUtil::replaceExtension( prefabPath, ".bin" ) ) );
        savedState = sw::ObjectStateSerializer::saveToXmlString( pSource );
    }

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "PrefabOnceWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::RegisterMockComponents();

    sw::SceneDocument                  doc;
    sw::SceneDocument::SceneObjectNode entity;
    entity._name        = "Lifecycle";
    entity._fileId      = 5;
    entity._prefab      = prefabPath;
    entity._embeddedXml = savedState;
    doc._listSceneObjectNode.push_back( entity );

    const int32 constructedBefore = sw::MockPoolLifecycleComponent::s_ctorCount.load();
    SW_ASSERT_TRUE( pScene->instantiate( doc ) );
    SW_EXPECT_EQUAL( 1, sw::MockPoolLifecycleComponent::s_ctorCount.load() - constructedBefore );
    SW_EXPECT_EQUAL( size_t( 0 ), pScene->getUnresolvedEntityCount() );

    sw::GameObject* pInstance = pScene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Lifecycle" ) );
    SW_ASSERT_NOT_NULL( pInstance );
    SW_EXPECT_TRUE( pInstance->getComponent<sw::MockPoolLifecycleComponent>() != nullptr );
    SW_EXPECT_STREQ( prefabPath.c_str(), pScene->getEntityPrefabPath( pInstance->getObjectId() ).c_str() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 프리팹을 찾지 못한 엔티티도 저장하면 그대로 남는다(유니티의 "Missing Prefab" 과 같은 자리)
 * @details 풀지 못한 엔티티는 문서 그대로 들고 있다가 저장 때 다시 써 넣는다. 스폰 실패 때 엔티티를 버리면 씬을 열고 저장하는 것만으로
 *          그 엔티티 · 덮어쓴 값 · 프리팹 GUID 가 파일에서 영영 사라진다 — 프리팹을 `.meta` 없이 옮겼거나 잠깐 없던 것만으로.
 */
SW_TEST_CASE( SceneTest, EntityWhosePrefabIsMissingSurvivesSave )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "MissingPrefabWorld" );
    SW_ASSERT_NOT_NULL( pScene );

    sw::SceneDocument                  doc;
    sw::SceneDocument::SceneObjectNode ghost;
    ghost._name        = "Ghost";
    ghost._fileId      = 6;
    ghost._prefab      = "prefabs/test_missing_for_scene_test.prefab.xml";
    ghost._prefabGuid  = "0b7c2a9e-4f1d-4c3a-9e8b-1d2c3b4a5f60";
    ghost._embeddedXml = "<GameObject _name=\"Ghost\" />";
    doc._listSceneObjectNode.push_back( ghost );
    sw::SceneDocument::SceneObjectNode plain;
    plain._name   = "Plain";
    plain._fileId = 7;
    doc._listSceneObjectNode.push_back( plain );

    {
        test::ScopedDefensiveTestLog expected( "prefab of 'Ghost' does not exist" );
        SW_EXPECT_TRUE( pScene->instantiate( doc ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), pScene->getUnresolvedEntityCount() );

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );
    const sw::SceneDocument::SceneObjectNode* pSavedGhost = nullptr;
    for ( const sw::SceneDocument::SceneObjectNode& node : saved._listSceneObjectNode )
    {
        if ( node._name == "Ghost" )
            pSavedGhost = &node;
    }
    SW_ASSERT_NOT_NULL( pSavedGhost );
    SW_EXPECT_STREQ( ghost._prefab.c_str(), pSavedGhost->_prefab.c_str() );
    SW_EXPECT_STREQ( ghost._prefabGuid.c_str(), pSavedGhost->_prefabGuid.c_str() );
    SW_EXPECT_STREQ( ghost._embeddedXml.c_str(), pSavedGhost->_embeddedXml.c_str() );
    SW_EXPECT_EQUAL( size_t( 2 ), saved._listSceneObjectNode.size() );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 깨진 씬 파일은 "없다" 가 아니라 어디가 틀렸는지(`경로:줄:열`)로 알린다
 * @details `SceneDocument::loadXml` 이 읽기 실패를 모두 "File not found" 로 알리면 파일이 바로 거기 있어도 그렇게 나오고, 구문 오류의 자리는
 *          XML 로그의 오프셋뿐이다.
 */
SW_TEST_CASE( SceneTest, BrokenSceneFileSaysWhereNotFileNotFound )
{
    const sw::string path = test::makeTempPath( "broken.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( path, "<Scene name=\"Broken\">\n  <Entity>\n</Scene>\n" ) );

    test::ScopedLogCollector logs;
    {
        test::ScopedDefensiveTestLog expected( "a scene file with a syntax error" );
        sw::SceneDocument            document;
        SW_EXPECT_FALSE( document.loadXml( path ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "broken.scene.xml:3:" ) > 0, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "not found" ) == 0, logs.joined().c_str() );

    // 정말 없는 파일은 없다고 한다.
    {
        test::ScopedDefensiveTestLog expected( "a scene file that does not exist" );
        sw::SceneDocument            document;
        SW_EXPECT_FALSE( document.loadXml( test::makeTempPath( "missing.scene.xml" ) ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "missing.scene.xml: not found" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [SceneTest] 엔진 서비스 없이도 XML 씬 · 프리팹을 읽고, 지원하는 것보다 새 형식 · 올릴 단계가 없는 옛 판은 그때도 거절한다
 * @details 단독 도구 · 테스트는 서비스를 묶지 않고 에셋을 읽는다. 로더가 `getAssetManager()` 로 형식 등록부를 꺼내면 거기서 assert 다 —
 *          같은 함수의 GUID 블록과 바이너리 로더는 이미 서비스가 있는지 묻는다. 서비스가 없을 때는 내장 migrator 만 든 등록부로 판정한다.
 */
SW_TEST_CASE( SceneTest, XmlAssetsLoadWithoutEngineServices )
{
    const sw::string scenePath = test::makeTempPath( "standalone.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( scenePath, "<Scene formatVersion=\"1\" name=\"Standalone\">\n"
                                                            "  <entities>\n"
                                                            "    <entity id=\"1\" name=\"Crate\" prefab=\"game/demo/prefabs/crate.prefab.xml\"/>\n"
                                                            "  </entities>\n"
                                                            "</Scene>\n" ) );
    const sw::string futureScenePath = test::makeTempPath( "future.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( futureScenePath, "<Scene formatVersion=\"999\" name=\"Future\"/>\n" ) );
    // 판 속성이 없는 문서는 0 판이고, 0 판을 1 판으로 올리는 단계는 없다 — 저장소의 씬은 모두 1 판이다.
    const sw::string oldScenePath = test::makeTempPath( "unversioned.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( oldScenePath, "<Scene name=\"Unversioned\"/>\n" ) );
    const sw::string prefabPath = test::makeTempPath( "standalone.prefab.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( prefabPath, "<Prefab name=\"Crate\">\n  <GameObject _name=\"Crate\"/>\n</Prefab>\n" ) );

    /** @brief 이 범위 동안 엔진 서비스를 풀고, 나갈 때 원래 표로 되묶는다(단언이 일찍 나가도). */
    struct ScopedUnboundEngineServices
    {
        sw::EngineServices _saved;

        ScopedUnboundEngineServices()
            : _saved{ sw::engine::getBoundEngineServices() }
        {
            sw::engine::unbindEngineServices();
        }

        ~ScopedUnboundEngineServices() { test::rebindEngineServices( _saved ); }
    };

    SW_ASSERT_TRUE( sw::engine::areEngineServicesBound() );
    {
        const ScopedUnboundEngineServices unbound;
        SW_ASSERT_FALSE( sw::engine::areEngineServicesBound() );

        sw::SceneDocument document;
        SW_EXPECT_TRUE( document.loadXml( scenePath ) );
        SW_EXPECT_EQUAL( sw::string( "Standalone" ), document._name );
        SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), document._listSceneObjectNode.size() );
        SW_EXPECT_EQUAL( sw::string( "game/demo/prefabs/crate.prefab.xml" ), document._listSceneObjectNode[0]._prefab );

        {
            test::ScopedDefensiveTestLog expected( "a scene file newer than this build" );
            sw::SceneDocument            futureDocument;
            SW_EXPECT_FALSE( futureDocument.loadXml( futureScenePath ) );
        }
        {
            test::ScopedDefensiveTestLog expected( "a scene file of a version no migrator upgrades" );
            sw::SceneDocument            oldDocument;
            SW_EXPECT_FALSE( oldDocument.loadXml( oldScenePath ) );
        }

        sw::PrefabAsset prefab;
        SW_EXPECT_TRUE( prefab.loadFromXmlFile( prefabPath ) );
    }
    SW_EXPECT_TRUE( sw::engine::areEngineServicesBound() );
}

/**
 * @brief [SceneTest] 컴포넌트 이름표는 씬 파일(XML) · 쿠킹한 씬(SCN1 바이너리 상태)을 건너 남고, 그 이름표로 가리킨 부착도 그 자리에 붙는다
 * @details 이름표는 오브젝트 안에서 컴포넌트를 가리키는 키다(언리얼의 컴포넌트 이름 자리). 저장하지 않으면 씬을 다시 열 때 기본값(타입 이름)으로
 *          돌아가, 이름표로 찾던 게임 코드와 그 키로 적힌 부착이 다른 컴포넌트를 본다.
 */
SW_TEST_CASE( SceneTest, ComponentNameSurvivesSceneFilesAndCooking )
{
    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::Scene* pScene = manager.createScene( "NamedComponentWorld" );
    SW_ASSERT_NOT_NULL( pScene );
    sw::GameObjectManager* pObjects = pScene->getObjectManager();

    sw::GameObject*     pGun    = pObjects->createGameObject( sw::hashed_string( "Gun" ) );
    sw::SceneComponent* pBody   = pGun->addComponent<sw::SceneComponent>();
    sw::SceneComponent* pMuzzle = pGun->addComponent<sw::SceneComponent>();
    SW_ASSERT_TRUE( pBody != nullptr && pMuzzle != nullptr );
    pMuzzle->setComponentName( sw::hashed_string( "Muzzle" ) );
    sw::GameObject*     pFlash     = pObjects->createGameObject( sw::hashed_string( "Flash" ) );
    sw::SceneComponent* pFlashRoot = pFlash->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pFlashRoot );
    SW_ASSERT_TRUE( pFlashRoot->attachToComponent( pMuzzle ) );

    /** @brief 다시 지은 씬의 이름표 · 부착이 처음과 같은지 봅니다. */
    struct Check
    {
        static void run( const sw::Scene* pBuilt, const utf8* pWhere )
        {
            const sw::GameObjectManager* pBuiltObjects = pBuilt->getObjectManager();
            sw::GameObject*              pBuiltGun     = pBuiltObjects->findGameObjectByName( sw::hashed_string( "Gun" ) );
            sw::GameObject*              pBuiltFlash   = pBuiltObjects->findGameObjectByName( sw::hashed_string( "Flash" ) );
            SW_ASSERT_TRUE_MSG( pBuiltGun != nullptr && pBuiltFlash != nullptr, pWhere );
            SW_ASSERT_TRUE_MSG( pBuiltGun->getComponents().size() == 2, pWhere );
            const sw::Component* pBuiltMuzzle = pBuiltGun->getComponents()[1];
            SW_EXPECT_TRUE_MSG( pBuiltGun->getComponents()[0]->getComponentName() == sw::hashed_string( "SceneComponent" ), pWhere );
            SW_EXPECT_TRUE_MSG( pBuiltMuzzle->getComponentName() == sw::hashed_string( "Muzzle" ), pWhere );
            const sw::SceneComponent* pBuiltFlashRoot = pBuiltFlash->getPrimarySceneComponent();
            SW_ASSERT_TRUE_MSG( pBuiltFlashRoot != nullptr, pWhere );
            SW_EXPECT_TRUE_MSG( pBuiltFlashRoot->getParent() == pBuiltMuzzle, pWhere );
        }
    };

    sw::SceneDocument saved;
    SW_ASSERT_TRUE( pScene->serializeToDocument( saved ) );
    const sw::string xmlPath = test::makeTempPath( "named_components.scene.xml" );
    SW_ASSERT_TRUE( saved.saveXml( xmlPath ) );

    sw::SceneDocument fromXml;
    SW_ASSERT_TRUE( fromXml.loadXml( xmlPath ) );
    sw::Scene* pFromXml = manager.createScene( "NamedComponentWorldXml" );
    SW_ASSERT_TRUE( pFromXml->instantiate( fromXml ) );
    Check::run( pFromXml, "xml" );

    // 쿠커는 엔티티 상태를 리플렉션 바이너리로 쿠킹한다 — 그 길에도 실려야 배포본이 같은 이름표를 본다.
    SW_EXPECT_EQUAL( 2u, sw::SceneCooker::cookEntityState( fromXml ) );
    const sw::string binPath = test::makeTempPath( "named_components.scene.bin" );
    SW_ASSERT_TRUE( fromXml.saveBinary( binPath ) );
    sw::SceneDocument fromBinary;
    SW_ASSERT_TRUE( fromBinary.loadBinary( binPath ) );
    SW_ASSERT_TRUE( fromBinary._listSceneObjectNode.empty() == false && fromBinary._listSceneObjectNode[0]._embeddedStateBytes.empty() == false );
    sw::Scene* pFromBinary = manager.createScene( "NamedComponentWorldBinary" );
    SW_ASSERT_TRUE( pFromBinary->instantiate( fromBinary ) );
    Check::run( pFromBinary, "binary" );

    manager.shutdown();
}

/**
 * @brief [SceneTest] 저장소의 실제 씬 · 프리팹이 모두 읽히고(이름표가 없는 옛 파일 — 기본값은 타입 이름, 이름표를 적은 파일 — 그 이름), 이름표를 달아
 *        다시 쓰면 그대로 돌아온다
 */
SW_TEST_CASE( SceneTest, RepositoryScenesAndPrefabsKeepComponentNames )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    sw::vector<sw::string> listFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( sw::ResourceUtil::getRootFolderPath(), "", listFile, true ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    uint32 sceneCount  = 0;
    uint32 prefabCount = 0;
    for ( const sw::string& filePath : listFile )
    {
        const sw::string path    = sw::FileUtil::normalizeSeparators( filePath );
        const bool       bScene  = sw::StringUtil::endsWith( path, ".scene.xml", true );
        const bool       bXml    = sw::StringUtil::endsWith( path, ".prefab.xml", true );
        const bool       bJson   = sw::StringUtil::endsWith( path, ".prefab.json", true );
        const sw::string tempTag = "repo_" + sw::to_string( sceneCount + prefabCount );
        if ( bScene )
        {
            ++sceneCount;
            sw::SceneDocument opened;
            SW_ASSERT_TRUE_MSG( opened.loadXml( path ), path.c_str() );
            sw::Scene* pOpened = manager.createScene( "RepositoryScene" );
            SW_ASSERT_TRUE_MSG( pOpened->instantiate( opened ), path.c_str() );
            const uint32 sceneLabelCount = sw::SceneTestInternal::renameEveryComponent( pOpened->getObjectManager() );
            if ( sw::SceneTestInternal::isLabelledFile( path ) == false )
                SW_EXPECT_TRUE_MSG( sceneLabelCount == 0, path.c_str() );
            const sw::vector<sw::string> listExpected = sw::SceneTestInternal::collectComponentNames( pOpened->getObjectManager() );

            sw::SceneDocument saved;
            SW_ASSERT_TRUE( pOpened->serializeToDocument( saved ) );
            const sw::string savedPath = test::makeTempPath( tempTag + ".scene.xml" );
            SW_ASSERT_TRUE( saved.saveXml( savedPath ) );
            sw::SceneDocument reread;
            SW_ASSERT_TRUE( reread.loadXml( savedPath ) );
            sw::Scene* pReopened = manager.createScene( "RepositorySceneReopened" );
            SW_ASSERT_TRUE( pReopened->instantiate( reread ) );
            SW_EXPECT_TRUE_MSG( sw::SceneTestInternal::collectComponentNames( pReopened->getObjectManager() ) == listExpected, path.c_str() );
            continue;
        }
        if ( ( bXml || bJson ) == false )
            continue;

        ++prefabCount;
        sw::PrefabAsset prefab;
        SW_ASSERT_TRUE_MSG( bJson ? prefab.loadFromJsonFile( path ) : prefab.loadFromXmlFile( path ), path.c_str() );
        sw::GameObjectManager authoring;
        sw::GameObject*       pAuthored = authoring.createGameObject( sw::hashed_string( "RepositoryPrefab" ) );
        SW_ASSERT_TRUE_MSG( prefab.applyStateTo( pAuthored ), path.c_str() );
        const uint32 prefabLabelCount = sw::SceneTestInternal::renameEveryComponent( &authoring );
        if ( sw::SceneTestInternal::isLabelledFile( path ) == false )
            SW_EXPECT_TRUE_MSG( prefabLabelCount == 0, path.c_str() );
        const sw::vector<sw::string> listExpected = sw::SceneTestInternal::collectComponentNames( &authoring );

        sw::PrefabAsset resaved;
        resaved.setFromGameObject( pAuthored );
        const sw::string savedPath = test::makeTempPath( tempTag + ( bJson ? ".prefab.json" : ".prefab.xml" ) );
        SW_ASSERT_TRUE( resaved.saveToFile( savedPath ) );
        sw::PrefabAsset reread;
        SW_ASSERT_TRUE( bJson ? reread.loadFromJsonFile( savedPath ) : reread.loadFromXmlFile( savedPath ) );
        sw::GameObjectManager rebuilt;
        sw::GameObject*       pRebuilt = rebuilt.createGameObject( sw::hashed_string( "RepositoryPrefab" ) );
        SW_ASSERT_TRUE( reread.applyStateTo( pRebuilt ) );
        SW_EXPECT_TRUE_MSG( sw::SceneTestInternal::collectComponentNames( &rebuilt ) == listExpected, path.c_str() );
    }
    SW_EXPECT_TRUE_MSG( sceneCount >= 2 && prefabCount >= 1, "no repository scene or prefab was found - this check saw nothing" );
    manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 문서의 씬 · 엔티티 값은 속성에서만 읽는다 — 같은 이름의 자식 텍스트(`<name>`)는 저장하는 모양이 아니라 읽지 않는다
 */
SW_TEST_CASE( SceneTest, SceneDocumentReadsValuesOnlyFromAttributes )
{
    const sw::string scenePath = test::makeTempPath( "childtext.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( scenePath, "<Scene formatVersion=\"1\">\n"
                                                            "  <name>FromChild</name>\n"
                                                            "  <entities>\n"
                                                            "    <entity id=\"1\"><name>Crate</name><prefab>game/demo/prefabs/crate.prefab.xml</prefab></entity>\n"
                                                            "  </entities>\n"
                                                            "</Scene>\n" ) );
    sw::SceneDocument document;
    SW_ASSERT_TRUE( document.loadXml( scenePath ) );
    SW_EXPECT_STREQ( "childtext.scene", document._name.c_str() ); // 이름 속성이 없으면 파일 이름이다
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), document._listSceneObjectNode.size() );
    SW_EXPECT_STREQ( "Entity", document._listSceneObjectNode[0]._name.c_str() );
    SW_EXPECT_TRUE( document._listSceneObjectNode[0]._prefab.empty() );
}
