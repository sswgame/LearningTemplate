#include "pch.h"

#include "Core/Math/VectorMath.h"

#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
    const float2 float2::Zero{ 0.f, 0.f };
    const float2 float2::UnitX{ 1.f, 0.f };
    const float2 float2::UnitY{ 0.f, 1.f };
    const float2 float2::UnitScale{ 1.f, 1.f };

    float2 float2::transform( const float2& v, const quaternion& rotation ) noexcept
    {
        const float3 v3{ v._x, v._y, 0.f };
        const float3 result = float3::transform( v3, rotation );
        return float2{ result._x, result._y };
    }

    float2 float2::transform( const float2& v, const float4x4& matrix ) noexcept
    {
        const float3 v3{ v._x, v._y, 0.f };
        const float3 result = float3::transform( v3, matrix );
        return float2{ result._x, result._y };
    }

    float2 float2::transformVector( const float2& v, const float4x4& matrix ) noexcept
    {
        const float3 v3{ v._x, v._y, 0.f };
        const float3 result = float3::transformVector( v3, matrix );
        return float2{ result._x, result._y };
    }

    const float3 float3::Zero{ 0.f, 0.f, 0.f };
    const float3 float3::UnitX{ 1.f, 0.f, 0.f };
    const float3 float3::UnitY{ 0.f, 1.f, 0.f };
    const float3 float3::UnitZ{ 0.f, 0.f, 1.f };
    const float3 float3::UnitScale{ 1.f, 1.f, 1.f };
    const float3 float3::Up{ 0.f, 1.f, 0.f };
    const float3 float3::Down{ 0.f, -1.f, 0.f };
    const float3 float3::Right{ 1.f, 0.f, 0.f };
    const float3 float3::Left{ -1.f, 0.f, 0.f };
    const float3 float3::Forward{ 0.f, 0.f, 1.f };
    const float3 float3::Backward{ 0.f, 0.f, -1.f };

    float3 float3::transform( const float3& position, const quaternion& rotation ) noexcept
    {
        float4x4 rotMat = rotation.toMatrix();
        return transform( position, rotMat );
    }

    float3 float3::transform( const float3& position, const float4x4& matrix ) noexcept
    {
        return float3{
            position._x * matrix._11 + position._y * matrix._21 + position._z * matrix._31 + matrix._41,
            position._x * matrix._12 + position._y * matrix._22 + position._z * matrix._32 + matrix._42,
            position._x * matrix._13 + position._y * matrix._23 + position._z * matrix._33 + matrix._43 };
    }

    float3 float3::transformVector( const float3& normal, const float4x4& matrix ) noexcept
    {
        return float3{
            normal._x * matrix._11 + normal._y * matrix._21 + normal._z * matrix._31,
            normal._x * matrix._12 + normal._y * matrix._22 + normal._z * matrix._32,
            normal._x * matrix._13 + normal._y * matrix._23 + normal._z * matrix._33 };
    }

    float32 float3::getAngleBetween( const float3& other ) const noexcept
    {
        const float32 lengthSquaredA = getLengthSquared();
        const float32 lengthSquaredB = other.getLengthSquared();

        SW_LOW_LEVEL_ASSERT( MathUtil::nearEqual( lengthSquaredA, 0.f ) == false, "Length is 0" );
        SW_LOW_LEVEL_ASSERT( MathUtil::nearEqual( lengthSquaredB, 0.f ) == false, "Length is 0" );

        const float32 dotProduct   = dot( other );
        const bool    isUnitLength = MathUtil::nearEqual( lengthSquaredA, 1.f ) && MathUtil::nearEqual( lengthSquaredB, 1.f );
        if ( isUnitLength )
            return DeterministicMathUtil::acos( MathUtil::clamp( dotProduct, -1.0f, 1.0f ) );

        const float32 length = MathUtil::sqrt( lengthSquaredA ) * MathUtil::sqrt( lengthSquaredB );
        if ( length <= MathUtil::kEpsilon )
            return 0.0f;
        return DeterministicMathUtil::acos( MathUtil::clamp( dotProduct / length, -1.0f, 1.0f ) );
    }

    const float4 float4::Zero{ 0.f, 0.f, 0.f, 0.f };
    const float4 float4::UnitX{ 1.f, 0.f, 0.f, 0.f };
    const float4 float4::UnitY{ 0.f, 1.f, 0.f, 0.f };
    const float4 float4::UnitZ{ 0.f, 0.f, 1.f, 0.f };
    const float4 float4::UnitW{ 0.f, 0.f, 0.f, 1.f };
    const float4 float4::UnitScale{ 1.f, 1.f, 1.f, 1.f };

    float4 float4::transform( const float4& v, const quaternion& rotation ) noexcept
    {
        const float4x4 rotMat = rotation.toMatrix();
        return transform( v, rotMat );
    }

    float4 float4::transform( const float4& v, const float4x4& matrix ) noexcept
    {
        return float4{
            v._x * matrix._11 + v._y * matrix._21 + v._z * matrix._31 + v._w * matrix._41,
            v._x * matrix._12 + v._y * matrix._22 + v._z * matrix._32 + v._w * matrix._42,
            v._x * matrix._13 + v._y * matrix._23 + v._z * matrix._33 + v._w * matrix._43,
            v._x * matrix._14 + v._y * matrix._24 + v._z * matrix._34 + v._w * matrix._44 };
    }

    float4 float4::transformVector( const float4& v, const float4x4& matrix ) noexcept
    {
        return float4{
            v._x * matrix._11 + v._y * matrix._21 + v._z * matrix._31,
            v._x * matrix._12 + v._y * matrix._22 + v._z * matrix._32,
            v._x * matrix._13 + v._y * matrix._23 + v._z * matrix._33,
            0.0f };
    }

    const double3 double3::Zero{ 0.0, 0.0, 0.0 };
    const double3 double3::UnitX{ 1.0, 0.0, 0.0 };
    const double3 double3::UnitY{ 0.0, 1.0, 0.0 };
    const double3 double3::UnitZ{ 0.0, 0.0, 1.0 };
    const double3 double3::UnitScale{ 1.0, 1.0, 1.0 };

} // namespace sw
