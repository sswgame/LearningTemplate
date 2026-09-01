#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Core/Math/MathUtil.h"

namespace sw
{

	const quaternion quaternion::Identity{ 0.f, 0.f, 0.f, 1.f };

	quaternion quaternion::createFromAxisAngle( const float3& axis, float32 angle ) noexcept
	{
		const float3 normalizedAxis = axis.normalize();
		const float3 halfAngle		= normalizedAxis * MathUtil::sin( angle * 0.5f );
		return quaternion{ halfAngle._x, halfAngle._y, halfAngle._z, MathUtil::cos( angle * 0.5f ) };
	}

	quaternion quaternion::createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept
	{
		const float32 halfYaw	= yaw * 0.5f;
		const float32 sinYaw	= MathUtil::sin( halfYaw );
		const float32 cosYaw	= MathUtil::cos( halfYaw );
		const float32 halfPitch = pitch * 0.5f;
		const float32 sinPitch	= MathUtil::sin( halfPitch );
		const float32 cosPitch	= MathUtil::cos( halfPitch );
		const float32 halfRoll	= roll * 0.5f;
		const float32 sinRoll	= MathUtil::sin( halfRoll );
		const float32 cosRoll	= MathUtil::cos( halfRoll );

		return quaternion{
			( cosYaw * sinPitch * cosRoll ) + ( sinYaw * cosPitch * sinRoll ),
			( sinYaw * cosPitch * cosRoll ) - ( cosYaw * sinPitch * sinRoll ),
			( cosYaw * cosPitch * sinRoll ) - ( sinYaw * sinPitch * cosRoll ),
			( cosYaw * cosPitch * cosRoll ) + ( sinYaw * sinPitch * sinRoll ) };
	}

	quaternion quaternion::createFromYawPitchRoll( const float3& angles ) noexcept
	{
		return createFromYawPitchRoll( angles._y, angles._x, angles._z );
	}

	quaternion quaternion::createFromRotationMatrix( const float4x4& matrix ) noexcept
	{
		const float32 trace = matrix._11 + matrix._22 + matrix._33;
		if ( trace > 0.f )
		{
			const float32 s	   = MathUtil::sqrt( MathUtil::max( 0.0f, trace + 1.0f ) ) * 2.f;
			const float32 invS = s > MathUtil::Epsilon ? ( 1.f / s ) : 1.f;
			return quaternion{
				( matrix._23 - matrix._32 ) * invS,
				( matrix._31 - matrix._13 ) * invS,
				( matrix._12 - matrix._21 ) * invS,
				0.25f * s };
		}
		if ( matrix._11 >= matrix._22 && matrix._11 >= matrix._33 )
		{
			const float32 s	   = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f + matrix._11 - matrix._22 - matrix._33 ) ) * 2.f;
			const float32 invS = s > MathUtil::Epsilon ? ( 1.f / s ) : 1.f;
			return quaternion{
				0.25f * s,
				( matrix._12 + matrix._21 ) * invS,
				( matrix._13 + matrix._31 ) * invS,
				( matrix._23 - matrix._32 ) * invS };
		}
		if ( matrix._22 >= matrix._33 )
		{
			const float32 s	   = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f + matrix._22 - matrix._11 - matrix._33 ) ) * 2.f;
			const float32 invS = s > MathUtil::Epsilon ? ( 1.f / s ) : 1.f;
			return quaternion{
				( matrix._12 + matrix._21 ) * invS,
				0.25f * s,
				( matrix._23 + matrix._32 ) * invS,
				( matrix._31 - matrix._13 ) * invS };
		}
		const float32 s	   = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f + matrix._33 - matrix._11 - matrix._22 ) ) * 2.f;
		const float32 invS = s > MathUtil::Epsilon ? ( 1.f / s ) : 1.f;
		return quaternion{
			( matrix._13 + matrix._31 ) * invS,
			( matrix._23 + matrix._32 ) * invS,
			0.25f * s,
			( matrix._12 - matrix._21 ) * invS };
	}

	quaternion quaternion::rotateTowards( const quaternion& from, const quaternion& to, float32 maxAngle ) noexcept
	{
		const float32 angle = getAngleBetween( from, to );
		if ( MathUtil::nearEqual( angle, 0.f ) )
			return to;

		const float32 t = MathUtil::min( 1.f, maxAngle / angle );
		return slerp( from, to, t );
	}

	quaternion quaternion::lerp( const quaternion& from, const quaternion& to, float32 t ) noexcept
	{
		quaternion q2	  = to;
		float32	   dotVal = from.dot( to );
		if ( dotVal < 0.f )
			q2 = -to;

		return ( ( 1.f - t ) * from + t * q2 ).normalize();
	}

	quaternion quaternion::slerp( const quaternion& from, const quaternion& to, float32 t ) noexcept
	{
		quaternion q2	  = to;
		float32	   dotVal = from.dot( to );
		if ( dotVal < 0.f )
		{
			dotVal = -dotVal;
			q2	   = -to;
		}

		if ( dotVal > 0.9995f )
			return lerp( from, q2, t );

		const float32 theta	   = MathUtil::acos( MathUtil::clamp( dotVal, -1.0f, 1.0f ) );
		const float32 sinTheta = MathUtil::sin( theta );
		const float32 scale1   = MathUtil::sin( ( 1.f - t ) * theta ) / sinTheta;
		const float32 scale2   = MathUtil::sin( t * theta ) / sinTheta;

		return ( scale1 * from ) + ( scale2 * q2 );
	}

	quaternion quaternion::concatenate( const quaternion& q1, const quaternion& q2 ) noexcept
	{
		return q2 * q1;
	}

	quaternion quaternion::fromToRotation( const float3& from, const float3& to ) noexcept
	{
		const float3  f = from.normalize();
		const float3  t = to.normalize();
		const float32 d = f.dot( t );

		if ( d >= 1.f - MathUtil::Epsilon )
			return Identity;

		if ( d <= -1.f + MathUtil::Epsilon )
		{
			float3 axis = float3::Right.cross( f );
			if ( axis.getLengthSquared() < MathUtil::Epsilon )
				axis = float3::Up.cross( f );
			return createFromAxisAngle( axis, MathUtil::Pi );
		}

		const float3 c = f.cross( t );
		return quaternion{ c._x, c._y, c._z, 1.f + d }.normalize();
	}

	quaternion quaternion::lookRotation( const float3& direction, const float3& up ) noexcept
	{
		float3 f = direction.normalize();
		if ( f.getLengthSquared() < MathUtil::Epsilon )
			return Identity;

		float3 upVec = up.normalize();
		if ( upVec.getLengthSquared() < MathUtil::Epsilon )
			upVec = float3::Up;

		float3 r = upVec.cross( f ).normalize();
		if ( r.getLengthSquared() < MathUtil::Epsilon )
		{
			const float3 altUp = MathUtil::abs( f._y ) > 0.99f ? float3::Forward : float3::Up;
			r				   = altUp.cross( f ).normalize();
		}

		const float3   u = f.cross( r );
		const float4x4 m{ r._x, r._y, r._z, 0.f, u._x, u._y, u._z, 0.f, f._x, f._y, f._z, 0.f, 0.f, 0.f, 0.f, 1.f };
		return createFromRotationMatrix( m );
	}

	float32 quaternion::getAngleBetween( const quaternion& lhs, const quaternion& rhs ) noexcept
	{
		const float32 dotVal = MathUtil::abs( lhs.dot( rhs ) );
		return ( dotVal > 1.f ) ? 0.f : MathUtil::acos( MathUtil::clamp( dotVal, -1.0f, 1.0f ) ) * 2.f;
	}

	float32 quaternion::norm() const noexcept
	{
		return MathUtil::sqrt( normSquared() );
	}

	float32 quaternion::normSquared() const noexcept
	{
		return ( _x * _x ) + ( _y * _y ) + ( _z * _z ) + ( _w * _w );
	}

	quaternion& quaternion::normalize() noexcept
	{
		const float32 invNorm = MathUtil::invSqrt( normSquared() );
		if ( invNorm > 0.f )
			( *this ) *= invNorm;
		else
			( *this ) = Identity;
		return *this;
	}

	quaternion quaternion::normalize() const noexcept
	{
		const float32 invNorm = MathUtil::invSqrt( normSquared() );
		if ( invNorm > 0.f )
			return ( *this ) * invNorm;
		return Identity;
	}

	void quaternion::conjugate() noexcept
	{
		_x = -_x;
		_y = -_y;
		_z = -_z;
	}

	quaternion quaternion::conjugate() const noexcept
	{
		return quaternion{ -_x, -_y, -_z, _w };
	}

	void quaternion::inverse() noexcept
	{
		const float32 nSq = normSquared();
		if ( nSq > MathUtil::Epsilon )
		{
			conjugate();
			( *this ) /= nSq;
		}
	}

	quaternion quaternion::inverse() const noexcept
	{
		const float32 nSq = normSquared();
		if ( nSq > MathUtil::Epsilon )
			return conjugate() / nSq;
		return Identity;
	}

	float32 quaternion::dot( const quaternion& other ) const noexcept
	{
		return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z ) + ( _w * other._w );
	}

	float3 quaternion::getEulerAngles() const noexcept
	{

		const float32 sinp = 2.f * ( _w * _x - _y * _z );
		float32		  pitch{};
		if ( MathUtil::abs( sinp ) >= 1.f )
			pitch = ( sinp >= 0.f ) ? MathUtil::HalfPi : -MathUtil::HalfPi;
		else
			pitch = MathUtil::asin( sinp );

		const float32 siny_cosp = 2.f * ( _w * _y + _z * _x );
		const float32 cosy_cosp = 1.f - 2.f * ( _x * _x + _y * _y );
		const float32 yaw		= MathUtil::atan2( siny_cosp, cosy_cosp );

		const float32 sinr_cosp = 2.f * ( _w * _z + _x * _y );
		const float32 cosr_cosp = 1.f - 2.f * ( _x * _x + _z * _z );
		const float32 roll		= MathUtil::atan2( sinr_cosp, cosr_cosp );

		return float3{ pitch, yaw, roll };
	}

	float4x4 quaternion::toMatrix() const noexcept
	{
		return float4x4::createFromQuaternion( *this );
	}

	bool quaternion::operator==( const quaternion& other ) const noexcept { return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z ) && MathUtil::nearEqual( _w, other._w ); }

	bool quaternion::operator!=( const quaternion& other ) const noexcept { return !( *this == other ); }

	quaternion& quaternion::operator+=( const quaternion& other ) noexcept
	{
		_x += other._x;
		_y += other._y;
		_z += other._z;
		_w += other._w;
		return *this;
	}

	quaternion& quaternion::operator-=( const quaternion& other ) noexcept
	{
		_x -= other._x;
		_y -= other._y;
		_z -= other._z;
		_w -= other._w;
		return *this;
	}

	quaternion& quaternion::operator*=( const quaternion& other ) noexcept
	{
		( *this ) = ( *this ) * other;
		return *this;
	}

	quaternion& quaternion::operator*=( float32 scale ) noexcept
	{
		_x *= scale;
		_y *= scale;
		_z *= scale;
		_w *= scale;
		return *this;
	}

	quaternion& quaternion::operator/=( float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		_x *= inv;
		_y *= inv;
		_z *= inv;
		_w *= inv;
		return *this;
	}

	quaternion quaternion::operator-() const noexcept
	{
		return quaternion{ -_x, -_y, -_z, -_w };
	}

	quaternion operator*( const quaternion& lhs, const quaternion& rhs ) noexcept
	{
		return quaternion{
			( lhs._w * rhs._x ) + ( lhs._x * rhs._w ) + ( lhs._y * rhs._z ) - ( lhs._z * rhs._y ),
			( lhs._w * rhs._y ) - ( lhs._x * rhs._z ) + ( lhs._y * rhs._w ) + ( lhs._z * rhs._x ),
			( lhs._w * rhs._z ) + ( lhs._x * rhs._y ) - ( lhs._y * rhs._x ) + ( lhs._z * rhs._w ),
			( lhs._w * rhs._w ) - ( lhs._x * rhs._x ) - ( lhs._y * rhs._y ) - ( lhs._z * rhs._z ) };
	}

	const float4x4 float4x4::Identity{
		float4{1.f, 0.f, 0.f, 0.f},
		float4{0.f, 1.f, 0.f, 0.f},
		float4{0.f, 0.f, 1.f, 0.f},
		float4{0.f, 0.f, 0.f, 1.f},
	};

	float4x4::float4x4( const float32* pArray ) noexcept
	{
		if ( pArray != nullptr )
			Memory::copy( &_11, pArray, sizeof( float32 ) * 16 );
	}

	float4x4 float4x4::createTranslation( const float3& position ) noexcept
	{
		return createTranslation( position._x, position._y, position._z );
	}

	float4x4 float4x4::createTranslation( float32 x, float32 y, float32 z ) noexcept
	{
		return float4x4{ 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, x, y, z, 1.f };
	}

	float4x4 float4x4::createScale( const float3& scales ) noexcept
	{
		return createScale( scales._x, scales._y, scales._z );
	}

	float4x4 float4x4::createScale( float32 x, float32 y, float32 z ) noexcept
	{
		return float4x4{ x, 0.f, 0.f, 0.f, 0.f, y, 0.f, 0.f, 0.f, 0.f, z, 0.f, 0.f, 0.f, 0.f, 1.f };
	}

	float4x4 float4x4::createScale( float32 scale ) noexcept
	{
		return createScale( scale, scale, scale );
	}

	float4x4 float4x4::createRotationX( float32 radians ) noexcept
	{
		const float32 s = MathUtil::sin( radians );
		const float32 c = MathUtil::cos( radians );
		return float4x4{ 1.f, 0.f, 0.f, 0.f, 0.f, c, s, 0.f, 0.f, -s, c, 0.f, 0.f, 0.f, 0.f, 1.f };
	}

	float4x4 float4x4::createRotationY( float32 radians ) noexcept
	{
		const float32 s = MathUtil::sin( radians );
		const float32 c = MathUtil::cos( radians );
		return float4x4{ c, 0.f, -s, 0.f, 0.f, 1.f, 0.f, 0.f, s, 0.f, c, 0.f, 0.f, 0.f, 0.f, 1.f };
	}

	float4x4 float4x4::createRotationZ( float32 radians ) noexcept
	{
		const float32 s = MathUtil::sin( radians );
		const float32 c = MathUtil::cos( radians );
		return float4x4{ c, s, 0.f, 0.f, -s, c, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
	}

	float4x4 float4x4::createFromAxisAngle( const float3& axis, float32 angle ) noexcept
	{
		const quaternion q = quaternion::createFromAxisAngle( axis, angle );
		return createFromQuaternion( q );
	}

	float4x4 float4x4::createPerspectiveFieldOfView( float32 fov, float32 aspectRatio, float32 nearPlane, float32 farPlane ) noexcept
	{
		const float32 safeFov	 = MathUtil::clamp( fov, 0.001f, MathUtil::Pi - 0.001f );
		const float32 safeAspect = aspectRatio > MathUtil::Epsilon ? aspectRatio : 1.0f;
		const float32 span		 = ( farPlane - nearPlane );
		const float32 safeSpan	 = MathUtil::abs( span ) > MathUtil::Epsilon ? span : 1.0f;

		const float32 yScale = 1.0f / MathUtil::tan( safeFov * 0.5f );
		const float32 xScale = yScale / safeAspect;
		return float4x4{ xScale, 0.f, 0.f, 0.f, 0.f, yScale, 0.f, 0.f, 0.f, 0.f, farPlane / safeSpan, 1.f, 0.f, 0.f, -nearPlane * farPlane / safeSpan, 0.f };
	}

	float4x4 float4x4::createPerspective( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept
	{
		const float32 safeW	   = MathUtil::abs( width ) > MathUtil::Epsilon ? width : 1.0f;
		const float32 safeH	   = MathUtil::abs( height ) > MathUtil::Epsilon ? height : 1.0f;
		const float32 span	   = ( farPlane - nearPlane );
		const float32 safeSpan = MathUtil::abs( span ) > MathUtil::Epsilon ? span : 1.0f;
		return float4x4{ 2.f * nearPlane / safeW, 0.f, 0.f, 0.f, 0.f, 2.f * nearPlane / safeH, 0.f, 0.f, 0.f, 0.f, farPlane / safeSpan, 1.f, 0.f, 0.f, -nearPlane * farPlane / safeSpan, 0.f };
	}

	float4x4 float4x4::createPerspectiveOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept
	{
		const float32 spanX	   = ( right - left );
		const float32 spanY	   = ( top - bottom );
		const float32 safeX	   = MathUtil::abs( spanX ) > MathUtil::Epsilon ? spanX : 1.0f;
		const float32 safeY	   = MathUtil::abs( spanY ) > MathUtil::Epsilon ? spanY : 1.0f;
		const float32 spanZ	   = ( farPlane - nearPlane );
		const float32 safeSpan = MathUtil::abs( spanZ ) > MathUtil::Epsilon ? spanZ : 1.0f;
		return float4x4{ 2.f * nearPlane / safeX, 0.f, 0.f, 0.f, 0.f, 2.f * nearPlane / safeY, 0.f, 0.f, ( left + right ) / ( -safeX ), ( top + bottom ) / ( -safeY ), farPlane / safeSpan, 1.f, 0.f, 0.f, -nearPlane * farPlane / safeSpan, 0.f };
	}

	float4x4 float4x4::createOrthographic( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept
	{
		const float32 safeW	   = MathUtil::abs( width ) > MathUtil::Epsilon ? width : 1.0f;
		const float32 safeH	   = MathUtil::abs( height ) > MathUtil::Epsilon ? height : 1.0f;
		const float32 span	   = ( farPlane - nearPlane );
		const float32 safeSpan = MathUtil::abs( span ) > MathUtil::Epsilon ? span : 1.0f;
		return float4x4{ 2.f / safeW, 0.f, 0.f, 0.f, 0.f, 2.f / safeH, 0.f, 0.f, 0.f, 0.f, 1.f / safeSpan, 0.f, 0.f, 0.f, -nearPlane / safeSpan, 1.f };
	}

	float4x4 float4x4::createOrthographicOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept
	{
		const float32 spanX	   = ( right - left );
		const float32 spanY	   = ( top - bottom );
		const float32 safeX	   = MathUtil::abs( spanX ) > MathUtil::Epsilon ? spanX : 1.0f;
		const float32 safeY	   = MathUtil::abs( spanY ) > MathUtil::Epsilon ? spanY : 1.0f;
		const float32 spanZ	   = ( farPlane - nearPlane );
		const float32 safeSpan = MathUtil::abs( spanZ ) > MathUtil::Epsilon ? spanZ : 1.0f;
		return float4x4{ 2.f / safeX, 0.f, 0.f, 0.f, 0.f, 2.f / safeY, 0.f, 0.f, 0.f, 0.f, 1.f / safeSpan, 0.f, ( left + right ) / ( -safeX ), ( top + bottom ) / ( -safeY ), -nearPlane / safeSpan, 1.f };
	}

	float4x4 float4x4::createLookAt( const float3& position, const float3& target, const float3& up ) noexcept
	{
		float3 zAxis = ( target - position ).normalize();
		if ( zAxis.getLengthSquared() < MathUtil::Epsilon )
			zAxis = float3::Forward;

		float3 upVec = up.normalize();
		if ( upVec.getLengthSquared() < MathUtil::Epsilon )
			upVec = float3::Up;

		float3 xAxis = upVec.cross( zAxis ).normalize();
		if ( xAxis.getLengthSquared() < MathUtil::Epsilon )
		{
			const float3 altUp = MathUtil::abs( zAxis._y ) > 0.99f ? float3::Forward : float3::Up;
			xAxis			   = altUp.cross( zAxis ).normalize();
		}

		const float3 yAxis = zAxis.cross( xAxis );

		return float4x4{ xAxis._x, yAxis._x, zAxis._x, 0.f, xAxis._y, yAxis._y, zAxis._y, 0.f, xAxis._z, yAxis._z, zAxis._z, 0.f, -xAxis.dot( position ), -yAxis.dot( position ), -zAxis.dot( position ), 1.f };
	}

	float4x4 float4x4::createWorld( const float3& position, const float3& forward, const float3& up ) noexcept
	{
		float3 zAxis = forward.normalize();
		if ( zAxis.getLengthSquared() < MathUtil::Epsilon )
			zAxis = float3::Forward;

		float3 upVec = up.normalize();
		if ( upVec.getLengthSquared() < MathUtil::Epsilon )
			upVec = float3::Up;

		float3 xAxis = upVec.cross( zAxis ).normalize();
		if ( xAxis.getLengthSquared() < MathUtil::Epsilon )
		{
			const float3 altUp = MathUtil::abs( zAxis._y ) > 0.99f ? float3::Forward : float3::Up;
			xAxis			   = altUp.cross( zAxis ).normalize();
		}

		const float3 yAxis = zAxis.cross( xAxis );

		return float4x4{ xAxis._x, xAxis._y, xAxis._z, 0.f, yAxis._x, yAxis._y, yAxis._z, 0.f, zAxis._x, zAxis._y, zAxis._z, 0.f, position._x, position._y, position._z, 1.f };
	}

	float4x4 float4x4::createFromQuaternion( const quaternion& q ) noexcept
	{
		const float32 xx = q._x * q._x;
		const float32 yy = q._y * q._y;
		const float32 zz = q._z * q._z;
		const float32 xy = q._x * q._y;
		const float32 xz = q._x * q._z;
		const float32 yz = q._y * q._z;
		const float32 wx = q._w * q._x;
		const float32 wy = q._w * q._y;
		const float32 wz = q._w * q._z;

		return float4x4{ 1.f - 2.f * ( yy + zz ), 2.f * ( xy + wz ), 2.f * ( xz - wy ), 0.f, 2.f * ( xy - wz ), 1.f - 2.f * ( xx + zz ), 2.f * ( yz + wx ), 0.f, 2.f * ( xz + wy ), 2.f * ( yz - wx ), 1.f - 2.f * ( xx + yy ), 0.f, 0.f, 0.f, 0.f, 1.f };
	}

	float4x4 float4x4::createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept
	{
		const quaternion q = quaternion::createFromYawPitchRoll( yaw, pitch, roll );
		return createFromQuaternion( q );
	}

	float4x4 float4x4::createFromYawPitchRoll( const float3& angles ) noexcept
	{
		return createFromYawPitchRoll( angles._y, angles._x, angles._z );
	}

	float4x4 float4x4::lerp( const float4x4& from, const float4x4& to, float32 t ) noexcept
	{
		return float4x4{ MathUtil::lerp( from._11, to._11, t ), MathUtil::lerp( from._12, to._12, t ), MathUtil::lerp( from._13, to._13, t ), MathUtil::lerp( from._14, to._14, t ), MathUtil::lerp( from._21, to._21, t ), MathUtil::lerp( from._22, to._22, t ), MathUtil::lerp( from._23, to._23, t ), MathUtil::lerp( from._24, to._24, t ), MathUtil::lerp( from._31, to._31, t ), MathUtil::lerp( from._32, to._32, t ), MathUtil::lerp( from._33, to._33, t ), MathUtil::lerp( from._34, to._34, t ), MathUtil::lerp( from._41, to._41, t ), MathUtil::lerp( from._42, to._42, t ), MathUtil::lerp( from._43, to._43, t ), MathUtil::lerp( from._44, to._44, t ) };
	}

	float4x4 float4x4::transform( const float4x4& matrix, const quaternion& rotation ) noexcept
	{
		return matrix * createFromQuaternion( rotation );
	}

	bool float4x4::decompose( float3& outScale, quaternion& outRotation, float3& outTranslation ) const noexcept
	{
		outTranslation = getTranslation();
		outScale	   = getScale();

		if ( MathUtil::nearEqual( outScale._x, 0.f ) || MathUtil::nearEqual( outScale._y, 0.f ) || MathUtil::nearEqual( outScale._z, 0.f ) )
		{
			outRotation = quaternion::Identity;
			return false;
		}

		float4x4 rotMat = *this;
		rotMat._11 /= outScale._x;
		rotMat._12 /= outScale._x;
		rotMat._13 /= outScale._x;
		rotMat._21 /= outScale._y;
		rotMat._22 /= outScale._y;
		rotMat._23 /= outScale._y;
		rotMat._31 /= outScale._z;
		rotMat._32 /= outScale._z;
		rotMat._33 /= outScale._z;
		rotMat._41 = 0.f;
		rotMat._42 = 0.f;
		rotMat._43 = 0.f;
		rotMat._44 = 1.f;

		outRotation = quaternion::createFromRotationMatrix( rotMat );
		return true;
	}

	float3 float4x4::getScale() const noexcept
	{
		const float3 r{ _11, _12, _13 };
		const float3 u{ _21, _22, _23 };
		const float3 f{ _31, _32, _33 };
		return float3{ r.getLength(), u.getLength(), f.getLength() };
	}

	quaternion float4x4::getRotation() const noexcept
	{
		float3	   scale{};
		quaternion rot{};
		float3	   trans{};
		decompose( scale, rot, trans );
		return rot;
	}

	float3 float4x4::getTranslation() const noexcept
	{
		return float3{ _41, _42, _43 };
	}

	void float4x4::setScale( const float3& scale ) noexcept
	{
		const float3 curScale = getScale();
		if ( curScale._x > MathUtil::Epsilon )
		{
			const float32 inv = scale._x / curScale._x;
			_11 *= inv;
			_12 *= inv;
			_13 *= inv;
		}
		else
		{
			_11 = scale._x;
			_12 = 0.f;
			_13 = 0.f;
		}

		if ( curScale._y > MathUtil::Epsilon )
		{
			const float32 inv = scale._y / curScale._y;
			_21 *= inv;
			_22 *= inv;
			_23 *= inv;
		}
		else
		{
			_21 = 0.f;
			_22 = scale._y;
			_23 = 0.f;
		}

		if ( curScale._z > MathUtil::Epsilon )
		{
			const float32 inv = scale._z / curScale._z;
			_31 *= inv;
			_32 *= inv;
			_33 *= inv;
		}
		else
		{
			_31 = 0.f;
			_32 = 0.f;
			_33 = scale._z;
		}
	}

	void float4x4::setRotation( const quaternion& rotation ) noexcept
	{
		const float3   curScale = getScale();
		const float3   curTrans = getTranslation();
		const float4x4 rotMat	= createFromQuaternion( rotation );

		_11 = rotMat._11 * curScale._x;
		_12 = rotMat._12 * curScale._x;
		_13 = rotMat._13 * curScale._x;
		_21 = rotMat._21 * curScale._y;
		_22 = rotMat._22 * curScale._y;
		_23 = rotMat._23 * curScale._y;
		_31 = rotMat._31 * curScale._z;
		_32 = rotMat._32 * curScale._z;
		_33 = rotMat._33 * curScale._z;
		_41 = curTrans._x;
		_42 = curTrans._y;
		_43 = curTrans._z;
	}

	void float4x4::setTranslation( const float3& translation ) noexcept
	{
		_41 = translation._x;
		_42 = translation._y;
		_43 = translation._z;
	}

	float32 float4x4::determinant() const noexcept
	{
		return _11 * ( _22 * ( _33 * _44 - _34 * _43 ) - _23 * ( _32 * _44 - _34 * _42 ) + _24 * ( _32 * _43 - _33 * _42 ) ) - _12 * ( _21 * ( _33 * _44 - _34 * _43 ) - _23 * ( _31 * _44 - _34 * _41 ) + _24 * ( _31 * _43 - _33 * _41 ) ) + _13 * ( _21 * ( _32 * _44 - _34 * _42 ) - _22 * ( _31 * _44 - _34 * _41 ) + _24 * ( _31 * _42 - _32 * _41 ) ) - _14 * ( _21 * ( _32 * _43 - _33 * _42 ) - _22 * ( _31 * _43 - _33 * _41 ) + _23 * ( _31 * _42 - _32 * _41 ) );
	}

	float4x4 float4x4::transpose() const noexcept
	{
		return float4x4{ _11, _21, _31, _41, _12, _22, _32, _42, _13, _23, _33, _43, _14, _24, _34, _44 };
	}

	float4x4 float4x4::invert() const noexcept
	{
		const float32 det = determinant();
		if ( MathUtil::nearEqual( det, 0.f ) )
			return Identity;

		const float32 invDet = 1.0f / det;
		float4x4	  res{};

		res._11 = ( _22 * ( _33 * _44 - _34 * _43 ) - _23 * ( _32 * _44 - _34 * _42 ) + _24 * ( _32 * _43 - _33 * _42 ) ) * invDet;
		res._12 = -( _12 * ( _33 * _44 - _34 * _43 ) - _13 * ( _32 * _44 - _34 * _42 ) + _14 * ( _32 * _43 - _33 * _42 ) ) * invDet;
		res._13 = ( _12 * ( _23 * _44 - _24 * _43 ) - _13 * ( _22 * _44 - _24 * _42 ) + _14 * ( _22 * _43 - _23 * _42 ) ) * invDet;
		res._14 = -( _12 * ( _23 * _34 - _24 * _33 ) - _13 * ( _22 * _34 - _24 * _32 ) + _14 * ( _22 * _33 - _23 * _32 ) ) * invDet;

		res._21 = -( _21 * ( _33 * _44 - _34 * _43 ) - _23 * ( _31 * _44 - _34 * _41 ) + _24 * ( _31 * _43 - _33 * _41 ) ) * invDet;
		res._22 = ( _11 * ( _33 * _44 - _34 * _43 ) - _13 * ( _31 * _44 - _34 * _41 ) + _14 * ( _31 * _43 - _33 * _41 ) ) * invDet;
		res._23 = -( _11 * ( _23 * _44 - _24 * _43 ) - _13 * ( _21 * _44 - _24 * _41 ) + _14 * ( _21 * _43 - _23 * _41 ) ) * invDet;
		res._24 = ( _11 * ( _23 * _34 - _24 * _33 ) - _13 * ( _21 * _34 - _24 * _31 ) + _14 * ( _21 * _33 - _23 * _31 ) ) * invDet;

		res._31 = ( _21 * ( _32 * _44 - _34 * _42 ) - _22 * ( _31 * _44 - _34 * _41 ) + _24 * ( _31 * _42 - _32 * _41 ) ) * invDet;
		res._32 = -( _11 * ( _32 * _44 - _34 * _42 ) - _12 * ( _31 * _44 - _34 * _41 ) + _14 * ( _31 * _42 - _32 * _41 ) ) * invDet;
		res._33 = ( _11 * ( _22 * _44 - _24 * _42 ) - _12 * ( _21 * _44 - _24 * _41 ) + _14 * ( _21 * _42 - _22 * _41 ) ) * invDet;
		res._34 = -( _11 * ( _22 * _34 - _24 * _32 ) - _12 * ( _21 * _34 - _24 * _31 ) + _14 * ( _21 * _32 - _22 * _31 ) ) * invDet;

		res._41 = -( _21 * ( _32 * _43 - _33 * _42 ) - _22 * ( _31 * _43 - _33 * _41 ) + _23 * ( _31 * _42 - _32 * _41 ) ) * invDet;
		res._42 = ( _11 * ( _32 * _43 - _33 * _42 ) - _12 * ( _31 * _43 - _33 * _41 ) + _13 * ( _31 * _42 - _32 * _41 ) ) * invDet;
		res._43 = -( _11 * ( _22 * _43 - _23 * _42 ) - _12 * ( _21 * _43 - _23 * _41 ) + _13 * ( _21 * _42 - _22 * _41 ) ) * invDet;
		res._44 = ( _11 * ( _22 * _33 - _23 * _32 ) - _12 * ( _21 * _33 - _23 * _31 ) + _13 * ( _21 * _32 - _22 * _31 ) ) * invDet;

		return res;
	}

	bool float4x4::operator==( const float4x4& other ) const noexcept { return ( MathUtil::nearEqual( _11, other._11 ) && MathUtil::nearEqual( _12, other._12 ) && MathUtil::nearEqual( _13, other._13 ) && MathUtil::nearEqual( _14, other._14 ) ) && ( MathUtil::nearEqual( _21, other._21 ) && MathUtil::nearEqual( _22, other._22 ) && MathUtil::nearEqual( _23, other._23 ) && MathUtil::nearEqual( _24, other._24 ) ) && ( MathUtil::nearEqual( _31, other._31 ) && MathUtil::nearEqual( _32, other._32 ) && MathUtil::nearEqual( _33, other._33 ) && MathUtil::nearEqual( _34, other._34 ) ) && ( MathUtil::nearEqual( _41, other._41 ) && MathUtil::nearEqual( _42, other._42 ) && MathUtil::nearEqual( _43, other._43 ) && MathUtil::nearEqual( _44, other._44 ) ); }

	bool float4x4::operator!=( const float4x4& other ) const noexcept { return !( *this == other ); }

	float4x4& float4x4::operator+=( const float4x4& other ) noexcept
	{
		_11 += other._11;
		_12 += other._12;
		_13 += other._13;
		_14 += other._14;
		_21 += other._21;
		_22 += other._22;
		_23 += other._23;
		_24 += other._24;
		_31 += other._31;
		_32 += other._32;
		_33 += other._33;
		_34 += other._34;
		_41 += other._41;
		_42 += other._42;
		_43 += other._43;
		_44 += other._44;
		return *this;
	}

	float4x4& float4x4::operator-=( const float4x4& other ) noexcept
	{
		_11 -= other._11;
		_12 -= other._12;
		_13 -= other._13;
		_14 -= other._14;
		_21 -= other._21;
		_22 -= other._22;
		_23 -= other._23;
		_24 -= other._24;
		_31 -= other._31;
		_32 -= other._32;
		_33 -= other._33;
		_34 -= other._34;
		_41 -= other._41;
		_42 -= other._42;
		_43 -= other._43;
		_44 -= other._44;
		return *this;
	}

	float4x4& float4x4::operator*=( const float4x4& other ) noexcept
	{
		( *this ) = ( *this ) * other;
		return *this;
	}

	float4x4& float4x4::operator*=( float32 scale ) noexcept
	{
		_11 *= scale;
		_12 *= scale;
		_13 *= scale;
		_14 *= scale;
		_21 *= scale;
		_22 *= scale;
		_23 *= scale;
		_24 *= scale;
		_31 *= scale;
		_32 *= scale;
		_33 *= scale;
		_34 *= scale;
		_41 *= scale;
		_42 *= scale;
		_43 *= scale;
		_44 *= scale;
		return *this;
	}

	float4x4& float4x4::operator/=( float32 scale ) noexcept
	{
		const float32 inv = 1.f / scale;
		( *this ) *= inv;
		return *this;
	}

	float4x4 float4x4::operator-() const noexcept
	{
		return float4x4{ -_11, -_12, -_13, -_14, -_21, -_22, -_23, -_24, -_31, -_32, -_33, -_34, -_41, -_42, -_43, -_44 };
	}

	float4x4 operator*( const float4x4& lhs, const float4x4& rhs ) noexcept
	{
		return float4x4{
			( lhs._11 * rhs._11 + lhs._12 * rhs._21 + lhs._13 * rhs._31 + lhs._14 * rhs._41 ),
			( lhs._11 * rhs._12 + lhs._12 * rhs._22 + lhs._13 * rhs._32 + lhs._14 * rhs._42 ),
			( lhs._11 * rhs._13 + lhs._12 * rhs._23 + lhs._13 * rhs._33 + lhs._14 * rhs._43 ),
			( lhs._11 * rhs._14 + lhs._12 * rhs._24 + lhs._13 * rhs._34 + lhs._14 * rhs._44 ),

			( lhs._21 * rhs._11 + lhs._22 * rhs._21 + lhs._23 * rhs._31 + lhs._24 * rhs._41 ),
			( lhs._21 * rhs._12 + lhs._22 * rhs._22 + lhs._23 * rhs._32 + lhs._24 * rhs._42 ),
			( lhs._21 * rhs._13 + lhs._22 * rhs._23 + lhs._23 * rhs._33 + lhs._24 * rhs._43 ),
			( lhs._21 * rhs._14 + lhs._22 * rhs._24 + lhs._23 * rhs._34 + lhs._24 * rhs._44 ),

			( lhs._31 * rhs._11 + lhs._32 * rhs._21 + lhs._33 * rhs._31 + lhs._34 * rhs._41 ),
			( lhs._31 * rhs._12 + lhs._32 * rhs._22 + lhs._33 * rhs._32 + lhs._34 * rhs._42 ),
			( lhs._31 * rhs._13 + lhs._32 * rhs._23 + lhs._33 * rhs._33 + lhs._34 * rhs._43 ),
			( lhs._31 * rhs._14 + lhs._32 * rhs._24 + lhs._33 * rhs._34 + lhs._34 * rhs._44 ),

			( lhs._41 * rhs._11 + lhs._42 * rhs._21 + lhs._43 * rhs._31 + lhs._44 * rhs._41 ),
			( lhs._41 * rhs._12 + lhs._42 * rhs._22 + lhs._43 * rhs._32 + lhs._44 * rhs._42 ),
			( lhs._41 * rhs._13 + lhs._42 * rhs._23 + lhs._43 * rhs._33 + lhs._44 * rhs._43 ),
			( lhs._41 * rhs._14 + lhs._42 * rhs._24 + lhs._43 * rhs._34 + lhs._44 * rhs._44 ) };
	}
} // namespace sw
