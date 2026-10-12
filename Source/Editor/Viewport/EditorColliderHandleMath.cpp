#include "pch.h"

#include "Editor/Viewport/EditorColliderHandleMath.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/Physics/PhysicsComponent.h"

namespace sw::editor
{
    namespace
    {
        struct EditorColliderHandleMathInternal
        {
            /** @brief 비율이 이보다 작으면 그 축은 배율이 0 이라고 보고 크기를 바꾸지 않는다. */
            static constexpr float32 kMinScale = 0.0001f;

            static float32 getAxisValue( const float3& value, uint32 axis )
            {
                return axis == 0 ? value._x : ( axis == 1 ? value._y : value._z );
            }

            static void setAxisValue( float3& value, uint32 axis, float32 component )
            {
                if ( axis == 0 )
                    value._x = component;
                else if ( axis == 1 )
                    value._y = component;
                else
                    value._z = component;
            }

            static float3 makeAxisVector( uint32 axis )
            {
                float3 vector{};
                setAxisValue( vector, axis, 1.0f );
                return vector;
            }

            /** @brief 단위 크기(상자 1 · 반지름 1 · 반 높이 1)의 셰이프에 배율을 먹여 크기 비율을 얻습니다. */
            static PhysicsShapeDesc3D makeScaleProbe( const PhysicsShapeDesc3D& shape, const float3& bodyScale )
            {
                PhysicsShapeDesc3D probe;
                probe._type        = shape._type;
                probe._halfExtents = float3{ 1.0f, 1.0f, 1.0f };
                probe._radius      = 1.0f;
                probe._halfHeight  = 1.0f;
                return PhysicsComponentUtil::makeScaledShape( probe, bodyScale );
            }

            static void addHandle( vector<ColliderHandle>& outListHandle, ColliderHandleKind kind, uint32 shapeIndex, uint32 axis, float32 sign, const float3& center,
                                   const quaternion& frameRotation, float32 extent )
            {
                ColliderHandle handle;
                handle._kind       = kind;
                handle._shapeIndex = shapeIndex;
                handle._axis       = axis;
                handle._sign       = sign;
                handle._center     = center;
                handle._direction  = float3::transform( makeAxisVector( axis ) * sign, frameRotation );
                handle._position   = center + handle._direction * extent;
                outListHandle.push_back( handle );
            }
        };
    } // namespace

    void EditorColliderHandleMath::collectHandles( const PhysicsShapeDesc3D& shape, uint32 shapeIndex, const float3& bodyPosition, const quaternion& bodyRotation,
                                                   const float3& bodyScale, vector<ColliderHandle>& outListHandle )
    {
        using Internal = EditorColliderHandleMathInternal;
        if ( shape._type != PhysicsShapeType3D::Box && shape._type != PhysicsShapeType3D::Sphere && shape._type != PhysicsShapeType3D::Capsule )
            return;
        const PhysicsShapeDesc3D scaled        = PhysicsComponentUtil::makeScaledShape( shape, bodyScale );
        const quaternion         frameRotation = bodyRotation * quaternion::makeFromYawPitchRoll( scaled._localRotation );
        const float3             center        = bodyPosition + float3::transform( scaled._localPosition, bodyRotation );
        for ( uint32 axis = 0; axis < 3; ++axis )
        {
            for ( uint32 side = 0; side < 2; ++side )
            {
                const float32 sign = side == 0 ? 1.0f : -1.0f;
                if ( shape._type == PhysicsShapeType3D::Box )
                {
                    Internal::addHandle( outListHandle, ColliderHandleKind::BoxExtent, shapeIndex, axis, sign, center, frameRotation,
                                         Internal::getAxisValue( scaled._halfExtents, axis ) );
                }
                else if ( shape._type == PhysicsShapeType3D::Sphere )
                {
                    Internal::addHandle( outListHandle, ColliderHandleKind::SphereRadius, shapeIndex, axis, sign, center, frameRotation, scaled._radius );
                }
                else if ( axis == 1 )
                {
                    Internal::addHandle( outListHandle, ColliderHandleKind::CapsuleHalfHeight, shapeIndex, axis, sign, center, frameRotation,
                                         scaled._halfHeight + scaled._radius );
                }
                else
                {
                    Internal::addHandle( outListHandle, ColliderHandleKind::CapsuleRadius, shapeIndex, axis, sign, center, frameRotation, scaled._radius );
                }
            }
        }
    }

    PhysicsShapeDesc3D EditorColliderHandleMath::applyDrag( const PhysicsShapeDesc3D& shape, const ColliderHandle& handle, float32 outwardDistance,
                                                            const float3& bodyScale )
    {
        using Internal                    = EditorColliderHandleMathInternal;
        const float32            distance = MathUtil::max( outwardDistance, kMinExtent );
        const PhysicsShapeDesc3D probe    = Internal::makeScaleProbe( shape, bodyScale );
        PhysicsShapeDesc3D       resized  = shape;
        switch ( handle._kind )
        {
            case ColliderHandleKind::BoxExtent:
            {
                const float32 scale = Internal::getAxisValue( probe._halfExtents, handle._axis );
                if ( scale > Internal::kMinScale )
                    Internal::setAxisValue( resized._halfExtents, handle._axis, distance / scale );
                break;
            }
            case ColliderHandleKind::SphereRadius:
            case ColliderHandleKind::CapsuleRadius:
            {
                if ( probe._radius > Internal::kMinScale )
                    resized._radius = distance / probe._radius;
                break;
            }
            case ColliderHandleKind::CapsuleHalfHeight:
            {
                // 끈 거리는 끝 반구의 가장자리까지다 — 원기둥 반 높이는 거기서 (월드) 반지름을 뺀 만큼이다.
                const float32 scaledRadius = shape._radius * probe._radius;
                if ( probe._halfHeight > Internal::kMinScale )
                    resized._halfHeight = MathUtil::max( distance - scaledRadius, 0.0f ) / probe._halfHeight;
                break;
            }
        }
        return resized;
    }

    float32 EditorColliderHandleMath::computeClosestAxisParameter( const float3& rayOrigin, const float3& rayDirection, const float3& axisOrigin,
                                                                   const float3& axisDirection )
    {
        const float3  offset      = rayOrigin - axisOrigin;
        const float32 alignment   = rayDirection.dot( axisDirection );
        const float32 denominator = 1.0f - alignment * alignment;
        if ( denominator < 0.000001f )
            return 0.0f;
        const float32 alongRay  = rayDirection.dot( offset );
        const float32 alongAxis = axisDirection.dot( offset );
        return ( alongAxis - alignment * alongRay ) / denominator;
    }
} // namespace sw::editor
