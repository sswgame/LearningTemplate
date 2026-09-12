#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Renderer/Light/GpuLightBuffer.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) SceneTest — 활성 씬·비동기 로드
// ------------------------------------------------------------------------------
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
    SW_EXPECT_FALSE( doc._listEntityNode.empty() );

    bool bFoundPrefab{ false };
    for ( const sw::SceneDocument::EntityNode& node : doc._listEntityNode )
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

    sw::SceneDocument::EntityNode node{};
    node._name              = "HeroInstance";
    node._prefab            = "prefabs/old_hero.prefab.xml";
    const sw::Uuid heroGuid = sw::Uuid::generate();
    node._prefabGuid        = heroGuid.toString();
    doc._listEntityNode.push_back( node );

    const sw::string tempSceneXml = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() ), "temp_guid_scene.scene.xml" );
    SW_ASSERT_TRUE( doc.saveXml( tempSceneXml ) );

    if ( sw::engine::areEngineServicesBound() )
        sw::engine::getResourceManager().getAssetDatabase().registerMapping( "prefabs/new_hero.prefab.xml", heroGuid );

    sw::SceneDocument loadedDoc{};
    SW_ASSERT_TRUE( loadedDoc.loadXml( tempSceneXml ) );
    SW_ASSERT_TRUE( loadedDoc._listEntityNode.empty() == false );
    SW_EXPECT_STREQ( "HeroInstance", loadedDoc._listEntityNode[0]._name.c_str() );
    SW_EXPECT_STREQ( heroGuid.toString().c_str(), loadedDoc._listEntityNode[0]._prefabGuid.c_str() );
    if ( sw::engine::areEngineServicesBound() )
        SW_EXPECT_STREQ( "prefabs/new_hero.prefab.xml", loadedDoc._listEntityNode[0]._prefab.c_str() );

    sw::FileUtil::removeFile( tempSceneXml );
}

/**
 * @brief [SceneTest] 커밋된 테스트 씬의 TestProp 은 옛 경로(old/)를 가리키지만 GUID 로 실제 프리팹을 찾는다.
 * @details 시작 시점 AssetDatabase(.meta 스캔 / 배포본은 assetregistry.txt)가 있어야 통과한다 — 로드된 적 없는
 *          에셋의 GUID 를 알아야 하므로. 배포본의 같은 경로는 SCN1 이 prefabGuid 를 실어야 열린다(쿠커가 그것을
 *          빠뜨리고 있었다). GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( SceneTest, EditorTestSceneResolvesMovedPrefabByGuid )
{
    sw::ResourceUtil::initialize();
    sw::SceneDocument doc{};
    SW_ASSERT_TRUE( doc.load( "game/empty/maps/editortest.scene.xml" ) );

    bool bFound = false;
    for ( const sw::SceneDocument::EntityNode& node : doc._listEntityNode )
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
 * @details 예전에는 매 프레임 **모든 GameObject** 를 훑어 주광을 찾았다(중단도 없었다). 등록부로
 *          바꾸면서 조회 결과가 달라지지 않는지 고정한다 — 없음 · 있음 · 비활성 · 파괴 네 상태다.
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
 * @brief [SceneTest] 그림자 행렬의 **깊이 범위**가 씬을 실제로 담는다
 * @details 그림자가 한 번도 진 적이 없었다. 직교 투영의 near/far 를 `(-거리, +거리)` 로 잡고 있었는데,
 *          라이트 카메라는 원점에서 **거리만큼 떨어져** 원점을 보므로 씬의 뷰 z 는 거리 언저리다 —
 *          `z' = (z_view - near)/(far - near)` 로는 씬 전체가 z' ≈ 1(원평면)에 뭉쳤고, 깊이 비교가
 *          늘 "가려지지 않음" 이 됐다. 로그도 경고도 없었고 그림에서만 드러난다(그림자를 받을 바닥이
 *          없으면 그마저도 안 보인다). 그래서 **행렬 자체**를 여기서 고정한다.
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
