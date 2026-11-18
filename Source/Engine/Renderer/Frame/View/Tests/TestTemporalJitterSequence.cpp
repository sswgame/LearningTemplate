#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/View/TemporalJitterSequence.h"

// TAA 서브픽셀 지터(RN-2) — 표본은 픽셀 안에 고르게 퍼져 평균이 0 근처이고, 지터를 실은 래스터 위치에서 이번 지터를 빼면 지터 없는 위치(화면 속도의 기준)와 같다.

using namespace sw;

namespace
{
    struct TemporalJitterSequenceTestUtil
    {
        static float2 projectToNdc( const float3& worldPosition, const float4x4& viewProj )
        {
            const float4 clip = float4::transform( float4{ worldPosition._x, worldPosition._y, worldPosition._z, 1.0f }, viewProj );
            return float2{ clip._x / clip._w, clip._y / clip._w };
        }

        static float4x4 makeViewProjection()
        {
            const float4x4 view       = float4x4::makeLookAt( float3{ 2.0f, 3.0f, -6.0f }, float3{ 0.0f, 0.5f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f } );
            const float4x4 projection = float4x4::makePerspectiveFieldOfView( 1.0f, 16.0f / 9.0f, 0.1f, 100.0f );
            return view * projection;
        }
    };
} // namespace

/** @brief [TemporalJitterSequenceTest] 한 바퀴의 표본은 모두 픽셀 안(-0.5 .. 0.5)이고 서로 다르며, 평균은 1/16 px 안이다(한쪽으로 치우친 그림이 남지 않는다). */
SW_TEST_CASE( TemporalJitterSequenceTest, SamplesStayInsidePixelWithNearZeroMean )
{
    float2 sum{};
    for ( uint32 sampleIndex = 0; sampleIndex < TemporalJitterSequence::kSampleCount; ++sampleIndex )
    {
        const float2 offset = TemporalJitterSequence::getPixelOffset( sampleIndex );
        SW_EXPECT_TRUE( offset._x >= -0.5f && offset._x <= 0.5f );
        SW_EXPECT_TRUE( offset._y >= -0.5f && offset._y <= 0.5f );
        for ( uint32 otherIndex = 0; otherIndex < sampleIndex; ++otherIndex )
        {
            const float2 other = TemporalJitterSequence::getPixelOffset( otherIndex );
            SW_EXPECT_FALSE( other._x == offset._x && other._y == offset._y );
        }
        sum._x += offset._x;
        sum._y += offset._y;
    }
    const float32 count = static_cast<float32>( TemporalJitterSequence::kSampleCount );
    SW_EXPECT_NEAR( 0.0, sum._x / count, 1.0 / 16.0 );
    SW_EXPECT_NEAR( 0.0, sum._y / count, 1.0 / 16.0 );
}

/** @brief [TemporalJitterSequenceTest] 지터를 실은 위치에서 이번 지터를 빼면 지터 없는 위치다 — 서 있는 카메라 · 물체의 화면 속도가 지터 때문에 0 이 아니게 되지 않는다. */
SW_TEST_CASE( TemporalJitterSequenceTest, VelocityExcludesJitter )
{
    const float4x4 viewProj = TemporalJitterSequenceTestUtil::makeViewProjection();
    const float3   arrPoint[]{
        float3{ 0.0f,   0.5f,  0.0f},
        float3{ 1.5f, -0.25f,  3.0f},
        float3{-2.0f,   1.0f, 20.0f},
    };
    TemporalJitterSequence jitter;
    for ( uint32 frame = 0; frame < TemporalJitterSequence::kSampleCount; ++frame )
    {
        const float2   ndcOffset = jitter.advance( 1280u, 720u );
        const float4x4 raster    = TemporalJitterSequence::applyToViewProjection( viewProj, ndcOffset );
        SW_EXPECT_TRUE( ndcOffset._x != 0.0f || ndcOffset._y != 0.0f );
        for ( const float3& point : arrPoint )
        {
            const float2 still    = TemporalJitterSequenceTestUtil::projectToNdc( point, viewProj );
            const float2 jittered = TemporalJitterSequenceTestUtil::projectToNdc( point, raster );
            // 래스터는 지터만큼 옮겨졌다(원근 깊이와 무관하게 같은 화면 거리).
            SW_EXPECT_NEAR( ndcOffset._x, jittered._x - still._x, 1.0e-5 );
            SW_EXPECT_NEAR( ndcOffset._y, jittered._y - still._y, 1.0e-5 );
            // 화면 속도 = (이번 래스터 위치 - 이번 지터) - 지난 위치(지터 없음). 카메라 · 물체가 서 있으면 0 이다.
            SW_EXPECT_NEAR( 0.0, ( jittered._x - ndcOffset._x ) - still._x, 1.0e-5 );
            SW_EXPECT_NEAR( 0.0, ( jittered._y - ndcOffset._y ) - still._y, 1.0e-5 );
        }
    }
}

/** @brief [TemporalJitterSequenceTest] 지터는 한 픽셀 안이다(NDC 로 2 / 크기 이하), 크기가 없으면 지터도 없다. */
SW_TEST_CASE( TemporalJitterSequenceTest, NdcOffsetIsWithinOnePixel )
{
    TemporalJitterSequence jitter;
    for ( uint32 frame = 0; frame < TemporalJitterSequence::kSampleCount; ++frame )
    {
        const float2 ndcOffset = jitter.advance( 640u, 360u );
        SW_EXPECT_TRUE( MathUtil::abs( ndcOffset._x ) <= 1.0f / 640.0f );
        SW_EXPECT_TRUE( MathUtil::abs( ndcOffset._y ) <= 1.0f / 360.0f );
    }
    const float2 none = jitter.advance( 0u, 0u );
    SW_EXPECT_TRUE( none._x == 0.0f && none._y == 0.0f );
}

/** @brief [TemporalJitterSequenceTest] 다시 시작하면 앞서 흐른 표본 수와 상관없이 첫 표본부터 같은 순서다 — 앞 씬의 프레임 수가 다음 씬의 그림을 바꾸지 않는다. */
SW_TEST_CASE( TemporalJitterSequenceTest, RestartBeginsAtFirstSample )
{
    TemporalJitterSequence fresh;
    TemporalJitterSequence used;
    for ( uint32 frame = 0; frame < 5u; ++frame )
        (void)used.advance( 640u, 360u );
    used.restart();
    for ( uint32 frame = 0; frame < TemporalJitterSequence::kSampleCount; ++frame )
    {
        const float2 expected = fresh.advance( 640u, 360u );
        const float2 actual   = used.advance( 640u, 360u );
        SW_EXPECT_TRUE( expected._x == actual._x && expected._y == actual._y );
    }
}
