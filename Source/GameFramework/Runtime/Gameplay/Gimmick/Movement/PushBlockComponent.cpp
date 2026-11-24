#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/PushBlockComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/World/Query/WorldQuery.h"

namespace sw
{
    PushBlockComponent::PushBlockComponent()
        : _cellSize{ 1.0f }
        , _moveTime{ 0.3f }
        , _bPlanar2D{ false }
        , _from{}
        , _to{}
        , _stepsLeft{ 0 }
        , _moveSteps{ 0 }
    {
    }

    bool PushBlockComponent::push( const float3& direction )
    {
        GameObject*              pOwner      = getOwner();
        const GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr; // 광선 읽기만 한다
        SceneComponent*          pScene      = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const bool               bCannotMove = pManager == nullptr || pScene == nullptr || isMoving();
        if ( bCannotMove )
            return false;
        // 가장 가까운 축 하나로 맞춘다. 3D 는 X/Z, 2D 는 X/Y 축을 쓴다.
        const float32 along  = direction._x;
        const float32 across = _bPlanar2D ? direction._y : direction._z;
        float3        cellStep{};
        if ( MathUtil::abs( along ) >= MathUtil::abs( across ) && along != 0.0f )
            cellStep._x = along > 0.0f ? _cellSize : -_cellSize;
        else if ( across != 0.0f && _bPlanar2D )
            cellStep._y = across > 0.0f ? _cellSize : -_cellSize;
        else if ( across != 0.0f )
            cellStep._z = across > 0.0f ? _cellSize : -_cellSize;
        else
            return false;
        const float3 from = pScene->getWorldPosition();
        WorldRayHit  hit;
        if ( WorldQuery::raycast( *pManager, from, from + cellStep, pOwner->getObjectID(), hit ) )
            return false;
        _from      = pManager->getWorldOrigin().toAbsolute( from ); // 저장하는 자리라 절대다 — 되돌릴 때의 원점이 다를 수 있다
        _to        = pManager->getWorldOrigin().toAbsolute( from + cellStep );
        _moveSteps = TickIntervalUtil::secondsToCount( kTickInterval, _moveTime, 1 );
        _stepsLeft = _moveSteps;
        return true;
    }

    void PushBlockComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _usedSubscription = InteractableComponent::listenCompleted<&PushBlockComponent::handleUsed>( *this ); // 상호작용이 없는 블록은 코드(`push`)가 민다
    }

    void PushBlockComponent::handleUsed( const GameObject& user )
    {
        const SceneComponent* pScene  = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        const SceneComponent* pPusher = user.getPrimarySceneComponent();
        if ( pScene != nullptr && pPusher != nullptr )
            (void)push( pScene->getWorldPosition() - pPusher->getWorldPosition() ); // 막혔거나 움직이는 중이면 밀리지 않을 뿐이다
    }

    void PushBlockComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void PushBlockComponent::stepOnce()
    {
        SceneComponent* pScene = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr )
            return;
        if ( _stepsLeft <= 0 )
            return;
        --_stepsLeft;
        const float32 alpha = 1.0f - static_cast<float32>( _stepsLeft ) / static_cast<float32>( MathUtil::max( 1, _moveSteps ) );
        pScene->setWorldPosition( getOwner()->getManager()->getWorldOrigin().toLocal( float3::lerp( _from, _to, alpha ) ) );
    }
} // namespace sw
