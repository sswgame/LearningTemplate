// 내비메시 군중 — 좁은 복도에서 마주 오는 두 에이전트가 서로 비켜 지나 각자 목적지에 닿는다(DetourCrowd 회피 · 떨어지기).
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"

#include "EngineTest/NavMeshTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct NavMeshCrowdTestInternal
    {
        static float32 computeDistance2D( const sw::float3& from, const sw::float3& to )
        {
            const float32 deltaX = to._x - from._x;
            const float32 deltaZ = to._z - from._z;
            return sw::MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        }
    };
} // namespace

/**
 * @brief [NavMeshCrowdTest] 폭 3 m 복도의 두 끝에서 마주 보고 출발한 두 에이전트(반지름 0.4)가 겹치지 않고 비켜 지나 서로의 출발점에 닿는다
 */
SW_TEST_CASE( NavMeshCrowdTest, TwoAgentsPassEachOtherInACorridor )
{
    using Internal                     = NavMeshCrowdTestInternal;
    const sw::NavMeshSettings settings = navtest::makeSettings();
    sw::NavMeshGeometry       geometry;
    navtest::addFloor( geometry, 0.0f, 0.0f, 20.0f, 3.0f );
    sw::unique_ptr<sw::INavMesh> pNavMesh = navtest::bake( settings, geometry );
    SW_ASSERT_NOT_NULL( pNavMesh.get() );
    sw::unique_ptr<sw::INavCrowd> pCrowd = pNavMesh->createCrowd( 8, 1.0f );
    SW_ASSERT_NOT_NULL( pCrowd.get() );
    pCrowd->setQueryFilter( settings.makeDefaultFilter() );

    sw::NavCrowdAgentParams params;
    params._radius           = 0.4f;
    params._height           = 1.8f;
    params._maxSpeed         = 3.0f;
    params._avoidanceQuality = sw::NavAvoidanceQuality::Good;
    const sw::float3          westStart{ 2.0f, 0.0f, 1.5f };
    const sw::float3          eastStart{ 18.0f, 0.0f, 1.5f };
    const sw::NavCrowdAgentID west = pCrowd->addAgent( westStart, params );
    const sw::NavCrowdAgentID east = pCrowd->addAgent( eastStart, params );
    SW_ASSERT_TRUE( west != sw::NavigationConstant::kInvalidAgentID && east != sw::NavigationConstant::kInvalidAgentID );
    SW_EXPECT_EQUAL( 2u, pCrowd->getActiveAgentCount() );
    SW_ASSERT_TRUE( pCrowd->requestMoveTarget( west, eastStart ) );
    SW_ASSERT_TRUE( pCrowd->requestMoveTarget( east, westStart ) );

    float32                minDistance = 1.0e9f;
    float32                maxSideStep = 0.0f;
    sw::NavCrowdAgentState westState;
    sw::NavCrowdAgentState eastState;
    for ( uint32 frame = 0; frame < 600; ++frame )
    {
        pCrowd->update( 1.0f / 60.0f );
        SW_ASSERT_TRUE( pCrowd->findAgentState( west, westState ) && pCrowd->findAgentState( east, eastState ) );
        minDistance = sw::MathUtil::min( minDistance, Internal::computeDistance2D( westState._position, eastState._position ) );
        maxSideStep = sw::MathUtil::max( maxSideStep, sw::MathUtil::abs( westState._position._z - eastState._position._z ) );
    }
    // 반지름 합 0.8 — 회피가 없으면 같은 줄을 따라 걸어 0 까지 붙는다.
    SW_EXPECT_TRUE_MSG( minDistance > 0.6f, ( "closest approach " + std::to_string( minDistance ) ).c_str() );
    SW_EXPECT_TRUE( maxSideStep > 0.5f );
    SW_EXPECT_TRUE( Internal::computeDistance2D( westState._position, eastStart ) < 0.5f );
    SW_EXPECT_TRUE( Internal::computeDistance2D( eastState._position, westStart ) < 0.5f );
    SW_EXPECT_TRUE( westState._moveState == sw::NavCrowdMoveState::Arrived );

    // 목적지를 내비메시에서 찾지 못하면 실패로 남는다.
    SW_EXPECT_FALSE( pCrowd->requestMoveTarget( west, sw::float3{ 50.0f, 0.0f, 50.0f } ) );
    SW_ASSERT_TRUE( pCrowd->findAgentState( west, westState ) );
    SW_EXPECT_TRUE( westState._moveState == sw::NavCrowdMoveState::Failed );
    pCrowd->removeAgent( west );
    SW_EXPECT_EQUAL( 1u, pCrowd->getActiveAgentCount() );
}
