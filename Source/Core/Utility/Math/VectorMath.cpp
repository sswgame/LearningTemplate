/**
 * @file VectorMath.cpp
 * @brief 벡터 수학 구현
 */
#include "pch.h"
#include "Core/Utility/Math/VectorMath.h"
#include "Core/Utility/Math/MatrixMath.h"
#include "Core/Utility/Math/MathUtil.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{

	const float2 float2::Zero{ 0.f, 0.f };
	const float2 float2::UnitX{ 1.f, 0.f };
	const float2 float2::UnitY{ 0.f, 1.f };
	const float2 float2::UnitScale{ 1.f, 1.f };

	float32 float2::getDistance( const float2& from, const float2& to ) noexcept
	{
		return sqrtf( getDistanceSquared( from, to ) );
	}

	float32 float2::getDistanceSquared( const float2& from, const float2& to ) noexcept
	{
		const float2 diff = to - from;
		return diff.getLengthSquared();
	}

	float2 float2::min( const float2& lhs, const float2& rhs ) noexcept
	{
		return float2{ std::min( lhs._x, rhs._x ), std::min( lhs._y, rhs._y ) };
	}

	float2 float2::max( const float2& lhs, const float2& rhs ) noexcept
	{
		return float2{ std::max( lhs._x, rhs._x ), std::max( lhs._y, rhs._y ) };
	}

	float2 float2::lerp( const float2& from, const float2& to, float32 t ) noexcept
	{
		return float2{ MathUtil::lerp( from._x, to._x, t ), MathUtil::lerp( from._y, to._y, t ) };
	}

	float2 float2::smoothStep( const float2& from, const float2& to, float32 t ) noexcept
	{
		float32 s = MathUtil::smoothstep( 0.0f, 1.0f, t );
		return float2{ MathUtil::lerp( from._x, to._x, s ), MathUtil::lerp( from._y, to._y, s ) };
	}

	float2 float2::barycentric( const float2& v1, const float2& v2, const float2& v3, float32 f, float32 g ) noexcept
	{
		return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
	}

	float2 float2::catmullRom( const float2& v1, const float2& v2, const float2& v3, const float2& v4, float32 t ) noexcept
	{
		return float2{ MathUtil::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), MathUtil::catmullRom( v1._y, v2._y, v3._y, v4._y, t ) };
	}

	float2 float2::hermite( const float2& p1, const float2& slope1, const float2& p2, const float2& slope2, float32 t ) noexcept
	{
		return float2{ MathUtil::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), MathUtil::hermite( p1._y, slope1._y, p2._y, slope2._y, t ) };
	}

	float2 float2::reflect( const float2& source, const float2& normal ) noexcept
	{
		const float2 n = normal.normalize();
		return source - ( 2.f * source.dot( n ) ) * n;
	}

	float2 float2::transform( const float2& v, const quaternion& rotation ) noexcept
	{
		const float3 v3{ v._x, v._y, 0.f };
		const float3 res = float3::transform( v3, rotation );
		return float2{ res._x, res._y };
	}

	float2 float2::transform( const float2& v, const float4x4& matrix ) noexcept
	{
		const float3 v3{ v._x, v._y, 0.f };
		const float3 res = float3::transform( v3, matrix );
		return float2{ res._x, res._y };
	}

	float2 float2::transformNormal( const float2& v, const float4x4& matrix ) noexcept
	{
		const float3 v3{ v._x, v._y, 0.f };
		const float3 res = float3::transformNormal( v3, matrix );
		return float2{ res._x, res._y };
	}

	bool float2::inBounds( const float2& bound ) const noexcept
	{
		return ( _x <= bound._x && _x >= -bound._x ) && ( _y <= bound._y && _y >= -bound._y );
	}

	bool float2::isInfinite() const noexcept
	{
		return std::isinf( _x ) || std::isinf( _y );
	}

	float32 float2::dot( const float2& other ) const noexcept
	{
		return ( _x * other._x ) + ( _y * other._y );
	}

	float2& float2::normalize() noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			( *this ) *= invLen;
		else
			( *this ) = Zero;
		return *this;
	}

	float2 float2::normalize() const noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			return ( *this ) * invLen;
		return Zero;
	}

	void float2::clamp( const float2& minValue, const float2& maxValue ) noexcept
	{
		_x = MathUtil::clamp( _x, minValue._x, maxValue._x );
		_y = MathUtil::clamp( _y, minValue._y, maxValue._y );
	}

	float2 float2::clamp( const float2& minValue, const float2& maxValue ) const noexcept
	{
		float2 result = *this;
		result.clamp( minValue, maxValue );
		return result;
	}

	float32 float2::getLength() const noexcept
	{
		return sqrtf( getLengthSquared() );
	}

	float32 float2::getLengthSquared() const noexcept
	{
		return ( _x * _x ) + ( _y * _y );
	}

	bool float2::operator==( const float2& other ) const noexcept
	{
		return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y );
	}

	bool float2::operator!=( const float2& other ) const noexcept
	{
		return !( *this == other );
	}

	float2& float2::operator+=( const float2& other ) noexcept
	{
		_x += other._x;
		_y += other._y;
		return *this;
	}

	float2& float2::operator-=( const float2& other ) noexcept
	{
		_x -= other._x;
		_y -= other._y;
		return *this;
	}

	float2& float2::operator*=( float32 scale ) noexcept
	{
		_x *= scale;
		_y *= scale;
		return *this;
	}

	float2& float2::operator/=( float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		_x *= inv;
		_y *= inv;
		return *this;
	}

	float2 float2::operator-() const noexcept
	{
		return float2{ -_x, -_y };
	}

	float2 operator+( const float2& lhs, const float2& rhs ) noexcept
	{
		return float2{ lhs._x + rhs._x, lhs._y + rhs._y };
	}

	float2 operator-( const float2& lhs, const float2& rhs ) noexcept
	{
		return float2{ lhs._x - rhs._x, lhs._y - rhs._y };
	}

	float2 operator*( const float2& v, float32 scale ) noexcept
	{
		return float2{ v._x * scale, v._y * scale };
	}

	float2 operator/( const float2& v, float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		return float2{ v._x * inv, v._y * inv };
	}

	float2 operator*( float32 scale, const float2& v ) noexcept
	{
		return float2{ v._x * scale, v._y * scale };
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

	float3 float3::transformNormal( const float3& normal, const float4x4& matrix ) noexcept
	{
		return float3{
			normal._x * matrix._11 + normal._y * matrix._21 + normal._z * matrix._31,
			normal._x * matrix._12 + normal._y * matrix._22 + normal._z * matrix._32,
			normal._x * matrix._13 + normal._y * matrix._23 + normal._z * matrix._33 };
	}

	bool float3::inBounds( const float3& bound ) const noexcept
	{
		return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y ) && ( -bound._z <= _z && _z <= bound._z );
	}

	bool float3::isInfinite() const noexcept
	{
		return std::isinf( _x ) || std::isinf( _y ) || std::isinf( _z );
	}

	float32 float3::dot( const float3& other ) const noexcept
	{
		return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z );
	}

	float3 float3::cross( const float3& other ) const noexcept
	{
		float3 result{};
		result._x = ( _y * other._z - _z * other._y );
		result._y = ( _z * other._x - _x * other._z );
		result._z = ( _x * other._y - _y * other._x );
		return result;
	}

	float3& float3::normalize() noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			( *this ) *= invLen;
		else
			( *this ) = Zero;
		return *this;
	}

	float3 float3::normalize() const noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			return ( *this ) * invLen;
		return Zero;
	}

	void float3::clamp( const float3& minValue, const float3& maxValue ) noexcept
	{
		_x = MathUtil::clamp( _x, minValue._x, maxValue._x );
		_y = MathUtil::clamp( _y, minValue._y, maxValue._y );
		_z = MathUtil::clamp( _z, minValue._z, maxValue._z );
	}

	float3 float3::clamp( const float3& minValue, const float3& maxValue ) const noexcept
	{
		float3 result = *this;
		result.clamp( minValue, maxValue );
		return result;
	}

	float32 float3::getLength() const noexcept
	{
		return sqrtf( getLengthSquared() );
	}

	float32 float3::getLengthSquared() const noexcept
	{
		return ( _x * _x ) + ( _y * _y ) + ( _z * _z );
	}

	float32 float3::getAngleBetween( const float3& other ) const noexcept
	{
		const float32 lengthSquaredA = getLengthSquared();
		const float32 lengthSquaredB = other.getLengthSquared();

		SW_LOG_ASSERT( MathUtil::nearEqual( lengthSquaredA, 0.f ) == false, "Length is 0" );
		SW_LOG_ASSERT( MathUtil::nearEqual( lengthSquaredB, 0.f ) == false, "Length is 0" );

		const float32 dotProduct   = dot( other );
		const bool	  isUnitLength = MathUtil::nearEqual( lengthSquaredA, 1.f ) && MathUtil::nearEqual( lengthSquaredB, 1.f );
		if ( isUnitLength )
			return acosf( dotProduct );

		const float32 length = sqrtf( lengthSquaredA ) * sqrtf( lengthSquaredB );
		return acosf( dotProduct / length );
	}

	float32 float3::getDistance( const float3& from, const float3& to ) noexcept
	{
		return sqrtf( getDistanceSquared( from, to ) );
	}

	float32 float3::getDistanceSquared( const float3& from, const float3& to ) noexcept
	{
		const float3 result = to - from;
		return result.getLengthSquared();
	}

	float3 float3::min( const float3& lhs, const float3& rhs ) noexcept
	{
		return float3{ std::min( lhs._x, rhs._x ), std::min( lhs._y, rhs._y ), std::min( lhs._z, rhs._z ) };
	}

	float3 float3::max( const float3& lhs, const float3& rhs ) noexcept
	{
		return float3{ std::max( lhs._x, rhs._x ), std::max( lhs._y, rhs._y ), std::max( lhs._z, rhs._z ) };
	}

	float3 float3::lerp( const float3& from, const float3& to, float32 t ) noexcept
	{
		return float3{ MathUtil::lerp( from._x, to._x, t ), MathUtil::lerp( from._y, to._y, t ), MathUtil::lerp( from._z, to._z, t ) };
	}

	float3 float3::smoothStep( const float3& from, const float3& to, float32 t ) noexcept
	{
		float32 s = MathUtil::smoothstep( 0.0f, 1.0f, t );
		return float3{ MathUtil::lerp( from._x, to._x, s ), MathUtil::lerp( from._y, to._y, s ), MathUtil::lerp( from._z, to._z, s ) };
	}

	float3 float3::barycentric( const float3& v1, const float3& v2, const float3& v3, float32 f, float32 g ) noexcept
	{
		return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
	}

	float3 float3::catmullRom( const float3& v1, const float3& v2, const float3& v3, const float3& v4, float32 t ) noexcept
	{
		return float3{ MathUtil::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), MathUtil::catmullRom( v1._y, v2._y, v3._y, v4._y, t ), MathUtil::catmullRom( v1._z, v2._z, v3._z, v4._z, t ) };
	}

	float3 float3::hermite( const float3& p1, const float3& slope1, const float3& p2, const float3& slope2, float32 t ) noexcept
	{
		return float3{ MathUtil::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), MathUtil::hermite( p1._y, slope1._y, p2._y, slope2._y, t ), MathUtil::hermite( p1._z, slope1._z, p2._z, slope2._z, t ) };
	}

	float3 float3::reflect( const float3& source, const float3& normal ) noexcept
	{
		const float3  direction	 = normal.normalize();
		const float32 dotProduct = source.dot( direction );
		return source - ( 2.f * dotProduct ) * direction;
	}

	float3 float3::project( const float3& from, const float3& to ) noexcept
	{
		const float3& direction	 = to.normalize();
		const float32 dotProduct = from.dot( direction );
		return direction * dotProduct;
	}

	float3 float3::perpedicular( const float3& from, const float3& to ) noexcept
	{
		return from - project( from, to );
	}

	bool float3::operator==( const float3& other ) const noexcept
	{
		return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z );
	}

	bool float3::operator!=( const float3& other ) const noexcept
	{
		return !( *this == other );
	}

	float3& float3::operator+=( const float3& other ) noexcept
	{
		_x += other._x;
		_y += other._y;
		_z += other._z;
		return *this;
	}

	float3& float3::operator-=( const float3& other ) noexcept
	{
		_x -= other._x;
		_y -= other._y;
		_z -= other._z;
		return *this;
	}

	float3& float3::operator*=( float32 scale ) noexcept
	{
		_x *= scale;
		_y *= scale;
		_z *= scale;
		return *this;
	}

	float3& float3::operator/=( float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		_x *= inv;
		_y *= inv;
		_z *= inv;
		return *this;
	}

	float3 float3::operator-() const noexcept
	{
		return float3{ -_x, -_y, -_z };
	}

	float3 operator+( const float3& lhs, const float3& rhs ) noexcept
	{
		return float3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z };
	}

	float3 operator-( const float3& lhs, const float3& rhs ) noexcept
	{
		return float3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z };
	}

	float3 operator*( const float3& v, float32 scale ) noexcept
	{
		return float3{ v._x * scale, v._y * scale, v._z * scale };
	}

	float3 operator/( const float3& v, float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		return float3{ v._x * inv, v._y * inv, v._z * inv };
	}

	float3 operator*( float32 scale, const float3& v ) noexcept
	{
		return float3{ v._x * scale, v._y * scale, v._z * scale };
	}

	const float4 float4::Zero{ 0.f, 0.f, 0.f, 0.f };
	const float4 float4::UnitX{ 1.f, 0.f, 0.f, 0.f };
	const float4 float4::UnitY{ 0.f, 1.f, 0.f, 0.f };
	const float4 float4::UnitZ{ 0.f, 0.f, 1.f, 0.f };
	const float4 float4::UnitW{ 0.f, 0.f, 0.f, 1.f };
	const float4 float4::UnitScale{ 1.f, 1.f, 1.f, 1.f };

	float32 float4::dot( const float4& other ) const noexcept
	{
		return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z ) + ( _w * other._w );
	}

	float4& float4::normalize() noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			( *this ) *= invLen;
		else
			( *this ) = Zero;
		return *this;
	}

	float4 float4::normalize() const noexcept
	{
		const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
		if ( invLen > 0.f )
			return ( *this ) * invLen;
		return Zero;
	}

	bool float4::inBounds( const float4& bound ) const noexcept
	{
		return ( _x <= bound._x && _x >= -bound._x ) && ( _y <= bound._y && _y >= -bound._y ) && ( _z <= bound._z && _z >= -bound._z ) && ( _w <= bound._w && _w >= -bound._w );
	}

	void float4::clamp( const float4& minValue, const float4& maxValue ) noexcept
	{
		_x = MathUtil::clamp( _x, minValue._x, maxValue._x );
		_y = MathUtil::clamp( _y, minValue._y, maxValue._y );
		_z = MathUtil::clamp( _z, minValue._z, maxValue._z );
		_w = MathUtil::clamp( _w, minValue._w, maxValue._w );
	}

	float4 float4::clamp( const float4& minValue, const float4& maxValue ) const noexcept
	{
		float4 result = *this;
		result.clamp( minValue, maxValue );
		return result;
	}

	float32 float4::getLength() const noexcept
	{
		return sqrtf( getLengthSquared() );
	}

	float32 float4::getLengthSquared() const noexcept
	{
		return ( _x * _x ) + ( _y * _y ) + ( _z * _z ) + ( _w * _w );
	}

	float32 float4::getDistance( const float4& from, const float4& to ) noexcept
	{
		return sqrtf( getDistanceSquared( from, to ) );
	}

	float32 float4::getDistanceSquared( const float4& from, const float4& to ) noexcept
	{
		const float4 result = to - from;
		return result.getLengthSquared();
	}

	float4 float4::min( const float4& lhs, const float4& rhs ) noexcept
	{
		return float4{ std::min( lhs._x, rhs._x ), std::min( lhs._y, rhs._y ), std::min( lhs._z, rhs._z ), std::min( lhs._w, rhs._w ) };
	}

	float4 float4::max( const float4& lhs, const float4& rhs ) noexcept
	{
		return float4{ std::max( lhs._x, rhs._x ), std::max( lhs._y, rhs._y ), std::max( lhs._z, rhs._z ), std::max( lhs._w, rhs._w ) };
	}

	float4 float4::lerp( const float4& from, const float4& to, float32 ratio ) noexcept
	{
		return float4{ MathUtil::lerp( from._x, to._x, ratio ), MathUtil::lerp( from._y, to._y, ratio ), MathUtil::lerp( from._z, to._z, ratio ), MathUtil::lerp( from._w, to._w, ratio ) };
	}

	float4 float4::smoothStep( const float4& from, const float4& to, float32 ratio ) noexcept
	{
		float32 s = MathUtil::smoothstep( 0.0f, 1.0f, ratio );
		return float4{ MathUtil::lerp( from._x, to._x, s ), MathUtil::lerp( from._y, to._y, s ), MathUtil::lerp( from._z, to._z, s ), MathUtil::lerp( from._w, to._w, s ) };
	}

	float4 float4::barycentric( const float4& v1, const float4& v2, const float4& v3, float32 f, float32 g ) noexcept
	{
		return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
	}

	float4 float4::catmullRom( const float4& v1, const float4& v2, const float4& v3, const float4& v4, float32 t ) noexcept
	{
		return float4{ MathUtil::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), MathUtil::catmullRom( v1._y, v2._y, v3._y, v4._y, t ), MathUtil::catmullRom( v1._z, v2._z, v3._z, v4._z, t ), MathUtil::catmullRom( v1._w, v2._w, v3._w, v4._w, t ) };
	}

	float4 float4::hermite( const float4& p1, const float4& slope1, const float4& p2, const float4& slope2, float32 t ) noexcept
	{
		return float4{ MathUtil::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), MathUtil::hermite( p1._y, slope1._y, p2._y, slope2._y, t ), MathUtil::hermite( p1._z, slope1._z, p2._z, slope2._z, t ), MathUtil::hermite( p1._w, slope1._w, p2._w, slope2._w, t ) };
	}

	float4 float4::reflect( const float4& source, const float4& normal ) noexcept
	{
		const float4  direction	 = normal.normalize();
		const float32 dotProduct = source.dot( direction );
		return source - ( 2.f * dotProduct ) * direction;
	}

	bool float4::operator==( const float4& other ) const noexcept
	{
		return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z ) && MathUtil::nearEqual( _w, other._w );
	}

	bool float4::operator!=( const float4& other ) const noexcept
	{
		return !( *this == other );
	}

	float4& float4::operator+=( const float4& other ) noexcept
	{
		_x += other._x;
		_y += other._y;
		_z += other._z;
		_w += other._w;
		return *this;
	}

	float4& float4::operator-=( const float4& other ) noexcept
	{
		_x -= other._x;
		_y -= other._y;
		_z -= other._z;
		_w -= other._w;
		return *this;
	}

	float4& float4::operator*=( float32 scale ) noexcept
	{
		_x *= scale;
		_y *= scale;
		_z *= scale;
		_w *= scale;
		return *this;
	}

	float4& float4::operator/=( float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		_x *= inv;
		_y *= inv;
		_z *= inv;
		_w *= inv;
		return *this;
	}

	float4 float4::operator-() const noexcept
	{
		return float4{ -_x, -_y, -_z, -_w };
	}

	float4 operator+( const float4& lhs, const float4& rhs ) noexcept
	{
		return float4{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z, lhs._w + rhs._w };
	}

	float4 operator-( const float4& lhs, const float4& rhs ) noexcept
	{
		return float4{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z, lhs._w - rhs._w };
	}

	float4 operator*( const float4& v, float32 scale ) noexcept
	{
		return float4{ v._x * scale, v._y * scale, v._z * scale, v._w * scale };
	}

	float4 operator/( const float4& v, float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		return float4{ v._x * inv, v._y * inv, v._z * inv, v._w * inv };
	}

	float4 operator*( float32 scale, const float4& v ) noexcept
	{
		return float4{ v._x * scale, v._y * scale, v._z * scale, v._w * scale };
	}

	const double3 double3::Zero{ 0.0, 0.0, 0.0 };
	const double3 double3::UnitX{ 1.0, 0.0, 0.0 };
	const double3 double3::UnitY{ 0.0, 1.0, 0.0 };
	const double3 double3::UnitZ{ 0.0, 0.0, 1.0 };
	const double3 double3::UnitScale{ 1.0, 1.0, 1.0 };

	float64 double3::getDistance( const double3& from, const double3& to ) noexcept
	{
		return sqrt( getDistanceSquared( from, to ) );
	}

	float64 double3::getDistanceSquared( const double3& from, const double3& to ) noexcept
	{
		const double3 result = to - from;
		return result.getLengthSquared();
	}

	double3 double3::min( const double3& lhs, const double3& rhs ) noexcept
	{
		return double3{ std::min( lhs._x, rhs._x ), std::min( lhs._y, rhs._y ), std::min( lhs._z, rhs._z ) };
	}

	double3 double3::max( const double3& lhs, const double3& rhs ) noexcept
	{
		return double3{ std::max( lhs._x, rhs._x ), std::max( lhs._y, rhs._y ), std::max( lhs._z, rhs._z ) };
	}

	double3 double3::lerp( const double3& from, const double3& to, float64 t ) noexcept
	{
		return double3{ MathUtil::lerp( from._x, to._x, t ), MathUtil::lerp( from._y, to._y, t ), MathUtil::lerp( from._z, to._z, t ) };
	}

	bool double3::inBounds( const double3& bound ) const noexcept
	{
		return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y ) && ( -bound._z <= _z && _z <= bound._z );
	}

	bool double3::isInfinite() const noexcept
	{
		return std::isinf( _x ) || std::isinf( _y ) || std::isinf( _z );
	}

	float64 double3::dot( const double3& other ) const noexcept
	{
		return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z );
	}

	double3 double3::cross( const double3& other ) const noexcept
	{
		double3 result{};
		result._x = ( _y * other._z - _z * other._y );
		result._y = ( _z * other._x - _x * other._z );
		result._z = ( _x * other._y - _y * other._x );
		return result;
	}

	double3& double3::normalize() noexcept
	{
		const float64 lenSq = getLengthSquared();
		if ( lenSq > 0.0 )
		{
			const float64 invLen = 1.0 / sqrt( lenSq );
			( *this ) *= invLen;
		}
		else
		{
			( *this ) = Zero;
		}
		return *this;
	}

	double3 double3::normalize() const noexcept
	{
		const float64 lenSq = getLengthSquared();
		if ( lenSq > 0.0 )
		{
			const float64 invLen = 1.0 / sqrt( lenSq );
			return ( *this ) * invLen;
		}
		return Zero;
	}

	void double3::clamp( const double3& minValue, const double3& maxValue ) noexcept
	{
		_x = MathUtil::clamp( _x, minValue._x, maxValue._x );
		_y = MathUtil::clamp( _y, minValue._y, maxValue._y );
		_z = MathUtil::clamp( _z, minValue._z, maxValue._z );
	}

	double3 double3::clamp( const double3& minValue, const double3& maxValue ) const noexcept
	{
		double3 result = *this;
		result.clamp( minValue, maxValue );
		return result;
	}

	float64 double3::getLength() const noexcept
	{
		return sqrt( getLengthSquared() );
	}

	float64 double3::getLengthSquared() const noexcept
	{
		return ( _x * _x ) + ( _y * _y ) + ( _z * _z );
	}

	bool double3::operator==( const double3& other ) const noexcept
	{
		return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z );
	}

	bool double3::operator!=( const double3& other ) const noexcept
	{
		return !( *this == other );
	}

	double3& double3::operator+=( const double3& other ) noexcept
	{
		_x += other._x;
		_y += other._y;
		_z += other._z;
		return *this;
	}

	double3& double3::operator-=( const double3& other ) noexcept
	{
		_x -= other._x;
		_y -= other._y;
		_z -= other._z;
		return *this;
	}

	double3& double3::operator*=( float64 scale ) noexcept
	{
		_x *= scale;
		_y *= scale;
		_z *= scale;
		return *this;
	}

	double3& double3::operator/=( float64 scale ) noexcept
	{
		const float64 inv = 1.0 / scale;
		_x *= inv;
		_y *= inv;
		_z *= inv;
		return *this;
	}

	double3 double3::operator-() const noexcept
	{
		return double3{ -_x, -_y, -_z };
	}

	double3 operator+( const double3& lhs, const double3& rhs ) noexcept
	{
		return double3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z };
	}

	double3 operator-( const double3& lhs, const double3& rhs ) noexcept
	{
		return double3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z };
	}

	double3 operator*( const double3& v, float64 scale ) noexcept
	{
		return double3{ v._x * scale, v._y * scale, v._z * scale };
	}

	double3 operator/( const double3& v, float64 scale ) noexcept
	{
		const float64 inv = 1.0 / scale;
		return double3{ v._x * inv, v._y * inv, v._z * inv };
	}

	double3 operator*( float64 scale, const double3& v ) noexcept
	{
		return double3{ v._x * scale, v._y * scale, v._z * scale };
	}
} // namespace sw
