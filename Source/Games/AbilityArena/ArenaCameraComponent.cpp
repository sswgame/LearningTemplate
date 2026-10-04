#include "pch.h"

#include "Games/AbilityArena/ArenaCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Camera/CameraMode.h"
#include "GameFramework/Camera/CameraPoseUtil.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaCameraComponent::ArenaCameraComponent()
        : _director{}
        , _offset{ 0.0f, 15.0f, -11.0f }
        , _minFarPlane{ 120.0f }
    {
        setCanEverTick( true );
    }

    void ArenaCameraComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이번 프레임의 플레이어 자리를 적은 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        applyToCamera( float3{ 0.0f, 0.0f, 0.0f } );
    }

    void ArenaCameraComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaDirectorComponent* pDirector = pManager != nullptr ? ArenaDirectorComponent::resolveDirector( *pManager, _director ) : nullptr;
        applyToCamera( pDirector != nullptr ? pDirector->getPlayerFocus() : float3{ 0.0f, 0.0f, 0.0f } );
    }

    void ArenaCameraComponent::applyToCamera( const float3& focus )
    {
        GameObject*      pOwner  = getOwner();
        CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
        const float32    length  = _offset.getLength();
        if ( pCamera == nullptr || length <= MathUtil::Epsilon )
            return;
        // 오프셋은 궤도 모드(`CameraPresetMode::Orbit`)의 요 · 피치 · 거리다 — 카메라는 초점에서 오프셋만큼 떨어져 초점을 본다.
        // 모드 계산은 틱 중의 월드 행렬을 읽지 않으므로(틱 안의 자리 쓰기는 틱 뒤에 보인다) 이번 프레임의 초점으로 바로 놓인다.
        CameraPresetDef def;
        def._view._mode         = CameraPresetMode::Orbit;
        def._view._distance     = length;
        def._view._pitch        = MathUtil::asin( MathUtil::clamp( _offset._y / length, -1.0f, 1.0f ) );
        def._view._yaw          = MathUtil::atan2( -_offset._x, -_offset._z );
        def._lens._fieldOfViewY = pCamera->getFieldOfViewY();
        def._lens._nearPlane    = pCamera->getNearPlane();
        def._lens._farPlane     = MathUtil::max( pCamera->getFarPlane(), _minFarPlane );
        CameraTarget target;
        target._focus = focus;
        CameraPoseUtil::applyToCamera( *pCamera, evaluatePreset( def, target ) );
    }
} // namespace sw
