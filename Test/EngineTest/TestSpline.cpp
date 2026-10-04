// 스플라인 — Catmull-Rom · 베지어 · 꺾은선, 호 길이 매개변수, 가장 가까운 점, 고른 간격 샘플.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Spline/SplinePath.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

/**
 * @brief [SplineTest] 호 길이 매개변수 — 꺾은선은 정확한 길이, 거리 s 의 점까지의 곡선 길이가 s 이고(고른 간격 샘플의 이웃 거리가 같다), 닫힌 곡선은 감긴다
 */
SW_TEST_CASE( SplineTest, ArcLengthParameterization )
{
    SplinePath polyline;
    SW_ASSERT_TRUE( polyline.initialize( {
                                             float3{0.0f, 0.0f, 0.0f},
                                             float3{3.0f, 0.0f, 0.0f},
                                             float3{3.0f, 4.0f, 0.0f}
    },
                                         SplineType::Linear, false, 4 ) );
    SW_EXPECT_NEAR_EQUAL( 7.0f, polyline.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, polyline.sampleAtDistance( 5.0f )._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, polyline.sampleAtDistance( 5.0f )._position._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, polyline.sampleAtDistance( 5.0f )._tangent._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 7.0f, polyline.sampleAtDistance( 100.0f )._distance, 1.0e-4f ); // 열린 곡선은 잘린다

    // 곡선 — 거리 s 로 고른 점의 참 호 길이(아주 촘촘한 표로 잰 값)가 s 다. 매개변수 t 를 고르게 나눈 점은 그렇지 않다.
    const vector<float3> listPoint = {
        float3{0.0f, 0.0f, 0.0f},
        float3{1.0f, 0.0f, 0.0f},
        float3{5.0f, 0.0f, 3.0f},
        float3{8.0f, 0.0f, 3.0f}
    };
    SplinePath curve;
    SplinePath reference;
    SW_ASSERT_TRUE( curve.initialize( listPoint, SplineType::CatmullRom, false, 32 ) );
    SW_ASSERT_TRUE( reference.initialize( listPoint, SplineType::CatmullRom, false, 2048 ) );
    vector<SplineSample> listSample;
    curve.sampleUniform( 0.5f, listSample );
    SW_ASSERT_TRUE( listSample.size() > 4 );
    float32 maxError = 0.0f;
    for ( const SplineSample& sample : listSample )
        maxError = MathUtil::max( maxError, MathUtil::abs( reference.findClosest( sample._position )._distance - sample._distance ) );
    SW_EXPECT_TRUE_MSG( maxError < 0.01f, std::to_string( maxError ).c_str() );
    const float32 middleOfSegment = reference.findClosest( curve.evaluate( 0.5f ) )._distance; // 첫 구간의 t = 0.5
    SW_EXPECT_TRUE( MathUtil::abs( middleOfSegment - reference.findClosest( curve.evaluate( 1.0f ) )._distance * 0.5f ) > 0.05f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, listSample.back()._position._x, 1.0e-3f ); // 끝점 포함

    // 닫힌 곡선 — 길이 + 1 은 1 로 감기고, 음수도 감긴다.
    SplinePath loop;
    SW_ASSERT_TRUE( loop.initialize( {
                                         float3{0.0f, 0.0f, 0.0f},
                                         float3{4.0f, 0.0f, 0.0f},
                                         float3{4.0f, 0.0f, 4.0f},
                                         float3{0.0f, 0.0f, 4.0f}
    },
                                     SplineType::Linear, true, 2 ) );
    SW_EXPECT_NEAR_EQUAL( 16.0f, loop.getLength(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, loop.sampleAtDistance( 17.0f )._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, loop.sampleAtDistance( -1.0f )._position._z, 1.0e-4f ); // 마지막 변 (0,0,4)→(0,0,0) 의 끝 1 m 전
}

/**
 * @brief [SplineTest] 3차 베지어는 끝점을 지나고 손잡이 쪽으로 휘며, 개수 규칙(3n+1 · 닫힌 3n)이 틀리면 짓지 않는다
 */
SW_TEST_CASE( SplineTest, BezierPassesAnchorsAndChecksShape )
{
    SplinePath bezier;
    SW_ASSERT_TRUE( bezier.initialize( {
                                           float3{0.0f, 0.0f, 0.0f},
                                           float3{0.0f, 2.0f, 0.0f},
                                           float3{4.0f, 2.0f, 0.0f},
                                           float3{4.0f, 0.0f, 0.0f}
    },
                                       SplineType::Bezier, false, 32 ) );
    SW_EXPECT_NEAR_EQUAL( 4.0f, bezier.sampleAtFraction( 1.0f )._position._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.5f, bezier.evaluate( 0.5f )._y, 1.0e-4f ); // 3/4 × 손잡이 높이
    SW_EXPECT_TRUE( bezier.getLength() > 4.0f );

    SplinePath broken;
    SW_EXPECT_FALSE( broken.initialize( {
                                            float3{},
                                            float3{ 1.0f, 0.0f, 0.0f },
                                            float3{ 2.0f, 0.0f, 0.0f },
                                            float3{ 3.0f, 0.0f, 0.0f },
                                            float3{ 4.0f, 0.0f, 0.0f }
    },
                                        SplineType::Bezier, false ) );
    SW_EXPECT_FALSE( broken.isValid() );
    SW_EXPECT_FALSE( broken.initialize( { float3{} }, SplineType::CatmullRom, false ) );
}

/**
 * @brief [SplineTest] 가장 가까운 점 — 곡선 옆의 점은 곡선 위 가까운 자리와 그 호 길이를 돌려준다(카메라 레일 · 길 위 자리)
 */
SW_TEST_CASE( SplineTest, ClosestPointReturnsArcDistance )
{
    SplinePath path;
    SW_ASSERT_TRUE( path.initialize( {
                                         float3{ 0.0f, 0.0f,  0.0f},
                                         float3{10.0f, 0.0f,  0.0f},
                                         float3{10.0f, 0.0f, 10.0f}
    },
                                     SplineType::Linear, false, 8 ) );
    const SplineSample nearFirst = path.findClosest( float3{ 3.0f, 0.0f, -2.0f } );
    SW_EXPECT_NEAR_EQUAL( 3.0f, nearFirst._distance, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, nearFirst._position._z, 1.0e-3f );
    const SplineSample nearSecond = path.findClosest( float3{ 12.0f, 0.0f, 6.0f } );
    SW_EXPECT_NEAR_EQUAL( 16.0f, nearSecond._distance, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, nearSecond._position._x, 1.0e-3f );

    SplinePath curve;
    SW_ASSERT_TRUE( curve.initialize( {
                                          float3{ 0.0f, 0.0f, 0.0f},
                                          float3{ 5.0f, 0.0f, 5.0f},
                                          float3{10.0f, 0.0f, 0.0f}
    },
                                      SplineType::CatmullRom, false, 64 ) );
    const SplineSample onCurve = curve.sampleAtDistance( curve.getLength() * 0.3f );
    const SplineSample found   = curve.findClosest( onCurve._position + float3{ 0.0f, 1.0f, 0.0f } ); // 곡선 바로 위
    SW_EXPECT_NEAR_EQUAL( onCurve._distance, found._distance, 0.02f );
}
