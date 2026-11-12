// 2D 블렌드 스페이스 — 들로네 삼각분할의 무게중심 가중: 켜지는 표본은 많아야 셋, 표본 위면 그 표본 하나, 삼각분할이 볼록 껍질을 빠짐없이 덮는다(nogpu).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Graph/BlendSpace.h"

using namespace sw;

namespace
{
    struct BlendSpaceTestUtil
    {
        /** @brief 무작위 세트의 격자 칸 수입니다. 축 범위를 같은 값으로 두면 표본이 정수 좌표라 넓이 · 외접원 판정을 테스트에서 정확히 셀 수 있다. */
        static constexpr int64 kRandomGrid = 64;

        /** @brief 이동 블렌드 스페이스 3×3 격자입니다. 축 단위가 다르다(속도 0~600 cm/s, 방향 -180~180 도) — 정규화가 없으면 속도 축이 지배한다. */
        static BlendSpace makeLocomotionGrid()
        {
            BlendSpace space;
            space.setAxisCount( 2 );
            space.setAxisRange( float2{ 0.0f, -180.0f }, float2{ 600.0f, 180.0f } );
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

        /** @brief 고정 시드 xorshift 입니다(실행마다 같은 세트). */
        static uint32 nextRandom( uint32& inoutState )
        {
            inoutState ^= inoutState << 13;
            inoutState ^= inoutState >> 17;
            inoutState ^= inoutState << 5;
            return inoutState;
        }

        static int64 toInteger( float32 value ) { return static_cast<int64>( value ); }

        static int64 orient( const float2& a, const float2& b, const float2& c )
        {
            return ( toInteger( b._x ) - toInteger( a._x ) ) * ( toInteger( c._y ) - toInteger( a._y ) ) - ( toInteger( b._y ) - toInteger( a._y ) ) * ( toInteger( c._x ) - toInteger( a._x ) );
        }

        /** @brief 반시계 삼각형 a · b · c 의 외접원 안에 d 가 확실히 있으면 참입니다(정수). */
        static bool isInsideCircle( const float2& a, const float2& b, const float2& c, const float2& d )
        {
            const int64 adx = toInteger( a._x ) - toInteger( d._x );
            const int64 ady = toInteger( a._y ) - toInteger( d._y );
            const int64 bdx = toInteger( b._x ) - toInteger( d._x );
            const int64 bdy = toInteger( b._y ) - toInteger( d._y );
            const int64 cdx = toInteger( c._x ) - toInteger( d._x );
            const int64 cdy = toInteger( c._y ) - toInteger( d._y );
            const int64 det = ( adx * adx + ady * ady ) * ( bdx * cdy - cdx * bdy ) + ( bdx * bdx + bdy * bdy ) * ( cdx * ady - adx * cdy ) + ( cdx * cdx + cdy * cdy ) * ( adx * bdy - bdx * ady );
            return det > 0;
        }

        /** @brief 정수 점들의 볼록 껍질 넓이 × 2 입니다(단조 사슬). */
        static int64 computeHullDoubleArea( vector<float2> listPoint )
        {
            VectorUtil::sort( listPoint, []( const float2& lhs, const float2& rhs )
            {
                return lhs._x < rhs._x || ( lhs._x == rhs._x && lhs._y < rhs._y );
            } );
            vector<float2> listHull;
            for ( uint32 pass = 0; pass < 2; ++pass )
            {
                const size_t start = listHull.size();
                for ( const float2& point : listPoint )
                {
                    while ( listHull.size() >= start + 2 && orient( listHull[listHull.size() - 2], listHull.back(), point ) <= 0 )
                        listHull.pop_back();
                    listHull.push_back( point );
                }
                listHull.pop_back();
                VectorUtil::reverse( listPoint );
            }
            int64 doubleArea = 0;
            for ( size_t index = 0; index < listHull.size(); ++index )
            {
                const float2& from = listHull[index];
                const float2& to   = listHull[( index + 1 ) % listHull.size()];
                doubleArea += toInteger( from._x ) * toInteger( to._y ) - toInteger( to._x ) * toInteger( from._y );
            }
            return doubleArea;
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
    // 범위 바깥은 축 범위로 가둔다 — 위 끝 변(속도 0~300 사이)의 두 표본이 반씩이다.
    space.computeSampleWeights( float2{ 150.0f, 400.0f }, listWeight );
    SW_EXPECT_NEAR( 0.5f, listWeight[2], 1.0e-5f ); // (0, 180)
    SW_EXPECT_NEAR( 0.5f, listWeight[5], 1.0e-5f ); // (300, 180)
    SW_EXPECT_EQUAL( 2u, Util::countActive( listWeight ) );
}

/** @brief [BlendSpaceTest] 볼록 껍질이 축 범위보다 좁으면, 가둔 파라미터를 가장 가까운 바깥 변으로 옮겨 두 표본만 섞는다 */
SW_TEST_CASE( BlendSpaceTest, TwoAxisOutsideHullUsesNearestEdge )
{
    BlendSpace space;
    space.setAxisCount( 2 );
    space.setAxisRange( float2{ 0.0f, 0.0f }, float2{ 1.0f, 1.0f } );
    space.addSample( float2{ 0.0f, 0.0f }, hashed_string( "A" ) );
    space.addSample( float2{ 1.0f, 0.0f }, hashed_string( "B" ) );
    space.addSample( float2{ 0.0f, 1.0f }, hashed_string( "C" ) );
    vector<float32> listWeight;
    space.computeSampleWeights( float2{ 1.0f, 1.0f }, listWeight );
    SW_EXPECT_NEAR( 0.0f, listWeight[0], 0.0f );
    SW_EXPECT_NEAR( 0.5f, listWeight[1], 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, listWeight[2], 1.0e-5f );
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

/**
 * @brief [BlendSpaceTest] 무작위 세트(고정 시드) — 삼각분할이 볼록 껍질을 빠짐없이 덮고(넓이가 같다), 모든 삼각형이 반시계이며, 외접원 안에 다른 표본이 없다(들로네)
 * @details 큰 바깥 삼각형을 쓰는 보이어-왓슨은 껍질 근처의 얇은 삼각형을 빠뜨릴 수 있다. 격자 세트(한 원 위 네 점)와 한 줄 테두리를 섞어 그 경우를 일부러 만든다.
 */
SW_TEST_CASE( BlendSpaceTest, TwoAxisRandomSetsCoverHullAndAreDelaunay )
{
    uint32 state = 0x2545F491u;
    for ( uint32 setIndex = 0; setIndex < 200; ++setIndex )
    {
        BlendSpace space;
        space.setAxisCount( 2 );
        space.setAxisRange( float2{ 0.0f, 0.0f }, float2{ static_cast<float32>( Util::kRandomGrid ), static_cast<float32>( Util::kRandomGrid ) } );
        const bool     bCoarse     = ( setIndex % 3u ) == 0; // 셋에 하나는 8 칸 격자 — 원 위 네 점 · 한 줄 테두리가 잦다
        const uint32   step        = bCoarse ? 8u : 1u;
        const uint32   sampleCount = 3u + Util::nextRandom( state ) % 22u;
        vector<float2> listPosition;
        for ( uint32 attempt = 0; attempt < sampleCount * 4u && listPosition.size() < sampleCount; ++attempt )
        {
            const uint32 cells = static_cast<uint32>( Util::kRandomGrid ) / step + 1u;
            const float2 position{ static_cast<float32>( ( Util::nextRandom( state ) % cells ) * step ), static_cast<float32>( ( Util::nextRandom( state ) % cells ) * step ) };
            if ( VectorUtil::contains( listPosition, position ) )
                continue;
            listPosition.push_back( position );
            space.addSample( position, hashed_string( "Clip" ) );
        }
        SW_REQUIRE_TRUE( space.hasCoincidentSamples() == false );
        int64 triangleDoubleArea = 0;
        for ( const BlendSpaceTriangle& triangle : space.getTriangles() )
        {
            const float2& a           = listPosition[triangle._sampleA];
            const float2& b           = listPosition[triangle._sampleB];
            const float2& c           = listPosition[triangle._sampleC];
            const int64   orientation = Util::orient( a, b, c );
            SW_EXPECT_TRUE( orientation > 0 );
            triangleDoubleArea += orientation;
            for ( size_t other = 0; other < listPosition.size(); ++other )
            {
                if ( other == triangle._sampleA || other == triangle._sampleB || other == triangle._sampleC )
                    continue;
                SW_EXPECT_FALSE( Util::isInsideCircle( a, b, c, listPosition[other] ) );
            }
        }
        SW_EXPECT_EQUAL( Util::computeHullDoubleArea( listPosition ), triangleDoubleArea );
        if ( space.hasArea() == false )
            continue;
        vector<float32> listWeight;
        for ( uint32 probe = 0; probe < 16; ++probe )
        {
            const float2 parameter{ static_cast<float32>( Util::nextRandom( state ) % 6400u ) * 0.01f, static_cast<float32>( Util::nextRandom( state ) % 6400u ) * 0.01f };
            space.computeSampleWeights( parameter, listWeight );
            SW_EXPECT_TRUE( Util::countActive( listWeight ) <= 3u );
            SW_EXPECT_NEAR( 1.0f, Util::sum( listWeight ), 1.0e-5f );
        }
    }
}

/** @brief [BlendSpaceTest] 한 줄로 늘어선 2D 표본은 넓이가 없어 삼각형이 없다(그래프 로드가 이를 오류로 막는다) */
SW_TEST_CASE( BlendSpaceTest, TwoAxisCollinearSamplesHaveNoArea )
{
    BlendSpace space;
    space.setAxisCount( 2 );
    space.setAxisRange( float2{ 0.0f, 0.0f }, float2{ 2.0f, 2.0f } );
    space.addSample( float2{ 0.0f, 0.0f }, hashed_string( "A" ) );
    space.addSample( float2{ 1.0f, 1.0f }, hashed_string( "B" ) );
    space.addSample( float2{ 2.0f, 2.0f }, hashed_string( "C" ) );
    SW_EXPECT_FALSE( space.hasArea() );
    space.addSample( float2{ 2.0f, 0.0f }, hashed_string( "D" ) );
    SW_EXPECT_TRUE( space.hasArea() );
}

/** @brief [BlendSpaceTest] 정규화 격자에서 같은 자리인 두 표본은 겹친 것으로 본다 — 1D 는 x 만 본다 */
SW_TEST_CASE( BlendSpaceTest, CoincidentSamplesAreDetected )
{
    BlendSpace space;
    space.setAxisCount( 2 );
    space.setAxisRange( float2{ 0.0f, 0.0f }, float2{ 1.0f, 1.0f } );
    space.addSample( float2{ 0.0f, 0.0f }, hashed_string( "A" ) );
    space.addSample( float2{ 1.0f, 0.0f }, hashed_string( "B" ) );
    space.addSample( float2{ 0.0f, 1.0f }, hashed_string( "C" ) );
    SW_EXPECT_FALSE( space.hasCoincidentSamples() );
    space.addSample( float2{ 1.0f, 0.0f }, hashed_string( "D" ) );
    SW_EXPECT_TRUE( space.hasCoincidentSamples() );

    BlendSpace line;
    line.setAxisRange( float2{ 0.0f, 0.0f }, float2{ 2.0f, 0.0f } );
    line.addSample( float2{ 1.0f, 0.0f }, hashed_string( "A" ) );
    line.addSample( float2{ 1.0f, 5.0f }, hashed_string( "B" ) ); // 1D 는 y 를 보지 않는다
    SW_EXPECT_TRUE( line.hasValidAxisRange() );
    SW_EXPECT_TRUE( line.hasCoincidentSamples() );
}
