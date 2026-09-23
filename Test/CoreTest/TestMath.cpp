#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Physics/AABB.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Math — float2/3/4·행렬·쿼터니언·MathUtil
// ------------------------------------------------------------------------------
/**
 * @brief [MathTest] float2 전체
 */

SW_TEST_CASE( MathTest, Float2FullTest )
{

    sw::float2 v0;
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._y, 1e-4f );

    sw::float2 vScalar( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, vScalar._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, vScalar._y, 1e-4f );

    sw::float2 vComp( 3.0f, 4.0f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, vComp._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, vComp._y, 1e-4f );

    float32    arr[2] = { 1.0f, 2.0f };
    sw::float2 vArr( arr );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vArr._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, vArr._y, 1e-4f );

    SW_EXPECT_NEAR_EQUAL( 5.0f, vComp.getLength(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, vComp.getLengthSquared(), 1e-4f );

    const sw::float2 constVComp  = vComp;
    sw::float2       vNormalized = constVComp.normalize();
    SW_EXPECT_NEAR_EQUAL( 0.6f, vNormalized._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, vNormalized._y, 1e-4f );

    sw::float2 vMutable = vComp;
    vMutable.normalize();
    SW_EXPECT_NEAR_EQUAL( 1.0f, vMutable.getLength(), 1e-4f );

    SW_EXPECT_NEAR_EQUAL( 11.0f, vComp.dot( sw::float2( 1.0f, 2.0f ) ), 1e-4f );

    sw::float2 clampedVal = vComp.clamped( sw::float2( 0.0f, 0.0f ), sw::float2( 3.5f, 3.5f ) );
    SW_EXPECT_NEAR_EQUAL( 3.0f, clampedVal._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.5f, clampedVal._y, 1e-4f );

    SW_EXPECT_NEAR_EQUAL( 5.0f, sw::float2::getDistance( sw::float2( 0.0f, 0.0f ), sw::float2( 3.0f, 4.0f ) ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 25.0f, sw::float2::getDistanceSquared( sw::float2( 0.0f, 0.0f ), sw::float2( 3.0f, 4.0f ) ), 1e-4f );

    sw::float2 minV = sw::float2::min( sw::float2( 1.0f, 5.0f ), sw::float2( 2.0f, 3.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, minV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, minV._y, 1e-4f );

    sw::float2 maxV = sw::float2::max( sw::float2( 1.0f, 5.0f ), sw::float2( 2.0f, 3.0f ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, maxV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, maxV._y, 1e-4f );

    sw::float2 lerpV = sw::float2::lerp( sw::float2( 0.0f, 0.0f ), sw::float2( 10.0f, 20.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, lerpV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, lerpV._y, 1e-4f );

    sw::float2 smoothV = sw::float2::smoothStep( sw::float2( 0.0f, 0.0f ), sw::float2( 10.0f, 20.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, smoothV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, smoothV._y, 1e-4f );

    sw::float2 baryV = sw::float2::barycentric( sw::float2( 0.0f, 0.0f ), sw::float2( 10.0f, 0.0f ), sw::float2( 0.0f, 10.0f ), 0.5f, 0.25f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, baryV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, baryV._y, 1e-4f );

    sw::float2 catmullV = sw::float2::catmullRom( sw::float2( 0.0f ), sw::float2( 10.0f ), sw::float2( 20.0f ), sw::float2( 30.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, catmullV._x, 1e-4f );

    sw::float2 hermiteV = sw::float2::hermite( sw::float2( 0.0f ), sw::float2( 10.0f ), sw::float2( 20.0f ), sw::float2( 10.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, hermiteV._x, 1e-4f );

    sw::float2 reflV = sw::float2::reflect( sw::float2( 1.0f, -1.0f ), sw::float2( 0.0f, 1.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, reflV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, reflV._y, 1e-4f );

    sw::float2 a( 2.0f, 3.0f );
    sw::float2 b( 4.0f, 5.0f );
    SW_EXPECT_TRUE( a == sw::float2( 2.0f, 3.0f ) );
    SW_EXPECT_TRUE( a != b );

    sw::float2 addV = a + b;
    SW_EXPECT_NEAR_EQUAL( 6.0f, addV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 8.0f, addV._y, 1e-4f );

    sw::float2 subV = b - a;
    SW_EXPECT_NEAR_EQUAL( 2.0f, subV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, subV._y, 1e-4f );

    sw::float2 mulV = a * 2.0f;
    SW_EXPECT_NEAR_EQUAL( 4.0f, mulV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, mulV._y, 1e-4f );

    sw::float2 divV = b / 2.0f;
    SW_EXPECT_NEAR_EQUAL( 2.0f, divV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, divV._y, 1e-4f );

    sw::float2 negV = -a;
    SW_EXPECT_NEAR_EQUAL( -2.0f, negV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -3.0f, negV._y, 1e-4f );
}

/**
 * @brief [MathTest] float3 전체
 */
SW_TEST_CASE( MathTest, Float3FullTest )
{

    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::float3::Zero._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitX._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitY._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitZ._z, 1e-4f );

    sw::float3 v3( 1.0f, 2.0f, 2.0f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, v3.getLength(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 9.0f, v3.getLengthSquared(), 1e-4f );

    const sw::float3 constV3 = v3;
    sw::float3       vNorm   = constV3.normalize();
    SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, vNorm._x, 1e-4f );

    sw::float3 v1( 1.0f, 0.0f, 0.0f );
    sw::float3 v2( 0.0f, 1.0f, 0.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v1.dot( v2 ), 1e-4f );

    sw::float3 crossV = v1.cross( v2 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, crossV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, crossV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, crossV._z, 1e-4f );

    SW_EXPECT_TRUE( v1.isInBounds( sw::float3( 2.0f ) ) );

    sw::float3 lerpV = sw::float3::lerp( sw::float3::Zero, sw::float3( 10.0f, 20.0f, 30.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, lerpV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, lerpV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, lerpV._z, 1e-4f );

    sw::float3 smoothV = sw::float3::smoothStep( sw::float3::Zero, sw::float3( 10.0f, 20.0f, 30.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, smoothV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, smoothV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, smoothV._z, 1e-4f );

    sw::float3 baryV = sw::float3::barycentric( sw::float3( 0.0f ), sw::float3( 10.0f, 0.0f, 0.0f ), sw::float3( 0.0f, 10.0f, 0.0f ), 0.5f, 0.25f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, baryV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, baryV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, baryV._z, 1e-4f );

    sw::float3 catmullV = sw::float3::catmullRom( sw::float3( 0.0f ), sw::float3( 10.0f ), sw::float3( 20.0f ), sw::float3( 30.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 15.0f, catmullV._x, 1e-4f );

    sw::float3 hermiteV = sw::float3::hermite( sw::float3( 0.0f ), sw::float3( 10.0f ), sw::float3( 20.0f ), sw::float3( 10.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, hermiteV._x, 1e-4f );

    sw::float3 minV = sw::float3::min( sw::float3( 1.0f, 5.0f, 9.0f ), sw::float3( 2.0f, 3.0f, 4.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, minV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, minV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, minV._z, 1e-4f );

    sw::float3 addV = v1 + v2;
    SW_EXPECT_NEAR_EQUAL( 1.0f, addV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, addV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, addV._z, 1e-4f );

    sw::float3 mulV = v3 * 3.0f;
    SW_EXPECT_NEAR_EQUAL( 3.0f, mulV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, mulV._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, mulV._z, 1e-4f );
}

/**
 * @brief [MathTest] float4 전체
 */
SW_TEST_CASE( MathTest, Float4FullTest )
{
    sw::float4 v0;
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, v0._w, 1e-4f );

    sw::float4 vComp( 1.0f, 2.0f, 3.0f, 4.0f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, vComp._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, vComp._w, 1e-4f );

    SW_EXPECT_NEAR_EQUAL( 30.0f, vComp.getLengthSquared(), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::sqrt( 30.0f ), vComp.getLength(), 1e-4f );

    const sw::float4 constVComp = vComp;
    sw::float4       vNorm      = constVComp.normalize();
    SW_EXPECT_NEAR_EQUAL( 1.0f, vNorm.getLength(), 1e-4f );

    SW_EXPECT_NEAR_EQUAL( 30.0f, vComp.dot( vComp ), 1e-4f );

    sw::float4 lerpV = sw::float4::lerp( sw::float4::Zero, sw::float4( 10.0f, 20.0f, 30.0f, 40.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, lerpV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, lerpV._w, 1e-4f );

    sw::float4 smoothV = sw::float4::smoothStep( sw::float4::Zero, sw::float4( 10.0f, 20.0f, 30.0f, 40.0f ), 0.5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, smoothV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, smoothV._w, 1e-4f );

    sw::float4 baryV = sw::float4::barycentric( sw::float4( 0.0f ), sw::float4( 10.0f, 0.0f, 0.0f, 0.0f ), sw::float4( 0.0f, 10.0f, 0.0f, 0.0f ), 0.5f, 0.25f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, baryV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, baryV._y, 1e-4f );

    sw::float4 addV = vComp + sw::float4( 1.0f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, addV._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, addV._w, 1e-4f );
}

/**
 * @brief [MathTest] float4x4 전체
 */
SW_TEST_CASE( MathTest, Float4x4FullTest )
{

    sw::float4x4 identity = sw::float4x4::Identity;
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._11, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._12, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity.determinant(), 1e-4f );

    sw::float4x4 transM = sw::float4x4::createTranslation( 10.0f, 20.0f, 30.0f );
    sw::float3   pos    = transM.getTranslation();
    SW_EXPECT_NEAR_EQUAL( 10.0f, pos._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pos._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pos._z, 1e-4f );

    sw::float4x4 scaleM = sw::float4x4::createScale( 2.0f, 3.0f, 4.0f );
    sw::float3   scale  = scaleM.getScale();
    SW_EXPECT_NEAR_EQUAL( 2.0f, scale._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, scale._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, scale._z, 1e-4f );

    const sw::float4x4 constTransM = transM;
    sw::float4x4       transResult = constTransM.transpose();
    SW_EXPECT_NEAR_EQUAL( 10.0f, transResult._14, 1e-4f );

    sw::float4x4 invM          = constTransM.invert();
    sw::float4x4 identityCheck = transM * invM;
    SW_EXPECT_NEAR_EQUAL( 1.0f, identityCheck._11, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identityCheck._12, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identityCheck._41, 1e-4f );

    sw::float4x4 projM = sw::float4x4::createPerspectiveFieldOfView( sw::MathUtil::toRadian( 60.0f ), 16.0f / 9.0f, 0.1f, 1000.0f );
    SW_EXPECT_TRUE( projM.determinant() != 0.0f );

    sw::float4x4 viewM = sw::float4x4::createLookAt( sw::float3( 0.0f, 0.0f, -10.0f ), sw::float3::Zero, sw::float3::Up );
    SW_EXPECT_TRUE( viewM.determinant() != 0.0f );
}

/**
 * @brief [MathTest] Quaternion 전체
 */
SW_TEST_CASE( MathTest, QuaternionFullTest )
{

    sw::quaternion identity = sw::quaternion::Identity;
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._w, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity.norm(), 1e-4f );

    sw::quaternion qRot = sw::quaternion::createFromAxisAngle( sw::float3::Up, sw::MathUtil::toRadian( 90.0f ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, qRot.norm(), 1e-4f );

    sw::float3 forward( 0.0f, 0.0f, 1.0f );
    sw::float3 rotated = sw::float3::transform( forward, qRot );
    SW_EXPECT_NEAR_EQUAL( 1.0f, rotated._x, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, rotated._z, 1e-3f );

    const sw::quaternion constQRot = qRot;
    sw::quaternion       invQ      = constQRot.inverse();
    sw::quaternion       resultQ   = qRot * invQ;
    SW_EXPECT_NEAR_EQUAL( 0.0f, resultQ._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, resultQ._w, 1e-4f );

    sw::quaternion slerpQ = sw::quaternion::slerp( identity, qRot, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, slerpQ.norm(), 1e-4f );

    sw::quaternion qYawPitchRoll = sw::quaternion::createFromYawPitchRoll( sw::MathUtil::toRadian( 45.0f ), 0.0f, 0.0f );
    sw::float3     euler         = qYawPitchRoll.getEulerAngles();
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::toRadian( 45.0f ), euler._y, 1e-3f );
}

/**
 * @brief [MathTest] MathUtil 함수 전체
 */
SW_TEST_CASE( MathTest, MathUtilFunctionsFull )
{
    float32 clamped = sw::MathUtil::clamp( 15.0f, 0.0f, 10.0f );
    SW_EXPECT_NEAR_EQUAL( 10.0f, clamped, 1e-4f );

    float32 saturated = sw::MathUtil::saturate( 1.5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, saturated, 1e-4f );

    float32 lerped = sw::MathUtil::lerp( 0.0f, 100.0f, 0.5f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, lerped, 1e-4f );

    float32 invLerp = sw::MathUtil::inverseLerp( 10.0f, 20.0f, 15.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, invLerp, 1e-4f );

    float32 smoothstepVal = sw::MathUtil::smoothstep( 0.0f, 10.0f, 5.0f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, smoothstepVal, 1e-4f );

    float32 sq = sw::MathUtil::square( 4.0f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, sq, 1e-4f );

    float32 p4 = sw::MathUtil::pow4( 2.0f );
    SW_EXPECT_NEAR_EQUAL( 16.0f, p4, 1e-4f );

    float32 fractional = sw::MathUtil::frac( 3.75f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, fractional, 1e-4f );

    uint32 aligned = sw::MathUtil::align( 13u, 8u );
    SW_EXPECT_EQUAL( 16u, aligned );

    float32 rad = sw::MathUtil::toRadian( 180.0f );
    SW_EXPECT_NEAR_EQUAL( sw::MathUtil::Pi, rad, 1e-4f );

    float32 rndVal = sw::MathUtil::getRandomRange( 1.0f, 5.0f );
    SW_EXPECT_TRUE( 1.0f <= rndVal && rndVal <= 5.0f );
}

/**
 * @brief [MathTest] float4x4 TRS 합성, 전치, 역행렬 및 벡터 변환 검증
 */
SW_TEST_CASE( MathTest, Matrix4x4TRSAndInversion )
{
    // 1) 이동 행렬과 벡터 변환 (Row-Major)
    const sw::float4x4 trans = sw::float4x4::createTranslation( sw::float3( 10.0f, 20.0f, 30.0f ) );
    const sw::float4   point( 1.0f, 2.0f, 3.0f, 1.0f );
    const sw::float4   transformed = sw::float4::transform( point, trans );

    SW_EXPECT_NEAR_EQUAL( 11.0f, transformed._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 22.0f, transformed._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 33.0f, transformed._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, transformed._w, 1e-4f );

    // 2) 스케일 행렬
    const sw::float4x4 scale  = sw::float4x4::createScale( sw::float3( 2.0f, 3.0f, 4.0f ) );
    const sw::float4   scaled = sw::float4::transform( point, scale );
    SW_EXPECT_NEAR_EQUAL( 2.0f, scaled._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 6.0f, scaled._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 12.0f, scaled._z, 1e-4f );

    // 3) 전치 행렬 검증
    const sw::float4x4 transposed = trans.transpose();
    SW_EXPECT_NEAR_EQUAL( 10.0f, transposed._14, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, transposed._24, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, transposed._34, 1e-4f );

    // 4) 역행렬 검증 (M * M^-1 = Identity)
    const sw::float4x4 combined    = scale * trans;
    const sw::float4x4 invCombined = combined.invert();
    const sw::float4x4 identity    = combined * invCombined;

    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._11, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._22, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._33, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, identity._44, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._12, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, identity._14, 1e-3f );
}

/**
 * @brief [MathTest] MathUtil::align 0 정렬 및 getRandomRange 역경계/8비트 정수 엣지 케이스 검증
 */
SW_TEST_CASE( MathTest, MathUtilAlignZeroAndRandomRangeEdgeCases )
{
    // 1) align 0 전달 시 Divide-by-Zero 없이 원본 반환
    SW_EXPECT_EQUAL( 13u, sw::MathUtil::align( 13u, 0u ) );
    SW_EXPECT_EQUAL( 27u, sw::MathUtil::align( 27u, 0u ) );

    // 2) from > to 역구간 전달 시 자동 스왑 안전성
    for ( int32 iter = 0; iter < 10; ++iter )
    {
        int32 val = sw::MathUtil::getRandomRange( 20, 10 );
        SW_EXPECT_TRUE( 10 <= val && val <= 20 );
    }

    // 3) int8 / uint8 8비트 정수 타입 전달 시 승격 및 안전한 범위 추출
    for ( int32 iter = 0; iter < 10; ++iter )
    {
        int8 s8Val = sw::MathUtil::getRandomRange( static_cast<int8>( -5 ), static_cast<int8>( 5 ) );
        SW_EXPECT_TRUE( -5 <= s8Val && s8Val <= 5 );

        uint8 u8Val = sw::MathUtil::getRandomRange( static_cast<uint8>( 10 ), static_cast<uint8>( 20 ) );
        SW_EXPECT_TRUE( 10 <= u8Val && u8Val <= 20 );
    }
}

/**
 * @brief [MathTest] float4x4::createPerspectiveFieldOfView Near >= Far 입력 시 안전 클램핑 검증
 */
SW_TEST_CASE( MathTest, PerspectiveFieldOfViewNearFarEdgeCase )
{
    // Near >= Far 시 near/far 역전 크래시 방지 및 유효한 투영 행렬 생성
    sw::float4x4 proj = sw::float4x4::createPerspectiveFieldOfView( sw::MathUtil::Pi / 4.0f, 1.777f, 100.0f, 10.0f );
    SW_EXPECT_TRUE( proj._33 != 0.0f );
    SW_EXPECT_TRUE( proj._34 != 0.0f );
}

/**
 * @brief [MathTest] float3::transformNormal 비균등 스케일 변환 시 법선 직교성 검증
 */
SW_TEST_CASE( MathTest, VectorTransformNormalNonUniformScale )
{
    // (0, 1, 0) 법선 벡터에 (2, 5, 2) 비균등 스케일 적용
    sw::float4x4 nonUniformScale = sw::float4x4::createScale( sw::float3{ 2.0f, 5.0f, 2.0f } );
    sw::float3   unitY{ 0.0f, 1.0f, 0.0f };
    sw::float3   transformedNormal = sw::float3::transformNormal( unitY, nonUniformScale ).normalize();

    // Y축 방향 법선은 여전히 Y축 방향이어야 하며 길이가 1이어야 함
    SW_EXPECT_NEAR_EQUAL( 0.0f, transformedNormal._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, transformedNormal._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, transformedNormal._z, 1e-4f );
}

/**
 * @brief [MathTest] AABB::empty 미초기화 시 getExtents() 부동소수점 오버플로우 방어 검증
 */
SW_TEST_CASE( MathTest, AABBEmptyExtentsSafety )
{
    sw::AABB emptyBox = sw::AABB::empty();
    SW_EXPECT_FALSE( emptyBox.isValid() );

    sw::float3 extents = emptyBox.getExtents();
    SW_EXPECT_NEAR_EQUAL( 0.0f, extents._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, extents._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, extents._z, 1e-4f );
}

// ------------------------------------------------------------------------------
// 5) MathTest — Double3 벡터
// ------------------------------------------------------------------------------
/**
 * @brief [MathTest] double3 벡터 연산
 */
SW_TEST_CASE( MathTest, Double3VectorOperations )
{
    sw::double3 v1( 3.0, 4.0, 0.0 );
    SW_EXPECT_NEAR_EQUAL( 5.0, v1.getLength(), 1e-6 );

    v1.normalize();
    SW_EXPECT_NEAR_EQUAL( 0.6, v1._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 0.8, v1._y, 1e-6 );

    sw::float3  f3( 10.0f, 20.0f, 30.0f );
    sw::double3 d3FromF3( f3 );
    SW_EXPECT_NEAR_EQUAL( 10.0, d3FromF3._x, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 20.0, d3FromF3._y, 1e-6 );
    SW_EXPECT_NEAR_EQUAL( 30.0, d3FromF3._z, 1e-6 );

    sw::float3 convertedF3 = d3FromF3.toFloat3();
    SW_EXPECT_NEAR_EQUAL( 10.0f, convertedF3._x, 1e-4f );
}

/**
 * @brief [MathTest] 네 벡터 타입의 `isInBounds` 가 **같은 답**을 낸다
 * @details 경계는 포함이고 범위는 `[-bound, +bound]` 다. `float4` 만 비교를 뒤집어 적고 있었는데
 *          (AGENTS 의 "값을 가운데 두는" 범위 비교 규칙과도 어긋난다) 결과는 같아야 한다 —
 *          같은 이름의 함수가 타입마다 다르게 읽히면 그 자체가 다음 실수의 씨앗이다.
 */
SW_TEST_CASE( MathTest, IsInBoundsAgreesAcrossVectorTypes )
{
    const sw::float2 bound2{ 2.0f, 2.0f };
    const sw::float3 bound3{ 2.0f, 2.0f, 2.0f };
    const sw::float4 bound4{ 2.0f, 2.0f, 2.0f, 2.0f };

    // 안쪽
    SW_EXPECT_TRUE( sw::float2( 1.0f, -1.0f ).isInBounds( bound2 ) );
    SW_EXPECT_TRUE( sw::float3( 1.0f, -1.0f, 0.0f ).isInBounds( bound3 ) );
    SW_EXPECT_TRUE( sw::float4( 1.0f, -1.0f, 0.0f, -2.0f ).isInBounds( bound4 ) );

    // 경계는 포함
    SW_EXPECT_TRUE( sw::float2( 2.0f, -2.0f ).isInBounds( bound2 ) );
    SW_EXPECT_TRUE( sw::float3( 2.0f, -2.0f, 2.0f ).isInBounds( bound3 ) );
    SW_EXPECT_TRUE( sw::float4( 2.0f, -2.0f, 2.0f, -2.0f ).isInBounds( bound4 ) );

    // 어느 성분이든 넘으면 false — w 성분도 마찬가지다.
    SW_EXPECT_FALSE( sw::float2( 2.5f, 0.0f ).isInBounds( bound2 ) );
    SW_EXPECT_FALSE( sw::float3( 0.0f, 0.0f, -2.5f ).isInBounds( bound3 ) );
    SW_EXPECT_FALSE( sw::float4( 0.0f, 0.0f, 0.0f, 2.5f ).isInBounds( bound4 ) );
    SW_EXPECT_FALSE( sw::float4( 0.0f, 0.0f, 0.0f, -2.5f ).isInBounds( bound4 ) );
}

/**
 * @brief [MathTest] 뒤집을 수 없는 행렬은 **Identity 로 돌아온다** (조용히 쓰레기를 내지 않는다)
 * @details 실패를 알리는 통로가 없어서 그렇게 정해 두었다. 그 계약을 여기서 못박는다 — 스케일 0 인
 *          트랜스폼을 뒤집으면 렌더러가 엉뚱한 자리에 그리는데, 적어도 값은 예측 가능해야 한다.
 */
SW_TEST_CASE( MathTest, SingularMatrixInvertsToIdentity )
{
    sw::float4x4 singular = sw::float4x4::Identity;
    singular._11          = 0.0f; // X 스케일 0 — 되돌릴 수 없다
    singular._22          = 0.0f;

    SW_EXPECT_TRUE( sw::MathUtil::abs( singular.determinant() ) < 1e-7f );

    const sw::float4x4 inverted = singular.invert();
    SW_EXPECT_TRUE_MSG( inverted == sw::float4x4::Identity,
                        "특이행렬의 역행렬이 Identity 가 아니다 — 문서가 약속한 값과 다르다" );

    // 정상 행렬은 실제로 뒤집힌다(위 계약이 정상 경로를 망가뜨리지 않았는지).
    sw::float4x4 scale              = sw::float4x4::Identity;
    scale._11                       = 2.0f;
    scale._22                       = 4.0f;
    const sw::float4x4 scaleInverse = scale.invert();
    SW_EXPECT_TRUE( sw::MathUtil::abs( scaleInverse._11 - 0.5f ) < 1e-5f );
    SW_EXPECT_TRUE( sw::MathUtil::abs( scaleInverse._22 - 0.25f ) < 1e-5f );
}

/**
 * @brief [MathTest] `createTrs` 는 행렬 셋을 곱한 것과 **같은 값**이다
 * @details 곱을 생략하는 지름길이라 "빠른데 값이 다르다" 가 가장 무서운 실패다. 비교 대상을 손으로
 *          적지 않고 **원래 식 그대로**(S * R * T) 두어, 규격(행-벡터 · 요/피치/롤 해석)이 바뀌면
 *          둘이 함께 움직이게 한다. 비대칭 스케일·세 축 회전·0 이 아닌 이동을 섞어야 행이 뒤바뀐
 *          구현이 통과하지 못한다.
 */
SW_TEST_CASE( MathTest, CreateTrsMatchesTheProductOfThree )
{
    const sw::float3 position{ 3.0f, -7.5f, 2.25f };
    const sw::float3 rotation{ 0.37f, -1.1f, 0.62f }; // 피치 · 요 · 롤 (라디안)
    const sw::float3 scale{ 2.0f, 0.5f, 3.25f };      // 축마다 달라야 행을 바꿔치기한 구현이 걸린다

    const sw::float4x4 expected = sw::float4x4::createScale( scale ) *
                                  sw::float4x4::createFromYawPitchRoll( rotation._y, rotation._x, rotation._z ) *
                                  sw::float4x4::createTranslation( position );
    const sw::float4x4 actual = sw::float4x4::createTrs( position, rotation, scale );

    const float32* pExpected = &expected._11;
    const float32* pActual   = &actual._11;
    for ( int32 elementIndex = 0; elementIndex < 16; ++elementIndex )
        SW_EXPECT_NEAR_EQUAL( pExpected[elementIndex], pActual[elementIndex], 1e-5f );

    // 쿼터니언 오버로드도 같은 값이어야 한다 — 오일러 쪽이 그쪽으로 넘기므로 둘이 갈라지면
    // 애니메이션(쿼터니언)과 컴포넌트(오일러)가 서로 다른 행렬을 쓰게 된다.
    const sw::quaternion rotationQuat = sw::quaternion::createFromYawPitchRoll( rotation._y, rotation._x, rotation._z );
    const sw::float4x4   fromQuat     = sw::float4x4::createTrs( position, rotationQuat, scale );
    const float32*       pFromQuat    = &fromQuat._11;
    for ( int32 elementIndex = 0; elementIndex < 16; ++elementIndex )
        SW_EXPECT_NEAR_EQUAL( pExpected[elementIndex], pFromQuat[elementIndex], 1e-5f );

    // 점 하나를 실제로 변환해 본다 — 16개 성분이 맞아도 규격을 잘못 읽었으면 여기서 갈린다.
    const sw::float4 point{ 1.0f, 2.0f, 3.0f, 1.0f };
    const sw::float4 byExpected = sw::float4::transform( point, expected );
    const sw::float4 byActual   = sw::float4::transform( point, actual );
    SW_EXPECT_NEAR_EQUAL( byExpected._x, byActual._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( byExpected._y, byActual._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( byExpected._z, byActual._z, 1e-4f );
}

/**
 * @brief [MathTest] 회전이 없는 `createTrs` 지름길은 단위 사원수를 거친 값과 **같다**
 * @details 오일러 오버로드는 세 각이 모두 0 이면 사원수를 만들지 않고 대각선에 스케일만 놓는다(움직이는 컴포넌트마다 지나는
 *          자리라 삼각 함수 여섯 번이 아깝다). 지름길이 틀리면 회전 없는 물체만 조용히 틀어지므로, 원래 경로(단위 사원수 오버로드)와
 *          성분마다 정확히 견준다. 음수 스케일과 -0 각도도 섞는다 — 둘 다 "0 인가" 판정과 부호가 엇갈리기 쉬운 자리다.
 */
SW_TEST_CASE( MathTest, CreateTrsWithoutRotationMatchesIdentityQuaternion )
{
    const sw::float3 position{ -4.0f, 12.5f, 0.75f };
    const sw::float3 arrScale[] = {
        sw::float3{ 1.0f, 1.0f,   1.0f},
        sw::float3{ 2.0f, 0.5f,  3.25f},
        sw::float3{-1.5f, 2.0f, -0.25f}
    };
    const sw::float3 arrRotation[] = {
        sw::float3{ 0.0f, 0.0f,  0.0f},
        sw::float3{-0.0f, 0.0f, -0.0f}
    };

    for ( const sw::float3& scale : arrScale )
    {
        const sw::float4x4 expected  = sw::float4x4::createTrs( position, sw::quaternion{ 0.0f, 0.0f, 0.0f, 1.0f }, scale );
        const float32*     pExpected = &expected._11;
        for ( const sw::float3& rotation : arrRotation )
        {
            const sw::float4x4 actual  = sw::float4x4::createTrs( position, rotation, scale );
            const float32*     pActual = &actual._11;
            for ( int32 elementIndex = 0; elementIndex < 16; ++elementIndex )
                SW_EXPECT_EQUAL( pExpected[elementIndex], pActual[elementIndex] );
        }
    }

    // 한 축이라도 돌면 지름길을 타지 않는다 — 요만 준 회전이 곱 셋과 같은 값인지로 본다.
    const sw::float3   yawOnly{ 0.0f, 0.8f, 0.0f };
    const sw::float3   scale{ 2.0f, 0.5f, 3.25f };
    const sw::float4x4 expectedYaw = sw::float4x4::createScale( scale ) * sw::float4x4::createFromYawPitchRoll( yawOnly._y, yawOnly._x, yawOnly._z ) *
                                     sw::float4x4::createTranslation( position );
    const sw::float4x4 actualYaw  = sw::float4x4::createTrs( position, yawOnly, scale );
    const float32*     pExpected  = &expectedYaw._11;
    const float32*     pActualYaw = &actualYaw._11;
    for ( int32 elementIndex = 0; elementIndex < 16; ++elementIndex )
        SW_EXPECT_NEAR_EQUAL( pExpected[elementIndex], pActualYaw[elementIndex], 1e-5f );
}
