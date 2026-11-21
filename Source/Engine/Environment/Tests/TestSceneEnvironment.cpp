#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Environment/SceneEnvironment.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    struct SceneEnvironmentTestUtil
    {
        /** @brief 늘 안개를 내는 시험 원천입니다(밀도로 누가 답했는지 가린다). */
        static bool writeTestFog( const float3& /*viewPosition*/, FogSettings& outSettings )
        {
            outSettings          = FogSettings{};
            outSettings._density = 0.25f;
            return true;
        }
    };
} // namespace

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
 * @brief [SceneEnvironmentTest] 안개 칸은 맡는 쪽이 하나다 — 묶인 칸에 둘째를 묶으면 거절하고, 비운 뒤 묶은 원천이 답한다
 * @details 변이: `bindFogSource` 의 이미 묶임 검사를 빼면 첫 단언(거절)이 진다.
 */
SW_TEST_CASE( SceneEnvironmentTest, SecondFogSourceIsRejectedUntilUnbound )
{
    GameObjectManager       manager;
    SceneEnvironment&       environment = manager.getSubsystem<SceneEnvironment>();
    const FogSourceDelegate source      = SW_DELEGATE_FUNCTION( FogSourceDelegate, &SceneEnvironmentTestUtil::writeTestFog );

    SW_EXPECT_LOG( "Environment.FogSourceAlreadyBound" );
    SW_EXPECT_FALSE( environment.bindFogSource( source ) );

    environment.unbindFogSource();
    FogSettings settings;
    SW_EXPECT_FALSE( environment.findFog( float3{}, settings ) );
    SW_REQUIRE_TRUE( environment.bindFogSource( source ) );
    SW_EXPECT_TRUE( environment.findFog( float3{}, settings ) );
    SW_EXPECT_NEAR( 0.25f, settings._density, 1.0e-6f );
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
