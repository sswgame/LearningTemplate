#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Environment/SceneEnvironment.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

/**
 * @brief [SceneEnvironmentTest] 렌더러가 읽는 하늘 칸이 하늘 찾기와 같은 답을 낸다 — 하늘이 없으면 false, 활성 하늘이 있으면 그 값이다(에디터 정지 중에도 묻는 칸이라 새 값)
 */
SW_TEST_CASE( SceneEnvironmentTest, SkySlotMatchesTheComponentLookup )
{
    GameObjectManager       manager;
    const SceneEnvironment& environment = manager.getSubsystem<SceneEnvironment>();
    SkyGradient             gradient;
    SW_EXPECT_FALSE( environment.findSky( gradient ) );

    GameObject*   pObject = manager.createGameObject( hashed_string( "Sky" ) );
    SkyComponent* pSky    = pObject != nullptr ? pObject->addComponent<SkyComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pSky );
    pSky->setIntensity( 3.0f );

    SW_EXPECT_TRUE( environment.findSky( gradient ) );
    SW_EXPECT_NEAR( 3.0f, gradient._intensity, 1.0e-6f );
}

/**
 * @brief [SceneEnvironmentTest] 렌더러가 읽는 안개가 안개 찾기와 같은 답을 낸다 — 안개가 없으면 false, 안개 컴포넌트가 있으면 그 값이다
 * @details 변이: `findFog` 가 컴포넌트를 찾지 않으면 둘째 단언이 진다.
 */
SW_TEST_CASE( SceneEnvironmentTest, FogMatchesTheComponentLookup )
{
    GameObjectManager       manager;
    const SceneEnvironment& environment = manager.getSubsystem<SceneEnvironment>();
    FogSettings             settings;
    SW_EXPECT_FALSE( environment.findFog( float3{}, settings ) );

    GameObject*   pObject = manager.createGameObject( hashed_string( "Fog" ) );
    FogComponent* pFog    = pObject != nullptr ? pObject->addComponent<FogComponent>() : nullptr;
    SW_REQUIRE_NOT_NULL( pFog );
    pFog->setDensity( 0.25f );

    SW_EXPECT_TRUE( environment.findFog( float3{}, settings ) );
    SW_EXPECT_NEAR( 0.25f, settings._density, 1.0e-6f );
}

/**
 * @brief [SceneEnvironmentTest] 환경 시간은 씬이 도는 동안 흐르고 씬을 비우면 0 에서 다시 시작한다 — 모든 식생이 이 한 값을 읽는다(컴포넌트마다 따로 세지 않는다)
 */
SW_TEST_CASE( SceneEnvironmentTest, EnvironmentTimeFlowsWithTheScene )
{
    GameObjectManager manager;
    SceneEnvironment& environment = manager.getSubsystem<SceneEnvironment>();
    SW_EXPECT_NEAR( 0.0f, environment.collectMaterialGlobals()._environment._w, 1.0e-6f );
    environment.update( 0.25f );
    environment.update( 0.5f );
    SW_EXPECT_NEAR( 0.75f, environment.collectMaterialGlobals()._environment._w, 1.0e-6f );
    manager.clear();
    SW_EXPECT_NEAR( 0.0f, environment.collectMaterialGlobals()._environment._w, 1.0e-6f );
}

/**
 * @brief [SceneEnvironmentTest] 표면 변형 값 칸 — 쓴 값을 읽고, 씬을 비우면 층이 없다
 */
SW_TEST_CASE( SceneEnvironmentTest, SurfaceDeformValueIsClearedWithTheScene )
{
    GameObjectManager    manager;
    SceneEnvironment&    environment = manager.getSubsystem<SceneEnvironment>();
    SurfaceDeformBinding binding;
    SW_EXPECT_FALSE( environment.findSurfaceDeform( binding ) );

    SurfaceDeformBinding written;
    written._params = float4{ 1.0f, 2.0f, 0.5f, 3.0f };
    environment.setSurfaceDeform( written );
    SW_EXPECT_TRUE( environment.findSurfaceDeform( binding ) );
    SW_EXPECT_NEAR( 3.0f, binding._params._w, 1.0e-6f );

    manager.clear();
    SW_EXPECT_FALSE( environment.findSurfaceDeform( binding ) );
    SW_EXPECT_NEAR( 0.0f, binding._params._w, 1.0e-6f );
}
