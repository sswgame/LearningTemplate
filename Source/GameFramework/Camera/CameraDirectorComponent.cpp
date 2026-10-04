#include "pch.h"

#include "GameFramework/Camera/CameraDirectorComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_LOG_CALLER( "CameraDirectorComponent" );
} // namespace sw

namespace sw
{
    CameraDirectorComponent::CameraDirectorComponent()
        : _catalogPath{}
        , _initialPreset{}
        , _target{}
        , _catalog{}
        , _director{}
    {
        setCanEverTick( true );
    }

    void CameraDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 대상이 앞 그룹에서 움직인 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        if ( _catalogPath.empty() == false )
        {
            _catalog.clear();
            if ( _catalog.loadFromResource( _catalogPath ) == false )
                SW_LOG_WARNING( "Camera presets '%#' did not load", _catalogPath );
        }
        if ( _catalog.getPresets().empty() )
            return;
        const hashed_string startId = _initialPreset.empty() ? _catalog.getPresets().front()._id : _initialPreset;
        if ( _director.activatePreset( _catalog, startId ) == false )
            SW_LOG_WARNING( "Camera preset '%#' is not in '%#'", startId.c_str(), _catalogPath );
        updateCamera( 0.0f );
    }

    void CameraDirectorComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        updateCamera( deltaTime );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id )
    {
        return _director.activatePreset( _catalog, id );
    }

    bool CameraDirectorComponent::activatePreset( const hashed_string& id, const CameraBlendSpec& blend )
    {
        return _director.activatePreset( _catalog, id, blend );
    }

    void CameraDirectorComponent::updateCamera( float32 deltaTime )
    {
        if ( _director.hasActivePreset() == false )
            return;
        applyToCamera( _director.step( deltaTime, computeTarget() ) );
    }

    CameraTarget CameraDirectorComponent::computeTarget() const
    {
        CameraTarget             target;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( _target ) : nullptr;
        const SceneComponent*    pScene   = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
        if ( pScene == nullptr )
            return target;
        const float4x4 world = pScene->getWorldMatrix();
        target._focus        = world.getTranslation();
        float3 forward       = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        if ( forward.getLengthSquared() <= MathUtil::Epsilon )
            return target;
        forward.normalize();
        target._yaw   = MathUtil::atan2( forward._x, forward._z );
        target._pitch = -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
        return target;
    }

    void CameraDirectorComponent::applyToCamera( const CameraPose& pose ) const
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        if ( pCamera == nullptr )
            return;
        pCamera->setOrthographic( pose._bOrthographic == SW_TRUE );
        pCamera->setFieldOfViewY( pose._fieldOfViewY );
        pCamera->setOrthoHeight( pose._orthoHeight );
        pCamera->setNearPlane( pose._nearPlane );
        pCamera->setFarPlane( pose._farPlane );
        // 포즈는 월드 값이다 — 부모 아래에 둔 카메라도 같은 자리를 본다(스케일은 지금 것을 지킨다).
        float3     worldScale{};
        quaternion worldRotation{};
        float3     worldTranslation{};
        if ( pCamera->getWorldMatrix().decompose( worldScale, worldRotation, worldTranslation ) == false )
            worldScale = float3{ 1.0f, 1.0f, 1.0f };
        pCamera->setWorldTransform( float4x4::createTrs( pose._position, pose._rotation, worldScale ) );
    }
} // namespace sw
