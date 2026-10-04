#include "pch.h"

#include "Games/AbilityArena/ArenaCameraComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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
        if ( pCamera == nullptr )
            return;
        // `lookAt` 은 지금 월드 행렬을 읽는다 — 틱 안의 자리 쓰기는 틱 뒤에 보이므로 보는 쪽을 오프셋에서 직접 구한다(`lookAt` 과 같은 배치).
        const float3  toFocus = float3{ 0.0f, 0.0f, 0.0f } - _offset;
        const float32 length  = toFocus.getLength();
        if ( length <= MathUtil::Epsilon )
            return;
        const float3 forward = toFocus * ( 1.0f / length );
        pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), _minFarPlane ) );
        pCamera->setLocalPosition( focus + _offset );
        pCamera->setLocalRotation( float3{ -MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) ), MathUtil::atan2( forward._x, forward._z ), 0.0f } );
    }
} // namespace sw
