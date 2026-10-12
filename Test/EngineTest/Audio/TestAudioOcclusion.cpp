#include "pch.h"

#include "Engine/Audio/AudioRoomGraph.h"
#include "Engine/Object/Component/Audio/Physics2DAudioOcclusionQuery.h"
#include "Engine/Physics/IPhysicsQueryScene.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// AudioOcclusionTest — 2D 물리 레이 가림 · 방/포털 그래프 가림
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 세로 벽 하나(x = 벽 위치, y 구간 [yMin, yMax])만 있는 가짜 2D 질의 씬입니다. */
    class WallQueryScene2D final : public sw::IPhysicsQueryScene2D
    {
    public:
        WallQueryScene2D( float32 wallX, float32 yMin, float32 yMax )
            : _wallX{ wallX }
            , _yMin{ yMin }
            , _yMax{ yMax }
        {
        }

        bool raycast( const sw::float2& origin, const sw::float2& direction, float32 maxDistance, const sw::PhysicsQueryFilter& /*filter*/,
                      sw::PhysicsCastHit2D& outHit ) const override
        {
            if ( sw::MathUtil::abs( direction._x ) < 1e-6f )
                return false;
            const float32 distance = ( _wallX - origin._x ) / direction._x;
            if ( distance < 0.0f || maxDistance < distance )
                return false;
            const float32 y = origin._y + direction._y * distance;
            if ( y < _yMin || _yMax < y )
                return false;
            outHit._distance = distance;
            return true;
        }

        bool shapeCast( const sw::PhysicsShapeDesc2D& /*shape*/, const sw::float2& /*position*/, const float32& /*rotation*/, const sw::float2& /*direction*/, float32 /*maxDistance*/,
                        const sw::PhysicsQueryFilter& /*filter*/, sw::PhysicsCastHit2D& /*outHit*/ ) const override
        {
            return false;
        }

        uint32 overlapShape( const sw::PhysicsShapeDesc2D& /*shape*/, const sw::float2& /*position*/, const float32& /*rotation*/, const sw::PhysicsQueryFilter& /*filter*/,
                             sw::vector<sw::PhysicsBodyHandle>& /*outListBody*/ ) const override
        {
            return 0;
        }

    private:
        float32 _wallX;
        float32 _yMin;
        float32 _yMax;
    };
} // namespace

/**
 * @brief [AudioOcclusionTest] 2D 물리 가림 — 벽이 사이에 있으면 1, 없으면 0, 레이 셋 중 하나만 막히면 1/3, 씬이 없으면 0
 */
SW_TEST_CASE( AudioOcclusionTest, Physics2DRayQueryCountsBlockedRays )
{
    sw::Physics2DAudioOcclusionQuery query;
    SW_EXPECT_NEAR_EQUAL( 0.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-6f ); // 씬 없음

    const WallQueryScene2D fullWall( 5.0f, -10.0f, 10.0f );
    query.setScene( &fullWall, 0xFFFFFFFFu );
    SW_EXPECT_NEAR_EQUAL( 1.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 4.0f, 0.0f, 0.0f ) ), 1e-6f );  // 벽 앞
    SW_EXPECT_NEAR_EQUAL( 0.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 0.0f, 50.0f ) ), 1e-6f ); // Z 는 평면 밖이라 무시한다

    // 벽이 위쪽 레이(+0.4)만 가린다.
    const WallQueryScene2D partialWall( 5.0f, 0.2f, 5.0f );
    query.setScene( &partialWall, 0xFFFFFFFFu );
    SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-5f );

    // 리스너 껍질(0.3 m) 안의 벽은 세지 않는다 — 서 있는 캐릭터 자신이다.
    const WallQueryScene2D nearWall( 0.1f, -10.0f, 10.0f );
    query.setScene( &nearWall, 0xFFFFFFFFu );
    SW_EXPECT_NEAR_EQUAL( 0.0f, query.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-6f );
}

/**
 * @brief [AudioOcclusionTest] 같은 방은 열려 있고, 이웃 방은 포털 열림이 가림을 정한다 — 열린 문 0.15, 닫힌 문 1
 */
SW_TEST_CASE( AudioOcclusionTest, RoomGraphUsesPortalOpenness )
{
    sw::AudioRoomGraph graph;
    const uint32       roomA = graph.addRoom( sw::float3( 5.0f, 0.0f, 0.0f ), sw::float3( 5.0f, 5.0f, 5.0f ) );  // x 0..10
    const uint32       roomB = graph.addRoom( sw::float3( 15.0f, 0.0f, 0.0f ), sw::float3( 5.0f, 5.0f, 5.0f ) ); // x 10..20
    const uint32       door  = graph.addPortal( roomA, roomB, 1.0f );
    SW_ASSERT_TRUE( door != sw::invalid_index::kUint32 );

    const sw::float3 inA( 2.0f, 0.0f, 0.0f );
    const sw::float3 inA2( 8.0f, 0.0f, 0.0f );
    const sw::float3 inB( 18.0f, 0.0f, 0.0f );
    SW_EXPECT_EQUAL( roomA, graph.findRoom( inA ) );
    SW_EXPECT_EQUAL( roomB, graph.findRoom( inB ) );
    SW_EXPECT_EQUAL( sw::AudioRoomGraph::kOutside, graph.findRoom( sw::float3( 50.0f, 0.0f, 0.0f ) ) );

    SW_EXPECT_NEAR_EQUAL( 0.0f, graph.computeOcclusion( inA, inA2 ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.15f, graph.computeOcclusion( inA, inB ), 1e-5f );

    SW_EXPECT_TRUE( graph.setPortalOpenness( door, 0.5f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 0.5f * 0.85f, graph.computeOcclusion( inA, inB ), 1e-5f );

    SW_EXPECT_TRUE( graph.setPortalOpenness( door, 0.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, graph.computeOcclusion( inA, inB ), 1e-6f ); // 닫힌 문
    graph.setWallLeak( 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.9f, graph.computeOcclusion( inA, inB ), 1e-5f ); // 벽으로 새는 소리

    SW_EXPECT_FALSE( graph.setPortalOpenness( 99, 1.0f ) );
}

/**
 * @brief [AudioOcclusionTest] 사슬 방은 투과율이 곱해지고, 여러 길이면 가장 잘 통하는 길을 고르며, 바깥도 노드다
 */
SW_TEST_CASE( AudioOcclusionTest, RoomGraphPicksTheBestPath )
{
    sw::AudioRoomGraph graph;
    const uint32       roomA = graph.addRoom( sw::float3( 5.0f, 0.0f, 0.0f ), sw::float3( 5.0f, 5.0f, 5.0f ) );
    const uint32       roomB = graph.addRoom( sw::float3( 15.0f, 0.0f, 0.0f ), sw::float3( 5.0f, 5.0f, 5.0f ) );
    const uint32       roomC = graph.addRoom( sw::float3( 25.0f, 0.0f, 0.0f ), sw::float3( 5.0f, 5.0f, 5.0f ) );
    const uint32       ab    = graph.addPortal( roomA, roomB, 1.0f );
    const uint32       bc    = graph.addPortal( roomB, roomC, 1.0f );
    SW_ASSERT_TRUE( ab != sw::invalid_index::kUint32 && bc != sw::invalid_index::kUint32 );

    const sw::float3 inA( 2.0f, 0.0f, 0.0f );
    const sw::float3 inC( 28.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 0.85f * 0.85f, graph.computeOcclusion( inA, inC ), 1e-5f ); // 문 둘을 지난다

    // 바로 잇는 닫힌 문 AC 가 있어도 열린 AB-BC 길이 이긴다. AC 를 열면 한 번만 지나는 길이 이긴다.
    const uint32 ac = graph.addPortal( roomA, roomC, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 0.85f * 0.85f, graph.computeOcclusion( inA, inC ), 1e-5f );
    SW_EXPECT_TRUE( graph.setPortalOpenness( ac, 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( 0.15f, graph.computeOcclusion( inA, inC ), 1e-5f );

    // 이어지지 않은 바깥 소리는 막히고, 바깥으로 잇는 문이 있으면 들린다.
    const sw::float3 outdoors( 100.0f, 0.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, graph.computeOcclusion( inA, outdoors ), 1e-6f );
    const uint32 front = graph.addPortal( roomC, sw::AudioRoomGraph::kOutside, 1.0f );
    SW_ASSERT_TRUE( front != sw::invalid_index::kUint32 );
    SW_EXPECT_NEAR_EQUAL( 1.0f - 0.85f * 0.85f, graph.computeOcclusion( inA, outdoors ), 1e-5f ); // A -> C(열린 AC) -> 바깥

    // 잘못된 포털은 거절한다.
    SW_EXPECT_TRUE( graph.addPortal( roomA, roomA, 1.0f ) == sw::invalid_index::kUint32 );
    SW_EXPECT_TRUE( graph.addPortal( roomA, 77, 1.0f ) == sw::invalid_index::kUint32 );
}

/**
 * @brief [AudioOcclusionTest] 방 안의 작은 방은 부피가 작은 쪽이 그 점의 방이고, 방이 없으면 가림은 0 이다
 */
SW_TEST_CASE( AudioOcclusionTest, NestedRoomsPreferTheSmallest )
{
    sw::AudioRoomGraph graph;
    SW_EXPECT_NEAR_EQUAL( 0.0f, graph.computeOcclusion( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 9.0f, 0.0f, 0.0f ) ), 1e-6f ); // 방이 없는 씬

    const uint32 hall   = graph.addRoom( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 20.0f, 5.0f, 20.0f ) );
    const uint32 closet = graph.addRoom( sw::float3( 0.0f, 0.0f, 0.0f ), sw::float3( 2.0f, 2.0f, 2.0f ) );
    SW_EXPECT_EQUAL( closet, graph.findRoom( sw::float3( 1.0f, 0.0f, 0.0f ) ) );
    SW_EXPECT_EQUAL( hall, graph.findRoom( sw::float3( 10.0f, 0.0f, 0.0f ) ) );

    SW_EXPECT_NEAR_EQUAL( 1.0f, graph.computeOcclusion( sw::float3( 1.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-6f ); // 이어 주는 문이 없다
    SW_EXPECT_TRUE( graph.addPortal( hall, closet, 1.0f ) != sw::invalid_index::kUint32 );
    SW_EXPECT_NEAR_EQUAL( 0.15f, graph.computeOcclusion( sw::float3( 1.0f, 0.0f, 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ) ), 1e-5f );
    graph.clear();
    SW_EXPECT_EQUAL( uint32( 0 ), graph.getRoomCount() );
}
