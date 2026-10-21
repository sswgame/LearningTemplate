#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Lighting/ReflectionProbeData.h"

#include "TestFramework/TestFramework.h"

// ReflectionProbeDataTest — 반사 프로브 데이터(`.reflprobe`)의 형식 · 옥타헤드럴 사상 · 면 접기와 거르기. 디바이스 없음(nogpu).
// 셰이더(lighting.hlsli swEncodeOctahedral · swSampleReflectionProbeLevel)가 같은 식을 쓰므로 C++ 쪽이 맞으면 GPU 쪽은 식이 같은지만 보면 된다.

namespace sw
{
    namespace
    {
        /** @brief 면 여섯을 찍은 위치 @p capturePosition 에서 90° 정사각 원근으로 만들고, 면마다 @p arrColor 의 한 색으로 채웁니다. */
        void makeUniformFacesForTest( const float3& capturePosition, const float3 ( &arrColor )[ReflectionProbeFilter::kFaceCount], uint32 size,
                                      ReflectionProbeFace ( &outArrFace )[ReflectionProbeFilter::kFaceCount] )
        {
            const float3 arrForward[ReflectionProbeFilter::kFaceCount] = {
                float3{ 1.0f,  0.0f,  0.0f},
                float3{-1.0f,  0.0f,  0.0f},
                float3{ 0.0f,  1.0f,  0.0f},
                float3{ 0.0f, -1.0f,  0.0f},
                float3{ 0.0f,  0.0f,  1.0f},
                float3{ 0.0f,  0.0f, -1.0f}
            };
            const float4x4 projection = float4x4::makePerspectiveFieldOfView( MathUtil::kHalfPi, 1.0f, 0.05f, 100.0f );
            for ( uint32 faceIndex = 0; faceIndex < ReflectionProbeFilter::kFaceCount; ++faceIndex )
            {
                const float3 up                 = MathUtil::abs( arrForward[faceIndex]._y ) > 0.5f ? float3{ 0.0f, 0.0f, 1.0f } : float3{ 0.0f, 1.0f, 0.0f };
                outArrFace[faceIndex]._size     = size;
                outArrFace[faceIndex]._forward  = arrForward[faceIndex];
                outArrFace[faceIndex]._viewProj = float4x4::makeLookAt( capturePosition, capturePosition + arrForward[faceIndex], up ) * projection;
                outArrFace[faceIndex]._listPixel.assign( static_cast<size_t>( size ) * size, arrColor[faceIndex] );
            }
        }
    } // namespace
} // namespace sw

/**
 * @brief [ReflectionProbeDataTest] `.reflprobe` 바이트는 왕복해도 크기 · 찍은 위치 · 텍셀이 같고, 깨진 머리말은 읽지 않는다
 */
SW_TEST_CASE( ReflectionProbeDataTest, BytesRoundTrip )
{
    sw::ReflectionProbeData source{};
    source._capturePosition = sw::float3{ 1.0f, 2.0f, -3.0f };
    source._resolution      = 4;
    source._levelCount      = 3;
    source._listTexel.resize( 4 * 4 * 3 );
    for ( size_t index = 0; index < source._listTexel.size(); ++index )
    {
        source._listTexel[index] = sw::float3{ static_cast<float32>( index ), static_cast<float32>( index ) * 0.5f, 1.0f };
    }
    SW_ASSERT_TRUE( source.isValid() );
    sw::vector<uint8> bytes;
    source.makeBytes( bytes );

    sw::ReflectionProbeData loaded{};
    SW_ASSERT_TRUE( loaded.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( 4u, loaded._resolution );
    SW_EXPECT_EQUAL( 3u, loaded._levelCount );
    SW_EXPECT_NEAR_EQUAL( -3.0f, loaded._capturePosition._z, 0.0f );
    SW_ASSERT_EQUAL( source._listTexel.size(), loaded._listTexel.size() );
    SW_EXPECT_NEAR_EQUAL( source._listTexel[17]._y, loaded._listTexel[17]._y, 0.0f );

    // 한 바이트 모자라면 길이가 맞지 않는다 — 내용을 비우고 false.
    SW_EXPECT_FALSE( loaded.readFromBytes( bytes.data(), bytes.size() - 1 ) );
    SW_EXPECT_TRUE( loaded._listTexel.empty() );
}

/**
 * @brief [ReflectionProbeDataTest] 옥타헤드럴 사상은 방향을 왕복한다 — 위 · 아래 반구 모두, 축과 귀퉁이 근처도
 */
SW_TEST_CASE( ReflectionProbeDataTest, OctahedralMappingRoundTrips )
{
    constexpr uint32 kStepCount = 24;
    for ( uint32 thetaIndex = 0; thetaIndex <= kStepCount; ++thetaIndex )
    {
        for ( uint32 phiIndex = 0; phiIndex < kStepCount * 2; ++phiIndex )
        {
            const float32    theta = sw::MathUtil::kPi * static_cast<float32>( thetaIndex ) / static_cast<float32>( kStepCount );
            const float32    phi   = sw::MathUtil::kPi * static_cast<float32>( phiIndex ) / static_cast<float32>( kStepCount );
            const sw::float3 direction{ sw::MathUtil::sin( theta ) * sw::MathUtil::cos( phi ), sw::MathUtil::cos( theta ), sw::MathUtil::sin( theta ) * sw::MathUtil::sin( phi ) };
            const sw::float2 uv = sw::ReflectionProbeData::encodeOctahedral( direction );
            SW_EXPECT_TRUE( 0.0f <= uv._x && uv._x <= 1.0f && 0.0f <= uv._y && uv._y <= 1.0f );
            const sw::float3 decoded = sw::ReflectionProbeData::decodeOctahedral( uv );
            SW_EXPECT_NEAR_EQUAL( 0.0f, ( decoded - direction ).getLength(), 1e-4f );
        }
    }
    // 사각형 가운데는 +Y 다(위 반구가 가운데 마름모).
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::ReflectionProbeData::decodeOctahedral( sw::float2{ 0.5f, 0.5f } )._y, 1e-6f );
}

/**
 * @brief [ReflectionProbeDataTest] 면 여섯을 접으면 방향마다 그 방향을 보는 면의 색이고, 한 색으로 채운 둘레는 모든 거칠기 단계에서 그 색이다(거르기가 빛을 더하거나 잃지 않는다)
 */
SW_TEST_CASE( ReflectionProbeDataTest, FacesFoldAndPrefilterKeepTheirLight )
{
    const sw::float3 capturePosition{ 0.0f, 1.0f, 0.0f };
    {
        const sw::float3 arrColor[sw::ReflectionProbeFilter::kFaceCount] = {
            sw::float3{1.0f, 0.0f, 0.0f},
            sw::float3{0.0f, 1.0f, 0.0f},
            sw::float3{0.0f, 0.0f, 1.0f},
            sw::float3{1.0f, 1.0f, 0.0f},
            sw::float3{0.0f, 1.0f, 1.0f},
            sw::float3{1.0f, 0.0f, 1.0f}
        };
        sw::ReflectionProbeFace arrFace[sw::ReflectionProbeFilter::kFaceCount];
        sw::makeUniformFacesForTest( capturePosition, arrColor, 16, arrFace );
        sw::ReflectionProbeData data{};
        SW_ASSERT_TRUE( sw::ReflectionProbeFilter::makeFromFaces( capturePosition, arrFace, 16, 2, 16, data ) );
        // 축 방향은 그 면 하나만 본다 — 단계 0 을 그 방향으로 읽으면 그 면의 색이다.
        SW_EXPECT_NEAR_EQUAL( 0.0f, ( data.sampleLevel( 0, sw::float3{ 1.0f, 0.0f, 0.0f } ) - arrColor[0] ).getLength(), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, ( data.sampleLevel( 0, sw::float3{ 0.0f, 1.0f, 0.0f } ) - arrColor[2] ).getLength(), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, ( data.sampleLevel( 0, sw::float3{ 0.0f, 0.0f, -1.0f } ) - arrColor[5] ).getLength(), 1e-4f );
    }
    {
        const sw::float3        kUniform{ 2.0f, 1.0f, 0.5f };
        const sw::float3        arrColor[sw::ReflectionProbeFilter::kFaceCount] = { kUniform, kUniform, kUniform, kUniform, kUniform, kUniform };
        sw::ReflectionProbeFace arrFace[sw::ReflectionProbeFilter::kFaceCount];
        sw::makeUniformFacesForTest( capturePosition, arrColor, 8, arrFace );
        sw::ReflectionProbeData data{};
        SW_ASSERT_TRUE( sw::ReflectionProbeFilter::makeFromFaces( capturePosition, arrFace, 8, 4, 32, data ) );
        SW_ASSERT_TRUE( data.isValid() );
        for ( const sw::float3& texel : data._listTexel )
        {
            SW_EXPECT_NEAR_EQUAL( 0.0f, ( texel - kUniform ).getLength(), 1e-3f );
        }
    }
    // 면 크기와 픽셀 수가 다르면 접지 않는다.
    {
        const sw::float3        arrColor[sw::ReflectionProbeFilter::kFaceCount] = {};
        sw::ReflectionProbeFace arrFace[sw::ReflectionProbeFilter::kFaceCount];
        sw::makeUniformFacesForTest( capturePosition, arrColor, 8, arrFace );
        arrFace[3]._listPixel.pop_back();
        sw::ReflectionProbeData data{};
        SW_EXPECT_FALSE( sw::ReflectionProbeFilter::makeFromFaces( capturePosition, arrFace, 8, 4, 32, data ) );
    }
}
