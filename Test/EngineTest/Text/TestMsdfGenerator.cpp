#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Text/Msdf/MsdfGenerator.h"

#include "TestFramework/TestFramework.h"

// MsdfGeneratorTest — MSDF 만들기: 부호 규약 · 모서리에서의 채널 칠하기 · 보간했을 때 모서리가 날카로운지(일반 SDF 보다 오차가 적은지). 순수 CPU(nogpu).

namespace
{
    struct MsdfGeneratorTestUtil
    {
        static constexpr uint32  kSize  = 16;
        static constexpr uint32  kScale = 8;
        static constexpr float32 kRange = 4.0f;

        static sw::MsdfEdge makeLine( float32 x0, float32 y0, float32 x1, float32 y1 )
        {
            sw::MsdfEdge edge{};
            edge._kind        = sw::MsdfEdgeKind::Linear;
            edge._arrPoint[0] = sw::float2{ x0, y0 };
            edge._arrPoint[1] = sw::float2{ x1, y1 };
            return edge;
        }

        /** @brief 안쪽이 진행 방향 왼쪽인(반시계) 다각형을 직선 간선으로 짓습니다. */
        static sw::MsdfShape makePolygon( const sw::vector<sw::float2>& listPoint )
        {
            sw::MsdfShape    shape{};
            sw::MsdfContour& contour = shape._listContour.emplace_back();
            for ( size_t index = 0; index < listPoint.size(); ++index )
            {
                const sw::float2& from = listPoint[index];
                const sw::float2& to   = listPoint[( index + 1 ) % listPoint.size()];
                contour._listEdge.push_back( makeLine( from._x, from._y, to._x, to._y ) );
            }
            return shape;
        }

        static sw::MsdfParams makeParams()
        {
            sw::MsdfParams params{};
            params._width   = kSize;
            params._height  = kSize;
            params._origin  = sw::float2{ 0.0f, static_cast<float32>( kSize ) };
            params._rangePx = kRange;
            return params;
        }

        static bool isInsidePolygon( const sw::vector<sw::float2>& listPoint, float32 x, float32 y )
        {
            // 반시계 볼록 다각형: 모든 간선의 왼쪽에 있으면 안이다.
            for ( size_t index = 0; index < listPoint.size(); ++index )
            {
                const sw::float2& from = listPoint[index];
                const sw::float2& to   = listPoint[( index + 1 ) % listPoint.size()];
                const float32     side = ( to._x - from._x ) * ( y - from._y ) - ( to._y - from._y ) * ( x - from._x );
                if ( side < 0.0f )
                    return false;
            }
            return true;
        }

        static float32 sampleChannel( const sw::vector<uint8>& bytes, float32 column, float32 row, uint32 channel )
        {
            // 텍셀 중심이 (i + 0.5) 인 쌍선형 표본. 끝은 가장자리 값을 늘인다.
            const float32 fx    = sw::MathUtil::clamp( column - 0.5f, 0.0f, static_cast<float32>( kSize - 1 ) );
            const float32 fy    = sw::MathUtil::clamp( row - 0.5f, 0.0f, static_cast<float32>( kSize - 1 ) );
            const uint32  x0    = static_cast<uint32>( fx );
            const uint32  y0    = static_cast<uint32>( fy );
            const uint32  x1    = sw::MathUtil::min( x0 + 1, kSize - 1 );
            const uint32  y1    = sw::MathUtil::min( y0 + 1, kSize - 1 );
            const float32 tx    = fx - static_cast<float32>( x0 );
            const float32 ty    = fy - static_cast<float32>( y0 );
            const auto    texel = [&bytes, channel]( uint32 x, uint32 y )
            { return static_cast<float32>( bytes[( static_cast<size_t>( y ) * kSize + x ) * 4 + channel] ) / 255.0f; };
            const float32 top    = texel( x0, y0 ) + ( texel( x1, y0 ) - texel( x0, y0 ) ) * tx;
            const float32 bottom = texel( x0, y1 ) + ( texel( x1, y1 ) - texel( x0, y1 ) ) * tx;
            return top + ( bottom - top ) * ty;
        }

        struct Mismatch
        {
            uint32 _msdf{ 0 };
            uint32 _sdf{ 0 };
            uint32 _total{ 0 };
        };

        /** @brief 비트맵을 배율 `kScale` 로 보간해 문턱 0.5 로 그린 모양이 해석적 다각형과 어긋나는 표본 수를 셉니다(MSDF = RGB 중앙값, SDF = 알파). */
        static Mismatch countMismatch( const sw::vector<uint8>& bytes, const sw::vector<sw::float2>& listPoint )
        {
            Mismatch result{};
            for ( uint32 sampleY = 0; sampleY < kSize * kScale; ++sampleY )
            {
                for ( uint32 sampleX = 0; sampleX < kSize * kScale; ++sampleX )
                {
                    const float32 column  = ( static_cast<float32>( sampleX ) + 0.5f ) / static_cast<float32>( kScale );
                    const float32 row     = ( static_cast<float32>( sampleY ) + 0.5f ) / static_cast<float32>( kScale );
                    const bool    bInside = isInsidePolygon( listPoint, column, static_cast<float32>( kSize ) - row );
                    const float32 red     = sampleChannel( bytes, column, row, 0 );
                    const float32 green   = sampleChannel( bytes, column, row, 1 );
                    const float32 blue    = sampleChannel( bytes, column, row, 2 );
                    const float32 median  = sw::MathUtil::max( sw::MathUtil::min( red, green ), sw::MathUtil::min( sw::MathUtil::max( red, green ), blue ) );
                    result._msdf += ( ( median > 0.5f ) != bInside ) ? 1u : 0u;
                    result._sdf += ( ( sampleChannel( bytes, column, row, 3 ) > 0.5f ) != bInside ) ? 1u : 0u;
                    ++result._total;
                }
            }
            return result;
        }

        static uint32 countChannels( uint8 channels )
        {
            return ( channels & 1u ) + ( ( channels >> 1u ) & 1u ) + ( ( channels >> 2u ) & 1u );
        }
    };
} // namespace

/** @brief [MsdfGeneratorTest] 부호 규약: 윤곽 안쪽은 128 위(작은 정사각형 가운데 근처 약 175), 범위 밖 바깥은 중앙값 · 알파가 0 이다 — FreeType SDF 와 같다 */
SW_TEST_CASE( MsdfGeneratorTest, InsideIsBrightAndFarOutsideIsDark )
{
    using Util = MsdfGeneratorTestUtil;
    const sw::vector<sw::float2> square{
        sw::float2{ 6.0f,  6.0f},
        sw::float2{10.0f,  6.0f},
        sw::float2{10.0f, 10.0f},
        sw::float2{ 6.0f, 10.0f}
    };
    sw::MsdfShape shape = Util::makePolygon( square );
    sw::MsdfGenerator::colorEdges( shape );
    sw::vector<uint8> bytes;
    sw::MsdfGenerator::computeBitmap( shape, Util::makeParams(), bytes );
    SW_ASSERT_EQUAL( static_cast<size_t>( Util::kSize ) * Util::kSize * 4, bytes.size() );

    // 텍셀 (8, 8) 의 중심은 모양 좌표 (8.5, 7.5) — 가장 가까운 변(오른쪽 x = 10, 아래 y = 6)까지 1.5 → 0.5 + 1.5 / 8 = 0.6875.
    const size_t center = ( static_cast<size_t>( 8 ) * Util::kSize + 8 ) * 4;
    for ( uint32 channel = 0; channel < 4; ++channel )
    {
        SW_EXPECT_NEAR_EQUAL( 175.0f, static_cast<float32>( bytes[center + channel] ), 2.0f );
    }
    // 텍셀 (0, 8) 은 왼쪽 변(x = 6)에서 5.5 > 범위 4 라 중앙값과 알파가 0 이다. 채널 하나하나는 0 이 아닐 수 있다 — 위 · 아래 변의 의사 거리(변을 연장한 직선까지의 거리)가
    // 그 직선의 안쪽을 가리키기 때문이고, 글자 안팎은 세 채널의 중앙값이 정한다.
    const size_t farLeft = static_cast<size_t>( 8 ) * Util::kSize * 4;
    const uint8  minimum = sw::MathUtil::min( bytes[farLeft], sw::MathUtil::min( bytes[farLeft + 1], bytes[farLeft + 2] ) );
    const uint8  maximum = sw::MathUtil::max( bytes[farLeft], sw::MathUtil::max( bytes[farLeft + 1], bytes[farLeft + 2] ) );
    const uint8  median  = static_cast<uint8>( bytes[farLeft] + bytes[farLeft + 1] + bytes[farLeft + 2] - minimum - maximum );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0 ), median );
    SW_EXPECT_EQUAL( static_cast<uint8>( 0 ), bytes[farLeft + 3] );
}

/**
 * @brief [MsdfGeneratorTest] 보간한 중앙값이 모서리를 날카롭게 낸다 — 정사각형은 어긋난 표본이 0.5% 미만이고 일반 SDF(알파)보다 많지 않으며, 뾰족한 삼각형도 SDF 보다 많지 않다
 * @details 변이: 모서리에서 갈리지 않게(모두 흰색으로) 칠하면 MSDF 가 일반 SDF 와 같아져 정사각형 기대가 진다. 의사 거리 확장을 빼면 뾰족한 모서리 바깥이 둥글어진다.
 */
SW_TEST_CASE( MsdfGeneratorTest, InterpolatedMedianKeepsCornersSharp )
{
    using Util = MsdfGeneratorTestUtil;
    const sw::vector<sw::float2> square{
        sw::float2{ 4.0f,  4.0f},
        sw::float2{12.0f,  4.0f},
        sw::float2{12.0f, 12.0f},
        sw::float2{ 4.0f, 12.0f}
    };
    const sw::vector<sw::float2> triangle{
        sw::float2{ 3.0f, 3.0f},
        sw::float2{13.0f, 3.0f},
        sw::float2{ 3.0f, 7.0f}
    };
    for ( const sw::vector<sw::float2>* pPolygon : { &square, &triangle } )
    {
        sw::MsdfShape shape = Util::makePolygon( *pPolygon );
        sw::MsdfGenerator::colorEdges( shape );
        sw::vector<uint8> bytes;
        sw::MsdfGenerator::computeBitmap( shape, Util::makeParams(), bytes );
        const Util::Mismatch mismatch = Util::countMismatch( bytes, *pPolygon );
        SW_EXPECT_TRUE( mismatch._msdf <= mismatch._sdf );
        if ( pPolygon == &square )
            SW_EXPECT_TRUE( mismatch._msdf * 200 < mismatch._total ); // 0.5% 미만
    }
}

/** @brief [MsdfGeneratorTest] 칠하기: 모서리 넷의 정사각형은 이웃 간선의 채널이 다르고 간선마다 둘 이상이며 윤곽 전체가 세 채널을 모두 덮는다 — 둥근 윤곽(모서리 없음)은 모두 흰색이다 */
SW_TEST_CASE( MsdfGeneratorTest, ColoringSeparatesCornersAndLeavesSmoothContoursWhite )
{
    using Util           = MsdfGeneratorTestUtil;
    sw::MsdfShape square = Util::makePolygon( sw::vector<sw::float2>{
        sw::float2{ 4.0f,  4.0f},
        sw::float2{12.0f,  4.0f},
        sw::float2{12.0f, 12.0f},
        sw::float2{ 4.0f, 12.0f}
    } );
    sw::MsdfGenerator::colorEdges( square );
    const sw::vector<sw::MsdfEdge>& listEdge = square._listContour[0]._listEdge;
    SW_ASSERT_EQUAL( size_t{ 4 }, listEdge.size() );
    uint8 covered = 0;
    for ( size_t index = 0; index < listEdge.size(); ++index )
    {
        const uint8 channels = listEdge[index]._channels;
        const uint8 next     = listEdge[( index + 1 ) % listEdge.size()]._channels;
        SW_EXPECT_TRUE( Util::countChannels( channels ) >= 2 );
        SW_EXPECT_NOT_EQUAL( channels, next );
        covered = static_cast<uint8>( covered | channels );
    }
    SW_EXPECT_EQUAL( sw::MsdfChannel::kWhite, covered );

    // 둥근 윤곽: 3차 베지어 넷(원 근사). 이웃 접선이 이어져 모서리가 없다.
    constexpr float32 kRadius = 5.0f;
    constexpr float32 kKappa  = 0.5523f * kRadius;
    const sw::float2  center{ 8.0f, 8.0f };
    const sw::float2  arrAnchor[4] = {
        sw::float2{center._x + kRadius,           center._y},
        sw::float2{          center._x, center._y + kRadius},
        sw::float2{center._x - kRadius,           center._y},
        sw::float2{          center._x, center._y - kRadius}
    };
    const sw::float2 arrTangent[4] = {
        sw::float2{ 0.0f,  1.0f},
        sw::float2{-1.0f,  0.0f},
        sw::float2{ 0.0f, -1.0f},
        sw::float2{ 1.0f,  0.0f}
    };
    sw::MsdfShape    circle{};
    sw::MsdfContour& contour = circle._listContour.emplace_back();
    for ( uint32 index = 0; index < 4; ++index )
    {
        const uint32 next = ( index + 1 ) % 4;
        sw::MsdfEdge edge{};
        edge._kind        = sw::MsdfEdgeKind::Cubic;
        edge._arrPoint[0] = arrAnchor[index];
        edge._arrPoint[1] = sw::float2{ arrAnchor[index]._x + arrTangent[index]._x * kKappa, arrAnchor[index]._y + arrTangent[index]._y * kKappa };
        edge._arrPoint[2] = sw::float2{ arrAnchor[next]._x - arrTangent[next]._x * kKappa, arrAnchor[next]._y - arrTangent[next]._y * kKappa };
        edge._arrPoint[3] = arrAnchor[next];
        contour._listEdge.push_back( edge );
    }
    sw::MsdfGenerator::colorEdges( circle );
    for ( const sw::MsdfEdge& edge : circle._listContour[0]._listEdge )
    {
        SW_EXPECT_EQUAL( sw::MsdfChannel::kWhite, edge._channels );
    }
}

/** @brief [MsdfGeneratorTest] 모서리가 하나뿐인 한 간선 윤곽(고리)은 삼등분해 세 채널 조합으로 칠하고, 조각은 끊김 없이 이어진다 */
SW_TEST_CASE( MsdfGeneratorTest, SingleCornerLoopIsSplitInThirds )
{
    sw::MsdfShape    shape{};
    sw::MsdfContour& contour = shape._listContour.emplace_back();
    sw::MsdfEdge     loop{};
    loop._kind        = sw::MsdfEdgeKind::Cubic;
    loop._arrPoint[0] = sw::float2{ 5.0f, 0.0f };
    loop._arrPoint[1] = sw::float2{ 15.0f, 10.0f };
    loop._arrPoint[2] = sw::float2{ -5.0f, 10.0f };
    loop._arrPoint[3] = sw::float2{ 5.0f, 0.0f };
    contour._listEdge.push_back( loop );
    sw::MsdfGenerator::colorEdges( shape );

    const sw::vector<sw::MsdfEdge>& listEdge = shape._listContour[0]._listEdge;
    SW_ASSERT_EQUAL( size_t{ 3 }, listEdge.size() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listEdge[0]._arrPoint[0]._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listEdge[2]._arrPoint[3]._x, 1e-4f );
    for ( size_t index = 0; index + 1 < listEdge.size(); ++index )
    {
        SW_EXPECT_NEAR_EQUAL( listEdge[index]._arrPoint[3]._x, listEdge[index + 1]._arrPoint[0]._x, 1e-3f );
        SW_EXPECT_NEAR_EQUAL( listEdge[index]._arrPoint[3]._y, listEdge[index + 1]._arrPoint[0]._y, 1e-3f );
    }
    uint8 covered = 0;
    for ( const sw::MsdfEdge& edge : listEdge )
    {
        covered = static_cast<uint8>( covered | edge._channels );
        SW_EXPECT_TRUE( MsdfGeneratorTestUtil::countChannels( edge._channels ) >= 2 );
    }
    SW_EXPECT_EQUAL( sw::MsdfChannel::kWhite, covered );
}
