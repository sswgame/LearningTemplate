#include "pch.h"

#include "Engine/Object/Component/CameraComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    CameraComponent::CameraComponent()
        : _fovY{ kDefaultFovY }
        , _nearZ{ kDefaultNearZ }
        , _farZ{ kDefaultFarZ }
        , _orthoHeight{ kDefaultOrthoHeight }
        , _priority{ 0 }
        , _role{ CameraRole::Game }
        , _bOrthographic{ false }
    {
    }

    CameraComponent* CameraComponent::findOrCreateNamed( GameObjectManager* pObjectManager, hashed_string objectName, CameraRole role,
                                                         const float3& position, const float3& lookTarget )
    {
        if ( pObjectManager == nullptr )
            return nullptr;

        GameObject* pObj = pObjectManager->findGameObjectByName( objectName );
        if ( pObj == nullptr )
            pObj = pObjectManager->createGameObject( objectName );
        if ( pObj == nullptr )
            return nullptr;

        CameraComponent* pCam = pObj->getComponent<CameraComponent>();
        if ( pCam == nullptr )
            pCam = pObj->addComponent<CameraComponent>();
        if ( pCam == nullptr )
            return nullptr;

        pCam->setRole( role );
        pCam->setLocalPosition( position );
        pCam->lookAt( lookTarget );
        pCam->setFieldOfViewY( kDefaultFovY );
        pCam->setNearPlane( kDefaultNearZ );
        pCam->setFarPlane( kDefaultFarZ );
        return pCam;
    }

    void CameraComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
    }

    void CameraComponent::lookAt( const float3& target, const float3& up )
    {
        const float3  eye     = getWorldPosition();
        float3        forward = ( target - eye );
        const float32 lenSq   = forward.getLengthSquared();
        if ( lenSq <= MathUtil::Epsilon )
            return;
        forward.normalize();

        const float32 yaw   = MathUtil::atan2( forward._x, forward._z );
        const float32 pitch = -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
        (void)up;
        setLocalRotation( float3( pitch, yaw, 0.0f ) );
    }

    float4x4 CameraComponent::getViewMatrix() const
    {
        const float4x4 worldMat = getWorldMatrix();
        const float3   eye      = worldMat.getTranslation();
        const float3   forward  = float3::transformNormal( float3( 0.0f, 0.0f, 1.0f ), worldMat );
        const float3   up       = float3::transformNormal( float3( 0.0f, 1.0f, 0.0f ), worldMat );
        const float3   target   = eye + forward;
        return float4x4::createLookAt( eye, target, up );
    }

    float4x4 CameraComponent::getProjectionMatrix( float32 aspectRatio ) const
    {
        const float32 aspect = aspectRatio > 1e-4f ? aspectRatio : ( 16.0f / 9.0f );
        const float32 nearZ  = _nearZ > 1e-4f ? _nearZ : 0.1f;
        const float32 farZ   = _farZ > nearZ + 1e-4f ? _farZ : ( nearZ + 100.0f );

        if ( _bOrthographic )
        {
            const float32 height = _orthoHeight > 1e-4f ? _orthoHeight : 10.0f;
            const float32 width  = height * aspect;
            return float4x4::createOrthographic( width, height, nearZ, farZ );
        }
        const float32 fov = _fovY > 1e-4f ? _fovY : 0.70f;
        return float4x4::createPerspectiveFieldOfView( fov, aspect, nearZ, farZ );
    }

    float4x4 CameraComponent::getViewProjectionMatrix( float32 aspectRatio ) const
    {
        return getViewMatrix() * getProjectionMatrix( aspectRatio );
    }

} // namespace sw
