#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Water/BuoyancyComponent.h"
#include "Engine/Environment/Water/WaterBodyComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsShape.h"

#include "TestFramework/TestFramework.h"

// 부력 — 구 폰툰의 잠긴 부피 비율(구 캡) · 부력 크기 · 호수 위 상자가 잠긴 비율이 질량과 맞는 높이에 뜨고, 물 밖 상자는 떨어진다.

using namespace sw;

namespace
{
    struct TestBuoyancyInternal
    {
        static constexpr float32 kFrame  = 1.0f / 60.0f;
        static constexpr float32 kRadius = 0.5f;

        /** @brief 폰툰 하나(중심, 반지름 0.5)를 단 상자입니다. */
        static RigidBodyComponent* spawnFloater( GameObjectManager& manager, const utf8* pName, const float3& position, float32 mass )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            RigidBodyComponent* pBody  = pObject->addComponent<RigidBodyComponent>();
            BuoyancyComponent*  pFloat = pObject->addComponent<BuoyancyComponent>();
            if ( pBody == nullptr || pFloat == nullptr )
                return nullptr;
            PhysicsShapeDesc3D box;
            box._halfExtents = float3{ 0.4f, 0.4f, 0.4f };
            pBody->setShape( box );
            pBody->setMass( mass );
            pBody->setLocalPosition( position );
            BuoyancyPontoon pontoon;
            pontoon._radius = kRadius;
            pFloat->setPontoons( { pontoon } );
            pFloat->setLinearDrag( 1500.0f );
            return pBody;
        }
    };
} // namespace

/**
 * @brief [BuoyancyTest] 잠긴 비율은 구 캡 부피다 — 수면이 바닥이면 0, 중심이면 절반, 꼭대기면 1, 반지름 절반 아래면 5/32 · 부력은 밀도 × 중력 × 잠긴 부피다
 */
SW_TEST_CASE( BuoyancyTest, SubmergedFractionIsTheSphereCapVolume )
{
    SW_EXPECT_NEAR_EQUAL( 0.0f, BuoyancyUtil::computeSubmergedFraction( -2.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, BuoyancyUtil::computeSubmergedFraction( -1.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, BuoyancyUtil::computeSubmergedFraction( 0.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, BuoyancyUtil::computeSubmergedFraction( 1.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, BuoyancyUtil::computeSubmergedFraction( 5.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 5.0f / 32.0f, BuoyancyUtil::computeSubmergedFraction( -1.0f, 2.0f ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, BuoyancyUtil::computeSubmergedFraction( 0.0f, 0.0f ), 1.0e-6f );

    // 반지름 0.5 구가 다 잠기면 1000 × 9.81 × (4/3)π(0.125) ≈ 5136.3 N.
    SW_EXPECT_NEAR_EQUAL( 5136.3f, BuoyancyUtil::computeBuoyantForce( 1.0f, 0.5f, 1000.0f, 9.81f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 2568.2f, BuoyancyUtil::computeBuoyantForce( 0.5f, 0.5f, 1000.0f, 9.81f ), 0.5f );
}

/**
 * @brief [BuoyancyTest] 호수 위 상자는 무게와 부력이 같은 높이에 멈추고(폰툰 부피의 절반 무게면 중심이 수면), 물 밖 상자는 떨어진다
 * @details 상자의 질량은 폰툰 부피 × 밀도의 절반이다. 그러면 평형에서 잠긴 비율이 0.5 이고 중심이 수면(y = 1)에 온다.
 *          물을 덮지 않는 (30, z) 의 상자는 부력을 받지 않는다.
 */
SW_TEST_CASE( BuoyancyTest, CrateFloatsWhereBuoyancyMatchesItsWeight )
{
    using Internal = TestBuoyancyInternal;
    GameObjectManager manager;
    GameObject*       pLake = manager.createGameObject( hashed_string( "Lake" ) );
    SW_ASSERT_NOT_NULL( pLake );
    WaterBodyComponent* pWater = pLake->addComponent<WaterBodyComponent>();
    SW_ASSERT_NOT_NULL( pWater );
    pWater->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } );
    pWater->setSize( float2{ 40.0f, 40.0f } );
    pWater->setWaves( {} ); // 잔잔한 수면 — 평형 높이를 정확히 본다
    pWater->setMaterialPath( "" );
    pWater->rebuildSurface();

    const float32       halfMass  = 0.5f * 1000.0f * 4.0f / 3.0f * MathUtil::kPi * Internal::kRadius * Internal::kRadius * Internal::kRadius;
    RigidBodyComponent* pFloating = Internal::spawnFloater( manager, "Floating", float3{ 0.0f, 1.3f, 0.0f }, halfMass );
    RigidBodyComponent* pOutside  = Internal::spawnFloater( manager, "Outside", float3{ 30.0f, 1.3f, 0.0f }, halfMass );
    SW_ASSERT_NOT_NULL( pFloating );
    SW_ASSERT_NOT_NULL( pOutside );

    manager.beginPlay();
    for ( uint32 frame = 0; frame < 300; ++frame )
    {
        manager.tick( Internal::kFrame );
    }
    SW_EXPECT_NEAR_EQUAL( 1.0f, pFloating->getWorldPosition()._y, 0.02f );
    SW_EXPECT_TRUE( pFloating->getOwner()->getComponent<BuoyancyComponent>()->isInWater() );
    SW_EXPECT_TRUE( pOutside->getWorldPosition()._y < -10.0f );
    SW_EXPECT_FALSE( pOutside->getOwner()->getComponent<BuoyancyComponent>()->isInWater() );
}
