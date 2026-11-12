// 2D 블렌드 스페이스 — 들로네 삼각분할의 무게중심 가중: 켜지는 표본은 많아야 셋, 표본 위면 그 표본 하나(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Graph/BlendSpace.h"

using namespace sw;

namespace
{
    struct BlendSpaceTestUtil
    {
        /** @brief 이동 블렌드 스페이스 3×3 격자입니다. 축 단위가 다르다(속도 0~600 cm/s, 방향 -180~180 도) — 정규화가 없으면 속도 축이 지배한다. */
        static BlendSpace makeLocomotionGrid()
        {
            BlendSpace space;
            space.setAxisCount( 2 );
            const float32 arrSpeed[3]     = { 0.0f, 300.0f, 600.0f };
            const float32 arrDirection[3] = { -180.0f, 0.0f, 180.0f };
            for ( const float32 speed : arrSpeed )
            {
                for ( const float32 direction : arrDirection )
                {
                    space.addSample( float2{ speed, direction }, hashed_string( "Clip" ) ); // 번호 = 속도 단계 × 3 + 방향 단계
                }
            }
            return space;
        }

        static uint32 countActive( const vector<float32>& listWeight )
        {
            uint32 active = 0;
            for ( const float32 weight : listWeight )
            {
                if ( weight > 0.0f )
                    ++active;
            }
            return active;
        }

        static float32 sum( const vector<float32>& listWeight )
        {
            float32 total = 0.0f;
            for ( const float32 weight : listWeight )
                total += weight;
            return total;
        }
    };
    using Util = BlendSpaceTestUtil;
} // namespace

/** @brief [BlendSpaceTest] 2D 는 삼각형 하나의 무게중심이라 켜지는 표본이 많아야 셋이고 합은 1 이다 — 범위 안 · 바깥 · 격자 대각선 모두 */
SW_TEST_CASE( BlendSpaceTest, TwoAxisActivatesAtMostThreeSamples )
{
    const BlendSpace space = Util::makeLocomotionGrid();
    SW_REQUIRE_TRUE( space.hasArea() );
    SW_EXPECT_EQUAL( size_t{ 8 }, space.getTriangles().size() ); // 3×3 격자는 칸마다 둘 — 한 원 위 네 점에서도 겹치지 않는다
    vector<float32> listWeight;
    for ( float32 speed = -100.0f; speed <= 700.0f; speed += 37.0f )
    {
        for ( float32 direction = -230.0f; direction <= 230.0f; direction += 23.0f )
        {
            space.computeSampleWeights( float2{ speed, direction }, listWeight );
            SW_REQUIRE_EQUAL( size_t{ 9 }, listWeight.size() );
            SW_EXPECT_TRUE( Util::countActive( listWeight ) <= 3u );
            SW_EXPECT_TRUE( Util::countActive( listWeight ) >= 1u );
            SW_EXPECT_NEAR( 1.0f, Util::sum( listWeight ), 1.0e-5f );
        }
    }
    // 칸 한가운데 — 그 칸의 대각선 위라 두 표본이 반씩, 또는 세 표본이 나눈다. 넷은 켜지지 않는다.
    space.computeSampleWeights( float2{ 150.0f, 90.0f }, listWeight );
    SW_EXPECT_TRUE( Util::countActive( listWeight ) <= 3u );
    // 범위 바깥은 가장 가까운 바깥 변의 두 표본이다(속도 0~300 사이, 방향 위 끝).
    space.computeSampleWeights( float2{ 150.0f, 400.0f }, listWeight );
    SW_EXPECT_NEAR( 0.5f, listWeight[2], 1.0e-5f ); // (0, 180)
    SW_EXPECT_NEAR( 0.5f, listWeight[5], 1.0e-5f ); // (300, 180)
    SW_EXPECT_EQUAL( 2u, Util::countActive( listWeight ) );
}

/** @brief [BlendSpaceTest] 표본 위치에서는 그 표본 하나만 가중치 1 이다 — 이웃이 반올림으로 켜지지 않는다 */
SW_TEST_CASE( BlendSpaceTest, TwoAxisSampleWeightIsOneOnSample )
{
    const BlendSpace space = Util::makeLocomotionGrid();
    vector<float32>  listWeight;
    for ( uint32 sample = 0; sample < space.getSampleCount(); ++sample )
    {
        space.computeSampleWeights( space.getSamples()[sample]._position, listWeight );
        SW_EXPECT_EQUAL( 1.0f, listWeight[sample] );
        SW_EXPECT_EQUAL( 1u, Util::countActive( listWeight ) );
    }
}

/** @brief [BlendSpaceTest] 한 줄로 늘어선 2D 표본은 넓이가 없어 삼각형이 없다(그래프 로드가 이를 오류로 막는다) */
SW_TEST_CASE( BlendSpaceTest, TwoAxisCollinearSamplesHaveNoArea )
{
    BlendSpace space;
    space.setAxisCount( 2 );
    space.addSample( float2{ 0.0f, 0.0f }, hashed_string( "A" ) );
    space.addSample( float2{ 1.0f, 1.0f }, hashed_string( "B" ) );
    space.addSample( float2{ 2.0f, 2.0f }, hashed_string( "C" ) );
    SW_EXPECT_FALSE( space.hasArea() );
    space.addSample( float2{ 2.0f, 0.0f }, hashed_string( "D" ) );
    SW_EXPECT_TRUE( space.hasArea() );
}
