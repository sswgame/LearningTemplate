#include "pch.h"

#include "Games/AbilityArena/ArenaCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Camera/CameraMode.h"
#include "GameFramework/Base/Camera/CameraPoseUtil.h"

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
        // 플레이어 컨트롤러(DuringPhysics)가 옮긴 자리가 적용된 뒤(물리 뒤 단계)에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
        applyToCamera( float3{ 0.0f, 0.0f, 0.0f } );
    }

    void ArenaCameraComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ArenaDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<ArenaDirectorComponent>( *pManager, _director ) : nullptr;
        if ( pDirector == nullptr )
        {
            applyToCamera( float3{ 0.0f, 0.0f, 0.0f } );
            return;
        }
        // 디렉터의 초점은 PrePhysics 에서 적은 틱 전 자리다 — 그대로 쓰면 한 프레임 늦게 따라간다. 이 그룹은 물리 뒤 단계라 플레이어 컨트롤러의
        // 이번 프레임 쓰기가 이미 적용됐으므로 플레이어 메시의 자리를 바로 읽는다.
        const GameObject*    pPlayer = pManager->resolveGameObject( pDirector->getPlayerObject() );
        const MeshComponent* pMesh   = pPlayer != nullptr ? pPlayer->getComponent<MeshComponent>() : nullptr;
        applyToCamera( pMesh != nullptr ? pMesh->getWorldPosition() : pDirector->getPlayerFocus() );
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
