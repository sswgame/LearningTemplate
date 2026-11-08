#include "pch.h"

#include "Core/Math/DualQuaternion.h"

#include "Core/Math/Math.h"

namespace sw
{
    DualQuaternion::DualQuaternion()
        : _real{ 0.0f, 0.0f, 0.0f, 1.0f }
        , _dual{ 0.0f, 0.0f, 0.0f, 0.0f }
    {
    }

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
        // 스케일이 섞인 행렬에 makeFromRotationMatrix 를 바로 걸면 회전이 틀린다. 축 길이로
        // 먼저 나눠야 한다. 그 일은 Core 의 decompose 가 이미 하므로 여기서 다시 적지 않는다.
        // (스케일 (2,1,1) + Z 90도 행렬이 112.6도로 읽히던 경우다.)
        float3 scale{};
        return fromMatrix( mat, scale );
    }

    DualQuaternion DualQuaternion::fromMatrix( const float4x4& mat, float3& outScale )
    {
        // 축 하나가 0 이면 decompose 는 회전을 단위로 두고 false 다 — 그 본의 점은 스케일 0 으로 이동 자리에 모이므로 회전은 뜻이 없다.
        quaternion rot{};
        float3     trans{};
        static_cast<void>( mat.decompose( outScale, rot, trans ) );
        return DualQuaternion( rot, trans );
    }

    void DualQuaternion::normalize()
    {
        const float32 mag = _real.norm();
        if ( mag > MathUtil::kEpsilon )
        {
            const float32 invMag = 1.0f / mag;
            _real *= invMag;
            _dual *= invMag;
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
        float4x4     mat = float4x4::makeFromQuaternion( _real );
        const float3 t   = getTranslation();
        mat._41          = t._x;
        mat._42          = t._y;
        mat._43          = t._z;
        return mat;
    }

    float3 DualQuaternion::transformDirection( const float3& direction ) const
    {
        // v + 2 q.xyz × (q.xyz × v + w v) — `float4x4::makeFromQuaternion` 을 행벡터로 곱한 것과 같다.
        const float3 axis{ _real._x, _real._y, _real._z };
        const float3 inner = axis.cross( direction ) + direction * _real._w;
        return direction + axis.cross( inner ) * 2.0f;
    }

    float3 DualQuaternion::transformPoint( const float3& point ) const
    {
        return transformDirection( point ) + getTranslation();
    }

    DualQuaternion DualQuaternion::dlb( const DualQuaternion& a, const DualQuaternion& b, float32 t )
    {
        const float32 dot    = a._real.dot( b._real );
        const float32 scaleB = ( dot < 0.0f ) ? -t : t;
        const float32 scaleA = 1.0f - t;

        DualQuaternion result{};
        result._real = a._real * scaleA + b._real * scaleB;
        result._dual = a._dual * scaleA + b._dual * scaleB;

        result.normalize();
        return result;
    }
} // namespace sw
