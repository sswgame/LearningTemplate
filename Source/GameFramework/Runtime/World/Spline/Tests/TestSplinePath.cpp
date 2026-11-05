#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/World/Spline/SplinePath.h"

using namespace sw;

namespace
{
    /** @brief 곡선을 만드는 도우미입니다. 정사각형은 한 변이 10 입니다. */
    struct SplineTestUtil
    {
        static vector<float3> makeSquare() { return {
            float3{ 0.0f, 0.0f,  0.0f},
            float3{10.0f, 0.0f,  0.0f},
            float3{10.0f, 0.0f, 10.0f},
            float3{ 0.0f, 0.0f, 10.0f}
 }; }

        static void expectPoint( const float3& expected, const float3& actual )
        {
            SW_EXPECT_NEAR( expected._x, actual._x, 1.0e-3f );
            SW_EXPECT_NEAR( expected._y, actual._y, 1.0e-3f );
            SW_EXPECT_NEAR( expected._z, actual._z, 1.0e-3f );
        }
    };
    using Util = SplineTestUtil;
} // namespace

SW_TEST_CASE( SplinePathTest, RefusesTooFewPointsAndBadSampleCount )
{
    SplinePath path;
    SW_EXPECT_FALSE( path.initialize( { float3{} }, SplineType::Linear, false, 4 ) );
    SW_EXPECT_FALSE( path.isValid() );
    SW_EXPECT_FALSE( path.initialize( Util::makeSquare(), SplineType::Linear, false, 0 ) );

    // 열린 베지어는 3n + 1 점, 닫힌 베지어는 3n 점이다
    SW_EXPECT_FALSE( path.initialize( { float3{}, float3{}, float3{} }, SplineType::Bezier, false, 4 ) );
    SW_EXPECT_FALSE( path.initialize( { float3{}, float3{}, float3{}, float3{}, float3{} }, SplineType::Bezier, false, 4 ) );
    SW_EXPECT_FALSE( path.initialize( Util::makeSquare(), SplineType::Bezier, true, 4 ) );
    SW_EXPECT_TRUE( path.initialize( Util::makeSquare(), SplineType::Bezier, false, 4 ) );

    // 빈 곡선의 조회는 기본값이다
    path.clear();
    SW_EXPECT_NEAR( 0.0f, path.getLength(), 0.0f );
    Util::expectPoint( float3{}, path.sampleAtDistance( 5.0f )._position );
    Util::expectPoint( float3{}, path.findClosest( float3{ 1.0f, 1.0f, 1.0f } )._position );
    Util::expectPoint( float3{}, path.evaluate( 1.0f ) );
}

SW_TEST_CASE( SplinePathTest, LinearOpenClampsAndMeasuresTheLength )
{
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( Util::makeSquare(), SplineType::Linear, false, 4 ) );
    SW_EXPECT_EQUAL( 3, path.getSegmentCount() );
    SW_EXPECT_NEAR( 30.0f, path.getLength(), 1.0e-3f );

    const SplineSample corner = path.sampleAtDistance( 15.0f );
    Util::expectPoint( float3{ 10.0f, 0.0f, 5.0f }, corner._position );
    Util::expectPoint( float3{ 0.0f, 0.0f, 1.0f }, corner._tangent );
    SW_EXPECT_NEAR( 15.0f, corner._distance, 1.0e-3f );

    Util::expectPoint( float3{ 0.0f, 0.0f, 0.0f }, path.sampleAtDistance( -5.0f )._position );
    Util::expectPoint( float3{ 0.0f, 0.0f, 10.0f }, path.sampleAtDistance( 99.0f )._position );
    Util::expectPoint( float3{ 0.0f, 0.0f, 10.0f }, path.sampleAtFraction( 1.0f )._position );
}

SW_TEST_CASE( SplinePathTest, ClosedWrapsAroundIncludingTheClosingSpan )
{
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( Util::makeSquare(), SplineType::Linear, true, 2 ) );
    SW_EXPECT_EQUAL( 4, path.getSegmentCount() );
    SW_EXPECT_TRUE( path.isClosed() );
    SW_EXPECT_NEAR( 40.0f, path.getLength(), 1.0e-3f );

    Util::expectPoint( float3{ 0.0f, 0.0f, 5.0f }, path.sampleAtDistance( 35.0f )._position ); // 닫는 변
    Util::expectPoint( float3{ 0.0f, 0.0f, -1.0f }, path.sampleAtDistance( 35.0f )._tangent );
    Util::expectPoint( float3{ 0.0f, 0.0f, 9.0f }, path.sampleAtDistance( 31.0f )._position );
    Util::expectPoint( float3{ 5.0f, 0.0f, 0.0f }, path.sampleAtDistance( 45.0f )._position ); // 한 바퀴를 넘어 감긴다
    Util::expectPoint( float3{ 0.0f, 0.0f, 5.0f }, path.sampleAtDistance( -5.0f )._position ); // 뒤로도 감긴다
    SW_EXPECT_NEAR( 0.0f, path.wrapDistance( 40.0f ), 1.0e-3f );
}

SW_TEST_CASE( SplinePathTest, CatmullRomPassesThroughItsPoints )
{
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( Util::makeSquare(), SplineType::CatmullRom, false, 16 ) );
    Util::expectPoint( float3{ 0.0f, 0.0f, 0.0f }, path.evaluate( 0.0f ) );
    Util::expectPoint( float3{ 10.0f, 0.0f, 0.0f }, path.evaluate( 1.0f ) );
    Util::expectPoint( float3{ 10.0f, 0.0f, 10.0f }, path.evaluate( 2.0f ) );
    Util::expectPoint( float3{ 0.0f, 0.0f, 10.0f }, path.evaluate( 3.0f ) );
    Util::expectPoint( float3{ 0.0f, 0.0f, 10.0f }, path.evaluate( 99.0f ) ); // 매개변수는 잘린다

    // 곡선이 현보다 길고, 표 샘플을 늘려도 거의 같다
    SW_EXPECT_TRUE( path.getLength() >= 30.0f );
    SplinePath fine;
    SW_REQUIRE_TRUE( fine.initialize( Util::makeSquare(), SplineType::CatmullRom, false, 64 ) );
    SW_EXPECT_NEAR( fine.getLength(), path.getLength(), 0.1f );

    SplinePath closed;
    SW_REQUIRE_TRUE( closed.initialize( Util::makeSquare(), SplineType::CatmullRom, true, 16 ) );
    Util::expectPoint( closed.evaluate( 0.0f ), closed.evaluate( 4.0f ) ); // 닫힌 곡선은 처음으로 돌아온다
}

SW_TEST_CASE( SplinePathTest, BezierStartsAndEndsOnItsEndPoints )
{
    const vector<float3> listPoint{
        float3{ 0.0f, 0.0f, 0.0f},
        float3{ 0.0f, 0.0f, 5.0f},
        float3{10.0f, 0.0f, 5.0f},
        float3{10.0f, 0.0f, 0.0f}
    };
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( listPoint, SplineType::Bezier, false, 32 ) );
    SW_EXPECT_EQUAL( 1, path.getSegmentCount() );
    Util::expectPoint( listPoint[0], path.evaluate( 0.0f ) );
    Util::expectPoint( listPoint[3], path.evaluate( 1.0f ) );
    Util::expectPoint( float3{ 5.0f, 0.0f, 3.75f }, path.evaluate( 0.5f ) );
    Util::expectPoint( float3{ 0.0f, 0.0f, 15.0f }, path.evaluateDerivative( 0.0f ) ); // 3 * (p1 - p0)
    Util::expectPoint( float3{ 1.0f, 0.0f, 0.0f }, path.sampleAtFraction( 0.5f )._tangent );

    // 닫힌 베지어는 첫 끝점으로 돌아온다(3n 점)
    const vector<float3> listClosed{
        float3{ 0.0f, 0.0f, 0.0f},
        float3{ 0.0f, 0.0f, 5.0f},
        float3{10.0f, 0.0f, 5.0f}
    };
    SplinePath closed;
    SW_REQUIRE_TRUE( closed.initialize( listClosed, SplineType::Bezier, true, 16 ) );
    Util::expectPoint( listClosed[0], closed.evaluate( 1.0f ) );
}

SW_TEST_CASE( SplinePathTest, FindClosestProjectsOntoTheCurve )
{
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( Util::makeSquare(), SplineType::Linear, false, 4 ) );
    const SplineSample side = path.findClosest( float3{ 4.0f, 3.0f, -2.0f } );
    Util::expectPoint( float3{ 4.0f, 0.0f, 0.0f }, side._position );
    SW_EXPECT_NEAR( 4.0f, side._distance, 1.0e-3f );
    Util::expectPoint( float3{ 1.0f, 0.0f, 0.0f }, side._tangent );

    const SplineSample end = path.findClosest( float3{ -5.0f, 0.0f, 20.0f } ); // 끝을 넘은 점은 끝점이다
    Util::expectPoint( float3{ 0.0f, 0.0f, 10.0f }, end._position );
    SW_EXPECT_NEAR( 30.0f, end._distance, 1.0e-3f );

    SplinePath closed;
    SW_REQUIRE_TRUE( closed.initialize( Util::makeSquare(), SplineType::Linear, true, 2 ) );
    const SplineSample closing = closed.findClosest( float3{ -1.0f, 0.0f, 5.0f } ); // 닫는 변에 있다
    Util::expectPoint( float3{ 0.0f, 0.0f, 5.0f }, closing._position );
    SW_EXPECT_NEAR( 35.0f, closing._distance, 1.0e-3f );
}

SW_TEST_CASE( SplinePathTest, SampleUniformKeepsTheEndOfAnOpenCurveOnly )
{
    SplinePath path;
    SW_REQUIRE_TRUE( path.initialize( Util::makeSquare(), SplineType::Linear, false, 4 ) );
    vector<SplineSample> listSample;
    path.sampleUniform( 7.0f, listSample );
    SW_REQUIRE_EQUAL( size_t( 6 ), listSample.size() ); // 0 7 14 21 28 + 끝 30
    SW_EXPECT_NEAR( 28.0f, listSample[4]._distance, 1.0e-3f );
    SW_EXPECT_NEAR( 30.0f, listSample[5]._distance, 1.0e-3f );

    path.sampleUniform( 10.0f, listSample );
    SW_EXPECT_EQUAL( size_t( 4 ), listSample.size() ); // 끝이 간격에 맞으면 겹쳐 넣지 않는다

    SplinePath closed;
    SW_REQUIRE_TRUE( closed.initialize( Util::makeSquare(), SplineType::Linear, true, 2 ) );
    closed.sampleUniform( 10.0f, listSample );
    SW_EXPECT_EQUAL( size_t( 4 ), listSample.size() ); // 닫힌 곡선은 처음을 다시 넣지 않는다

    closed.sampleUniform( 0.0f, listSample );
    SW_EXPECT_TRUE( listSample.empty() );
}

// 모든 점이 같은 곡선은 길이가 0 이다. 나누기 0 이나 NaN 접선이 나오면 안 된다.
SW_TEST_CASE( SplinePathTest, DegenerateCurveStaysFinite )
{
    const float3 point{ 1.0f, 2.0f, 3.0f };
    SplinePath   path;
    SW_REQUIRE_TRUE( path.initialize( { point, point }, SplineType::Linear, false, 4 ) );
    SW_EXPECT_NEAR( 0.0f, path.getLength(), 0.0f );
    const SplineSample sample = path.sampleAtDistance( 3.0f );
    Util::expectPoint( point, sample._position );
    Util::expectPoint( float3{ 0.0f, 0.0f, 1.0f }, sample._tangent );
    Util::expectPoint( point, path.findClosest( float3{} )._position );
    vector<SplineSample> listSample;
    path.sampleUniform( 1.0f, listSample );
    SW_EXPECT_TRUE( listSample.empty() );
}
