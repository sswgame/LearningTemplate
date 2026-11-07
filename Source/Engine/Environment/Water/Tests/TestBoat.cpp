#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Foliage/WindComponent.h"
#include "Engine/Environment/Water/BoatComponent.h"
#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

// 배 — 키 힘의 부호 · 돛 각 · 돛 힘(순수 함수), 잔잔한 호수에서 노로 나아가기 · 키로 오른쪽 선회 · 강물에 밀리기 · 순풍과 역풍의 돛(물리 바디).

using namespace sw;

namespace
{
    struct TestBoatInternal
    {
        static constexpr float32 kFrame = 1.0f / 60.0f;

        /** @brief 잔잔한 물 하나를 세웁니다(수면 y = 0). 강이면 +x 로 흐르는 넓은 띠입니다. */
        static WaterBodyComponent* spawnWater( GameObjectManager& manager, bool bRiver, float32 flowSpeed )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Water" ) );
            if ( pObject == nullptr )
                return nullptr;
            WaterBodyComponent* pWater = pObject->addComponent<WaterBodyComponent>();
            if ( pWater == nullptr )
                return nullptr;
            if ( bRiver )
            {
                pWater->setShape( WaterBodyShape::River );
                pWater->setRiverPath( {
                                          float3{-200.0f, 0.0f, 0.0f},
                                          float3{ 200.0f, 0.0f, 0.0f}
                },
                                      60.0f );
            }
            else
            {
                pWater->setSize( float2{ 400.0f, 400.0f } );
            }
            pWater->setFlow( flowSpeed, 0.0f );
            pWater->setWaves( {} );
            pWater->setMaterialPath( "" );
            pWater->rebuildSurface();
            return pWater;
        }

        /** @brief 6 m 선체(600 kg)에 폰툰 여섯, 배 컴포넌트를 단 배입니다. @p yaw 만큼 돌려 둡니다(0 이면 선수 +z). */
        static BoatComponent* spawnBoat( GameObjectManager& manager, const utf8* pName, const float3& position, float32 yaw )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody  = pObject->addComponent<RigidBodyComponent>();
            BuoyancyComponent*  pFloat = pObject->addComponent<BuoyancyComponent>();
            BoatComponent*      pBoat  = pObject->addComponent<BoatComponent>();
            if ( pBody == nullptr || pFloat == nullptr || pBoat == nullptr )
                return nullptr;
            PhysicsShapeDesc3D hull;
            hull._halfExtents = float3{ 1.0f, 0.35f, 3.0f };
            pBody->setShape( hull );
            pBody->setMass( 600.0f );
            pBody->setLocalPosition( position );
            pBody->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
            vector<BuoyancyPontoon> listPontoon;
            for ( const float32 z : { -2.2f, 0.0f, 2.2f } )
            {
                for ( const float32 x : { -0.7f, 0.7f } )
                {
                    BuoyancyPontoon pontoon;
                    pontoon._localPosition = float3{ x, -0.1f, z };
                    pontoon._radius        = 0.45f;
                    listPontoon.push_back( pontoon );
                }
            }
            pFloat->setPontoons( listPontoon );
            pFloat->setLinearDrag( 120.0f );
            return pBoat;
        }

        static void step( GameObjectManager& manager, uint32 frameCount )
        {
            for ( uint32 frame = 0; frame < frameCount; ++frame )
            {
                manager.update( kFrame );
            }
        }

        /** @brief 순풍 1, 옆바람 0.8, 비스듬한 역풍 0.3, 정면 0 의 돛 곡선입니다. */
        static FloatCurve makeSailCurve()
        {
            FloatCurve curve;
            (void)curve.addKey( 0.0f, 1.0f, CurveInterpolation::Linear );
            (void)curve.addKey( 0.5f * MathUtil::kPi, 0.8f, CurveInterpolation::Linear );
            (void)curve.addKey( 0.75f * MathUtil::kPi, 0.3f, CurveInterpolation::Linear );
            (void)curve.addKey( MathUtil::kPi, 0.0f, CurveInterpolation::Linear );
            return curve;
        }
    };
} // namespace

/**
 * @brief [BoatTest] 키 힘은 앞 속도와 조타 각의 사인에 비례하고 뒤로 가면 부호가 바뀐다 · 돛 각은 순풍 0 · 옆바람 π/2 · 역풍 π · 돛 힘은 ½ρv²A × 효율이다
 */
SW_TEST_CASE( BoatTest, RudderAndSailMath )
{
    SW_EXPECT_NEAR( 150.0f * 4.0f * MathUtil::sin( 0.5f ), BoatComponent::computeRudderForce( 150.0f, 4.0f, 0.5f ), 1.0e-3f );
    SW_EXPECT_NEAR( -150.0f * 4.0f * MathUtil::sin( 0.5f ), BoatComponent::computeRudderForce( 150.0f, -4.0f, 0.5f ), 1.0e-3f );
    SW_EXPECT_NEAR( 0.0f, BoatComponent::computeRudderForce( 150.0f, 0.0f, 0.5f ), 1.0e-6f );

    const float3 forward{ 0.0f, 0.0f, 1.0f };
    float32      angle{ 0.0f };
    SW_REQUIRE_TRUE( BoatComponent::computeSailAngle( forward, float3{ 0.0f, 0.0f, 5.0f }, angle ) );
    SW_EXPECT_NEAR( 0.0f, angle, 1.0e-4f );
    SW_REQUIRE_TRUE( BoatComponent::computeSailAngle( forward, float3{ 5.0f, 3.0f, 0.0f }, angle ) ); // 수직 성분은 보지 않는다
    SW_EXPECT_NEAR( 0.5f * MathUtil::kPi, angle, 1.0e-4f );
    SW_REQUIRE_TRUE( BoatComponent::computeSailAngle( forward, float3{ 0.0f, 0.0f, -5.0f }, angle ) );
    SW_EXPECT_NEAR( MathUtil::kPi, angle, 1.0e-4f );
    SW_EXPECT_FALSE( BoatComponent::computeSailAngle( forward, float3{ 0.0f, 2.0f, 0.0f }, angle ) );

    SW_EXPECT_NEAR( 0.5f * BoatComponent::kAirDensity * 100.0f * 20.0f * 0.8f, BoatComponent::computeSailForce( 20.0f, 10.0f, 0.8f ), 1.0e-3f );
}

/**
 * @brief [BoatTest] 잔잔한 호수 — 노를 저으면 선수 쪽으로 나아가고, 키를 오른쪽으로 꺾으면 오른쪽(+x)으로 돌며 선회 반경이 유한하다. 물 밖으로 들린 배는 힘을 받지 않는다
 */
SW_TEST_CASE( BoatTest, OarsDriveForwardAndRudderTurnsRight )
{
    using Internal = TestBoatInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::spawnWater( manager, false, 0.0f ) );
    BoatComponent* pBoat = Internal::spawnBoat( manager, "Boat", float3{ 0.0f, 0.3f, -150.0f }, 0.0f );
    SW_REQUIRE_NOT_NULL( pBoat );
    pBoat->setEngineThrust( 1500.0f );
    manager.beginPlay();
    Internal::step( manager, 120 ); // 내려앉는다

    SW_EXPECT_TRUE( pBoat->getState()._submergedRatio > 0.5f );
    SW_EXPECT_NEAR( 0.0f, pBoat->getState()._forwardSpeed, 0.1f );

    for ( uint32 frame = 0; frame < 600; ++frame )
    {
        pBoat->setHelmInput( BoatHelmInput{ 1.0f, 0.0f } );
        manager.update( Internal::kFrame );
    }
    const float32 straightSpeed = pBoat->getState()._forwardSpeed;
    SW_EXPECT_TRUE( straightSpeed > 2.0f );
    SW_EXPECT_TRUE( straightSpeed < 8.0f );
    SW_EXPECT_TRUE( pBoat->getOwner()->getPrimarySceneComponent()->getWorldPosition()._z > -140.0f );
    SW_EXPECT_NEAR( 0.0f, pBoat->getOwner()->getPrimarySceneComponent()->getWorldPosition()._x, 0.5f );

    const float32 startX = pBoat->getOwner()->getPrimarySceneComponent()->getWorldPosition()._x;
    for ( uint32 frame = 0; frame < 120; ++frame )
    {
        pBoat->setHelmInput( BoatHelmInput{ 1.0f, 1.0f } );
        manager.update( Internal::kFrame );
    }
    const BoatState& state = pBoat->getState();
    SW_EXPECT_TRUE( state._yawRate > 0.05f );
    SW_EXPECT_TRUE( pBoat->getOwner()->getPrimarySceneComponent()->getWorldPosition()._x > startX + 0.5f );
    const float32 turnRadius = state._forwardSpeed / state._yawRate;
    SW_EXPECT_TRUE( turnRadius > 2.0f );
    SW_EXPECT_TRUE( turnRadius < 40.0f );
}

/**
 * @brief [BoatTest] 강물(+x 1.5 m/s)에 둔 배는 입력 없이도 흐름을 따라 +x 로 밀리고, 흐름 속도에 다가간다
 */
SW_TEST_CASE( BoatTest, CurrentPushesTheBoatDownstream )
{
    using Internal = TestBoatInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::spawnWater( manager, true, 1.5f ) );
    BoatComponent* pBoat = Internal::spawnBoat( manager, "Drifter", float3{ -100.0f, 0.3f, 0.0f }, 0.5f * MathUtil::kPi );
    SW_REQUIRE_NOT_NULL( pBoat );
    manager.beginPlay();
    Internal::step( manager, 600 );

    const float3 position = pBoat->getOwner()->getPrimarySceneComponent()->getWorldPosition();
    SW_EXPECT_TRUE( position._x > -100.0f + 8.0f ); // 10 초 × 1.5 m/s = 15 m 에서 처음 가속 몫을 뺀 만큼
    SW_EXPECT_TRUE( position._x < -100.0f + 15.5f );
    SW_EXPECT_NEAR( 0.0f, position._z, 1.0f );
}

/**
 * @brief [BoatTest] +x 로 부는 바람(10 m/s) — 바람을 등진 배(선수 +x)는 돛으로 나아가고, 바람을 마주한 배(선수 −x)는 거의 서 있다
 */
SW_TEST_CASE( BoatTest, SailRunsBeforeTheWindButNotIntoIt )
{
    using Internal = TestBoatInternal;
    GameObjectManager manager;
    SW_REQUIRE_NOT_NULL( Internal::spawnWater( manager, false, 0.0f ) );
    GameObject* pWindObject = manager.createGameObject( hashed_string( "Wind" ) );
    SW_REQUIRE_NOT_NULL( pWindObject );
    WindComponent* pWind = pWindObject->addComponent<WindComponent>();
    SW_REQUIRE_NOT_NULL( pWind );
    pWind->setDirection( 0.0f ); // +x 쪽으로
    pWind->setSpeed( 10.0f );

    BoatComponent* pDownwind = Internal::spawnBoat( manager, "Downwind", float3{ -100.0f, 0.3f, 50.0f }, 0.5f * MathUtil::kPi );
    BoatComponent* pUpwind   = Internal::spawnBoat( manager, "Upwind", float3{ 100.0f, 0.3f, -50.0f }, -0.5f * MathUtil::kPi );
    SW_REQUIRE_NOT_NULL( pDownwind );
    SW_REQUIRE_NOT_NULL( pUpwind );
    pDownwind->setSail( 30.0f, Internal::makeSailCurve() );
    pUpwind->setSail( 30.0f, Internal::makeSailCurve() );
    manager.beginPlay();
    Internal::step( manager, 120 );
    for ( uint32 frame = 0; frame < 900; ++frame )
    {
        pDownwind->setHelmInput( BoatHelmInput{ 1.0f, 0.0f } );
        pUpwind->setHelmInput( BoatHelmInput{ 1.0f, 0.0f } );
        manager.update( Internal::kFrame );
    }
    SW_EXPECT_TRUE( pDownwind->getState()._sailForce > 100.0f );
    SW_EXPECT_TRUE( pDownwind->getState()._forwardSpeed > 1.5f );
    SW_EXPECT_NEAR( 0.0f, pUpwind->getState()._sailForce, 1.0f );
    SW_EXPECT_TRUE( pUpwind->getState()._forwardSpeed < 0.3f );
}
