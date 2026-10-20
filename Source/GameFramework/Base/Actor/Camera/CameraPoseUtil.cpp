#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraPoseUtil.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Window/IWindow.h"

#include "GameFramework/Base/Actor/Camera/CameraPreset.h"

namespace sw
{
    float32 CameraPoseUtil::computeAspectRatio( const CameraComponent& camera )
    {
        const CameraRenderOutput& output = camera.getRenderOutput();
        if ( output._target == CameraOutputTarget::RenderTexture )
        {
            if ( output._renderTextureHeight == 0 )
                return CameraTarget::kDefaultAspectRatio;
            return static_cast<float32>( output._renderTextureWidth ) / static_cast<float32>( output._renderTextureHeight );
        }
        const IWindow* pWindow = IWindow::getActiveWindow();
        if ( pWindow == nullptr || pWindow->getHeight() == 0 )
            return CameraTarget::kDefaultAspectRatio;
        // 화면 사각형(분할 화면, PiP)은 창의 일부다 — 가로 비율과 세로 비율을 곱해 센다.
        const float32 width  = static_cast<float32>( pWindow->getWidth() ) * output._screenRect._z;
        const float32 height = static_cast<float32>( pWindow->getHeight() ) * output._screenRect._w;
        return height > 0.0f ? width / height : CameraTarget::kDefaultAspectRatio;
    }

    void CameraPoseUtil::applyLensToCamera( CameraComponent& camera, const CameraPose& pose )
    {
        camera.setOrthographic( pose._bOrthographic == SW_TRUE );
        camera.setFieldOfViewY( pose._fieldOfViewY );
        camera.setOrthoHeight( pose._orthoHeight );
        camera.setNearPlane( pose._nearPlane );
        camera.setFarPlane( pose._farPlane );
        camera.setPostProcess( pose._postProcess );
    }

    void CameraPoseUtil::applyToCameraLocal( CameraComponent& camera, const CameraPose& pose )
    {
        applyLensToCamera( camera, pose );
        camera.setLocalPosition( pose._position );
        camera.setLocalRotation( pose._rotation.getEulerAngles() );
    }

    void CameraPoseUtil::applyToCamera( CameraComponent& camera, const CameraPose& pose )
    {
        applyLensToCamera( camera, pose );
        float3     worldScale{};
        quaternion worldRotation{};
        float3     worldTranslation{};
        if ( camera.getWorldMatrix().decompose( worldScale, worldRotation, worldTranslation ) == false )
            worldScale = float3{ 1.0f, 1.0f, 1.0f };
        camera.setWorldTransform( float4x4::makeTrs( pose._position, pose._rotation, worldScale ) );
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
        pose._postProcess   = camera.getPostProcess();
        return pose;
    }
} // namespace sw
