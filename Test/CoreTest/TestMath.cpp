#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_Math — float2/3/4·행렬·쿼터니언·MathUtil
// ------------------------------------------------------------------------------
/**
 * @brief [Core_Math] float2 전체
 */

SW_TEST_CASE( Core_Math, Float2FullTest )
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

	float32	   arr[2] = { 1.0f, 2.0f };
	sw::float2 vArr( arr );
	SW_EXPECT_NEAR_EQUAL( 1.0f, vArr._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 2.0f, vArr._y, 1e-4f );

	SW_EXPECT_NEAR_EQUAL( 5.0f, vComp.getLength(), 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 25.0f, vComp.getLengthSquared(), 1e-4f );

	const sw::float2 constVComp	 = vComp;
	sw::float2		 vNormalized = constVComp.normalize();
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
 * @brief [Core_Math] float3 전체
 */
SW_TEST_CASE( Core_Math, Float3FullTest )
{

	SW_EXPECT_NEAR_EQUAL( 0.0f, sw::float3::Zero._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitX._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitY._y, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, sw::float3::UnitZ._z, 1e-4f );

	sw::float3 v3( 1.0f, 2.0f, 2.0f );
	SW_EXPECT_NEAR_EQUAL( 3.0f, v3.getLength(), 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 9.0f, v3.getLengthSquared(), 1e-4f );

	const sw::float3 constV3 = v3;
	sw::float3		 vNorm	 = constV3.normalize();
	SW_EXPECT_NEAR_EQUAL( 1.0f / 3.0f, vNorm._x, 1e-4f );

	sw::float3 v1( 1.0f, 0.0f, 0.0f );
	sw::float3 v2( 0.0f, 1.0f, 0.0f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, v1.dot( v2 ), 1e-4f );

	sw::float3 crossV = v1.cross( v2 );
	SW_EXPECT_NEAR_EQUAL( 0.0f, crossV._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, crossV._y, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, crossV._z, 1e-4f );

	SW_EXPECT_TRUE( v1.inBounds( sw::float3( 2.0f ) ) );

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
 * @brief [Core_Math] float4 전체
 */
SW_TEST_CASE( Core_Math, Float4FullTest )
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
	sw::float4		 vNorm		= constVComp.normalize();
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
 * @brief [Core_Math] float4x4 전체
 */
SW_TEST_CASE( Core_Math, Float4x4FullTest )
{

	sw::float4x4 identity = sw::float4x4::Identity;
	SW_EXPECT_NEAR_EQUAL( 1.0f, identity._11, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, identity._12, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, identity.determinant(), 1e-4f );

	sw::float4x4 transM = sw::float4x4::createTranslation( 10.0f, 20.0f, 30.0f );
	sw::float3	 pos	= transM.getTranslation();
	SW_EXPECT_NEAR_EQUAL( 10.0f, pos._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 20.0f, pos._y, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 30.0f, pos._z, 1e-4f );

	sw::float4x4 scaleM = sw::float4x4::createScale( 2.0f, 3.0f, 4.0f );
	sw::float3	 scale	= scaleM.getScale();
	SW_EXPECT_NEAR_EQUAL( 2.0f, scale._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 3.0f, scale._y, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 4.0f, scale._z, 1e-4f );

	const sw::float4x4 constTransM = transM;
	sw::float4x4	   transResult = constTransM.transpose();
	SW_EXPECT_NEAR_EQUAL( 10.0f, transResult._14, 1e-4f );

	sw::float4x4 invM		   = constTransM.invert();
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
 * @brief [Core_Math] Quaternion 전체
 */
SW_TEST_CASE( Core_Math, QuaternionFullTest )
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
	sw::quaternion		 invQ	   = constQRot.inverse();
	sw::quaternion		 resultQ   = qRot * invQ;
	SW_EXPECT_NEAR_EQUAL( 0.0f, resultQ._x, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, resultQ._w, 1e-4f );

	sw::quaternion slerpQ = sw::quaternion::slerp( identity, qRot, 0.5f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, slerpQ.norm(), 1e-4f );

	sw::quaternion qYawPitchRoll = sw::quaternion::createFromYawPitchRoll( sw::MathUtil::toRadian( 45.0f ), 0.0f, 0.0f );
	sw::float3	   euler		 = qYawPitchRoll.toEuler();
	SW_EXPECT_NEAR_EQUAL( sw::MathUtil::toRadian( 45.0f ), euler._y, 1e-3f );
}

/**
 * @brief [Core_Math] MathUtil 함수 전체
 */
SW_TEST_CASE( Core_Math, MathUtilFunctionsFull )
{
	float32 clamped = sw::MathUtil::clamp( 15.0f, 0.0f, 10.0f );
	SW_EXPECT_NEAR_EQUAL( 10.0f, clamped, 1e-4f );

	float32 saturated = sw::MathUtil::saturate( 1.5f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, saturated, 1e-4f );

	float32 lerped = sw::MathUtil::lerp( 0.0f, 100.0f, 0.5f );
	SW_EXPECT_NEAR_EQUAL( 50.0f, lerped, 1e-4f );

	float32 invLerp = sw::MathUtil::inverse_lerp( 10.0f, 20.0f, 15.0f );
	SW_EXPECT_NEAR_EQUAL( 0.5f, invLerp, 1e-4f );

	float32 smoothstepVal = sw::MathUtil::smoothstep( 0.0f, 10.0f, 5.0f );
	SW_EXPECT_NEAR_EQUAL( 0.5f, smoothstepVal, 1e-4f );

	float32 sq = sw::MathUtil::sqaure( 4.0f );
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
	SW_EXPECT_TRUE( rndVal >= 1.0f && rndVal <= 5.0f );
}

/**
 * @brief [Core_Math] float4x4 TRS 합성, 전치, 역행렬 및 벡터 변환 검증
 */
SW_TEST_CASE( Core_Math, Matrix4x4TRSAndInversion )
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
	const sw::float4x4 combined	   = scale * trans;
	const sw::float4x4 invCombined = combined.invert();
	const sw::float4x4 identity	   = combined * invCombined;

	SW_EXPECT_NEAR_EQUAL( 1.0f, identity._11, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, identity._22, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, identity._33, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, identity._44, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, identity._12, 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.0f, identity._14, 1e-3f );
}
