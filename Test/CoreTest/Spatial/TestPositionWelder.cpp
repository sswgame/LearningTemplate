#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Spatial/PositionWelder.h"

#include "TestFramework/TestFramework.h"

// PositionWelder — 허용 오차 안의 위치 합치기, 셀 경계, 대칭 꼭짓점, 같은 입력의 같은 출력(결정성).

namespace
{
    bool isSamePoint( const sw::float3& lhs, const sw::float3& rhs )
    {
        return lhs._x == rhs._x && lhs._y == rhs._y && lhs._z == rhs._z;
    }

    /** @brief 반 크기 0.5 정육면체의 꼭짓점 여덟을 삼각형 목록처럼 여러 번 되풀이하고, 1e-7 m 어긋난 사본을 섞은 입력입니다. */
    sw::vector<sw::float3> makeJitteredCorners()
    {
        sw::vector<sw::float3> listPosition;
        for ( uint32 repeat = 0; repeat < 3; ++repeat )
        {
            for ( uint32 corner = 0; corner < 8; ++corner )
            {
                const float32 x      = ( corner & 1u ) != 0 ? 0.5f : -0.5f;
                const float32 y      = ( corner & 2u ) != 0 ? 0.5f : -0.5f;
                const float32 z      = ( corner & 4u ) != 0 ? 0.5f : -0.5f;
                const float32 jitter = static_cast<float32>( repeat ) * 1.0e-7f;
                listPosition.push_back( sw::float3{ x + jitter, y - jitter, z + jitter } );
            }
        }
        return listPosition;
    }
} // namespace

/**
 * @brief [PositionWelderTest] 허용 오차 안의 위치는 처음 본 점 번호로 합쳐지고, 오차 밖은 새 번호다
 */
SW_TEST_CASE( PositionWelderTest, WeldsWithinToleranceToFirstSeenPoint )
{
    sw::PositionWelder welder;
    const uint32       first = welder.weld( sw::float3{ 1.0f, 2.0f, 3.0f } );
    SW_EXPECT_EQUAL( 0u, first );
    // 같은 위치, 오차 안의 위치는 첫 점으로 합쳐지고 대표 위치는 처음 넣은 값이다.
    SW_EXPECT_EQUAL( first, welder.weld( sw::float3{ 1.0f, 2.0f, 3.0f } ) );
    SW_EXPECT_EQUAL( first, welder.weld( sw::float3{ 1.0f + 5.0e-6f, 2.0f, 3.0f } ) );
    SW_EXPECT_TRUE( isSamePoint( welder.getPoint( first ), sw::float3{ 1.0f, 2.0f, 3.0f } ) );
    // 오차 밖은 새 점이다.
    const uint32 farPoint = welder.weld( sw::float3{ 1.0f + 1.0e-4f, 2.0f, 3.0f } );
    SW_EXPECT_EQUAL( 1u, farPoint );
    SW_EXPECT_EQUAL( 2u, welder.getPointCount() );

    welder.clear();
    SW_EXPECT_EQUAL( 0u, welder.getPointCount() );
    SW_EXPECT_EQUAL( 0u, welder.weld( sw::float3{ 9.0f, 9.0f, 9.0f } ) );
}

/**
 * @brief [PositionWelderTest] 셀 경계(0 을 사이에 둔 두 점)에 걸친 가까운 점도 합쳐진다 — 이웃 셀까지 본다
 */
SW_TEST_CASE( PositionWelderTest, WeldsAcrossCellBoundary )
{
    sw::PositionWelder welder;
    const uint32       negative = welder.weld( sw::float3{ -2.0e-6f, -2.0e-6f, -2.0e-6f } );
    const uint32       positive = welder.weld( sw::float3{ 2.0e-6f, 2.0e-6f, 2.0e-6f } );
    SW_EXPECT_EQUAL( negative, positive );
    SW_EXPECT_EQUAL( 1u, welder.getPointCount() );
}

/**
 * @brief [PositionWelderTest] 부호만 다른 대칭 꼭짓점은 서로 다른 점이다(키를 성분별 XOR 로 섞으면 하나로 합쳐지던 모양)
 */
SW_TEST_CASE( PositionWelderTest, KeepsSymmetricCornersApart )
{
    const sw::vector<sw::float3> listPosition = makeJitteredCorners();
    sw::PositionWelder           welder;
    sw::vector<uint32>           listRemap;
    welder.weldAll( sw::span<const sw::float3>{ listPosition.data(), listPosition.size() }, listRemap );
    SW_ASSERT_EQUAL( listPosition.size(), listRemap.size() );
    SW_EXPECT_EQUAL( 8u, welder.getPointCount() );
    for ( size_t index = 0; index < listPosition.size(); ++index )
    {
        // 되풀이마다 같은 꼭짓점은 첫 되풀이의 번호를 받는다(번호는 처음 본 순서).
        SW_EXPECT_EQUAL( static_cast<uint32>( index % 8 ), listRemap[index] );
    }
}

/**
 * @brief [PositionWelderTest] 같은 입력이면 점 · 번호가 언제나 같다 — 쿠킹 해시와 파괴 해시가 기대는 성질
 */
SW_TEST_CASE( PositionWelderTest, SameInputGivesSameOutput )
{
    sw::vector<sw::float3> listPosition = makeJitteredCorners();
    // 격자 셀에 여럿이 몰리는 경우까지 섞는다(오차의 절반 간격 줄).
    for ( uint32 step = 0; step < 64; ++step )
    {
        listPosition.push_back( sw::float3{ static_cast<float32>( step ) * 0.5e-5f, 0.25f, -0.25f } );
    }
    const sw::span<const sw::float3> input{ listPosition.data(), listPosition.size() };

    sw::PositionWelder welderA;
    sw::vector<uint32> listRemapA;
    welderA.weldAll( input, listRemapA );
    sw::PositionWelder welderB;
    sw::vector<uint32> listRemapB;
    welderB.weldAll( input, listRemapB );

    SW_ASSERT_EQUAL( listRemapA.size(), listRemapB.size() );
    for ( size_t index = 0; index < listRemapA.size(); ++index )
    {
        SW_EXPECT_EQUAL( listRemapA[index], listRemapB[index] );
    }
    SW_ASSERT_EQUAL( welderA.getPointCount(), welderB.getPointCount() );
    for ( uint32 index = 0; index < welderA.getPointCount(); ++index )
    {
        SW_EXPECT_TRUE( isSamePoint( welderA.getPoint( index ), welderB.getPoint( index ) ) );
    }
    // 대표 점은 그 번호를 처음 받은 입력 위치 그대로다.
    sw::vector<uint8> listSeen( welderA.getPointCount(), 0 );
    for ( size_t index = 0; index < listRemapA.size(); ++index )
    {
        const uint32 pointIndex = listRemapA[index];
        if ( listSeen[pointIndex] != 0 )
            continue;
        listSeen[pointIndex] = 1;
        SW_EXPECT_TRUE( isSamePoint( welderA.getPoint( pointIndex ), listPosition[index] ) );
    }
}
