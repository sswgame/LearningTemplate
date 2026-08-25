#include "pch.h"

#include "Engine/Animation/DualQuaternion.h"

#include "Core/Math/Math.h"

namespace sw
{
	DualQuaternion::DualQuaternion( const quaternion& r, const float3& t )
		: _real{ r }
		, _dual{
			  0.5f * ( t._x * r._w + t._y * r._z - t._z * r._y ),
			  0.5f * ( -t._x * r._z + t._y * r._w + t._z * r._x ),
			  0.5f * ( t._x * r._y - t._y * r._x + t._z * r._w ),
			  -0.5f * ( t._x * r._x + t._y * r._y + t._z * r._z ) }
	{
	}

	DualQuaternion::DualQuaternion( const quaternion& real, const quaternion& dual )
		: _real{ real }
		, _dual{ dual }
	{
	}

	DualQuaternion DualQuaternion::fromTransform( const float3& translation, const quaternion& rotation )
	{
		return DualQuaternion( rotation, translation );
	}

	DualQuaternion DualQuaternion::fromMatrix( const float4x4& mat )
	{
		const float3	 trans{ mat._41, mat._42, mat._43 };
		const quaternion rot = quaternion::createFromRotationMatrix( mat );
		return DualQuaternion( rot, trans );
	}

	void DualQuaternion::normalize()
	{
		const float32 mag = MathUtil::sqrt( _real._x * _real._x + _real._y * _real._y + _real._z * _real._z + _real._w * _real._w );
		if ( mag > 1e-6f )
		{
			const float32 invMag = 1.0f / mag;
			_real._x *= invMag;
			_real._y *= invMag;
			_real._z *= invMag;
			_real._w *= invMag;

			_dual._x *= invMag;
			_dual._y *= invMag;
			_dual._z *= invMag;
			_dual._w *= invMag;
		}
	}

	DualQuaternion DualQuaternion::normalized() const
	{
		DualQuaternion copy = *this;
		copy.normalize();
		return copy;
	}

	quaternion DualQuaternion::getRotation() const
	{
		return _real;
	}

	float3 DualQuaternion::getTranslation() const
	{
		return float3{
			2.0f * ( -_dual._w * _real._x + _dual._x * _real._w - _dual._y * _real._z + _dual._z * _real._y ),
			2.0f * ( -_dual._w * _real._y + _dual._x * _real._z + _dual._y * _real._w - _dual._z * _real._x ),
			2.0f * ( -_dual._w * _real._z - _dual._x * _real._y + _dual._y * _real._x + _dual._z * _real._w ) };
	}

	float4x4 DualQuaternion::toMatrix4x4() const
	{
		float4x4	 mat = float4x4::createFromQuaternion( _real );
		const float3 t	 = getTranslation();
		mat._41			 = t._x;
		mat._42			 = t._y;
		mat._43			 = t._z;
		return mat;
	}

	DualQuaternion DualQuaternion::dlb( const DualQuaternion& a, const DualQuaternion& b, float32 t )
	{
		float32		  dot	 = a._real._x * b._real._x + a._real._y * b._real._y + a._real._z * b._real._z + a._real._w * b._real._w;
		const float32 scaleB = ( dot < 0.0f ) ? -t : t;
		const float32 scaleA = 1.0f - t;

		DualQuaternion result{};
		result._real._x = a._real._x * scaleA + b._real._x * scaleB;
		result._real._y = a._real._y * scaleA + b._real._y * scaleB;
		result._real._z = a._real._z * scaleA + b._real._z * scaleB;
		result._real._w = a._real._w * scaleA + b._real._w * scaleB;

		result._dual._x = a._dual._x * scaleA + b._dual._x * scaleB;
		result._dual._y = a._dual._y * scaleA + b._dual._y * scaleB;
		result._dual._z = a._dual._z * scaleA + b._dual._z * scaleB;
		result._dual._w = a._dual._w * scaleA + b._dual._w * scaleB;

		result.normalize();
		return result;
	}
} // namespace sw
