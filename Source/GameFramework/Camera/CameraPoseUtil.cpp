#include "pch.h"

#include "GameFramework/Camera/CameraPoseUtil.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
    void CameraPoseUtil::applyToCamera( CameraComponent& camera, const CameraPose& pose )
    {
        camera.setOrthographic( pose._bOrthographic == SW_TRUE );
        camera.setFieldOfViewY( pose._fieldOfViewY );
        camera.setOrthoHeight( pose._orthoHeight );
        camera.setNearPlane( pose._nearPlane );
        camera.setFarPlane( pose._farPlane );
        float3     worldScale{};
        quaternion worldRotation{};
        float3     worldTranslation{};
        if ( camera.getWorldMatrix().decompose( worldScale, worldRotation, worldTranslation ) == false )
            worldScale = float3{ 1.0f, 1.0f, 1.0f };
        camera.setWorldTransform( float4x4::createTrs( pose._position, pose._rotation, worldScale ) );
    }

    CameraPose CameraPoseUtil::makePoseFromCamera( const CameraComponent& camera )
    {
        CameraPose pose;
        float3     worldScale{};
        quaternion worldRotation{};
        float3     worldTranslation{};
        if ( camera.getWorldMatrix().decompose( worldScale, worldRotation, worldTranslation ) )
        {
            pose._position = worldTranslation;
            pose._rotation = worldRotation;
        }
        else
        {
            pose._position = camera.getWorldPosition();
        }
        pose._fieldOfViewY  = camera.getFieldOfViewY();
        pose._orthoHeight   = camera.getOrthoHeight();
        pose._nearPlane     = camera.getNearPlane();
        pose._farPlane      = camera.getFarPlane();
        pose._bOrthographic = camera.isOrthographic() ? SW_TRUE : SW_FALSE;
        return pose;
    }
} // namespace sw
