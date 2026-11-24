#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/TriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/ObjectIDAllocator.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Condition/IGameFlagsOwner.h"

namespace sw
{
    void TriggerSpawnWindow::markAppeared( const GameObjectManager& manager )
    {
        _appearedFrame     = manager.getRunningUpdateCount();
        _appearedStep      = manager.getSubsystem<ScenePhysics>().getStepCount();
        _watermarkFrame    = _appearedFrame;
        _watermarkPrevious = manager.getObjectIDAllocator().peekNextObjectID();
        _watermarkCurrent  = _watermarkPrevious;
    }

    void TriggerSpawnWindow::advance( const GameObjectManager& manager )
    {
        // 창 하나 전에 적은 다음 id 보다 크거나 같은 오브젝트는 창 둘 안에 생겼다.
        if ( manager.getRunningUpdateCount() < _watermarkFrame + kFrames )
            return;
        _watermarkPrevious = _watermarkCurrent;
        _watermarkCurrent  = manager.getObjectIDAllocator().peekNextObjectID();
        _watermarkFrame    = manager.getRunningUpdateCount();
    }

    bool TriggerSpawnWindow::isOverlapSpawnEntry( const GameObjectManager& manager, const OverlapInfo& overlap ) const
    {
        // 볼륨 쪽: 나타난 뒤 첫 물리 스텝의 진입. 활성자 쪽: 물리가 상대 첫 바디의 첫 스텝 진입을 표시했다 — 둘 다 스텝으로 재고 프레임 수를 가정하지 않는다.
        const bool bVolumeAppeared = manager.getSubsystem<ScenePhysics>().getStepCount() <= _appearedStep + 1;
        return bVolumeAppeared || overlap._bOtherSpawnedInside == SW_TRUE;
    }

    bool TriggerSpawnWindow::isSampledSpawnEntry( const GameObjectManager& manager, const GameObject& activator ) const
    {
        // 볼륨 쪽: 나타났을 때 이미 안에 있던 것만 — 첫 표본 프레임에서 온 진입이다.
        const bool bVolumeAppeared = manager.getRunningUpdateCount() <= _appearedFrame + 1;
        const bool bActivatorNew   = activator.getObjectID() >= _watermarkPrevious;
        return bVolumeAppeared || bActivatorNew;
    }

    TriggerComponent::TriggerComponent( TriggerAuthority authority )
        : _activatorTag{}
        , _activator{}
        , _radius{ 0.0f }
        , _fireCount{ 0 }
        , _progressFlag{}
        , _spawnWindow{}
        , _authority{ authority }
        , _bOnce{ true }
        , _spawnInside{ TriggerSpawnInside::Activate }
        , _bActivatorInside{ false }
    {
        setCanEverTick( true );
    }

    void TriggerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        markAppeared();
    }

    void TriggerComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( isActive() == false )
        {
            _bActivatorInside = false; // 꺼진 볼륨(꺼진 컴포넌트가 틱을 받는 구성이어도)은 재지 않고 안의 활성자를 내보낸다
            return;
        }
        const GameObjectManager& manager = context.getManager();
        if ( _spawnInside == TriggerSpawnInside::Ignore )
            _spawnWindow.advance( manager );                                              // 거리 판정의 나타남 창(`isSampledSpawnEntry`)
        const bool bSpentHere        = _bOnce && _progressFlag.empty() && _fireCount > 0; // 플래그로 적는 볼륨은 활성자마다 다를 수 있어(플레이어별 기록) 계속 잰다
        const bool bNothingToMeasure = _radius <= 0.0f || bSpentHere;
        if ( bNothingToMeasure )
            return;
        const GameObject*     pOwner     = getOwner();
        const GameObject*     pActivator = manager.resolveGameObject( _activator );
        const SceneComponent* pSelf      = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const SceneComponent* pOther     = pActivator != nullptr ? pActivator->getPrimarySceneComponent() : nullptr;
        if ( pSelf == nullptr || pOther == nullptr || pActivator->isActiveInHierarchy() == false )
        {
            _bActivatorInside = false; // 활성자가 사라졌거나 꺼졌다(파괴 · 로그아웃 · 비활성) — 나간 것이다. 다시 생겨 안이면 들어오는 순간이 된다
            return;
        }
        const bool bInside  = float3::getDistance( pSelf->getWorldPosition(), pOther->getWorldPosition() ) <= _radius;
        const bool bEntered = bInside && _bActivatorInside == false;
        _bActivatorInside   = bInside;
        // 나타남의 진입을 받지 않는 볼륨은 안에 있는 것으로만 적는다 — 나갔다 다시 들어올 때 들어온 것이다.
        const bool bSpawnIgnored = bEntered && _spawnInside == TriggerSpawnInside::Ignore && _spawnWindow.isSampledSpawnEntry( manager, *pActivator );
        if ( bEntered == false || bSpawnIgnored )
            return;
        context.deferPostTickOnComponent( *this, []( TriggerComponent& volume, GameObjectManager& deferredManager )
        { (void)volume.activate( deferredManager.resolveGameObject( volume._activator ) ); } );
    }

    void TriggerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&TriggerComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void TriggerComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다. 거리로 재는 활성자는 거리 판정만 들인다(몸도 있으면 두 길이 한 진입을 두 번 낸다).
        if ( overlap._bOtherTrigger == SW_TRUE || overlap._pOther == nullptr )
            return;
        if ( _radius > 0.0f && overlap._pOther->getHandle() == _activator )
            return;
        // 나타남의 진입을 받지 않는 볼륨 — 이 쌍의 다음 진입은 이탈 뒤에만 오므로(트리거 계약) 따로 기억할 것이 없다.
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        if ( _spawnInside == TriggerSpawnInside::Ignore && pManager != nullptr && _spawnWindow.isOverlapSpawnEntry( *pManager, overlap ) )
            return;
        (void)activate( overlap._pOther );
    }

    void TriggerComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        // 꺼진 볼륨은 안의 활성자를 내보낸다 — 다시 켜면 안에 있는 활성자가 들어오는 순간이 된다(물리 트리거가 몸을 놓고 다시 얹는 것과 같다). 다시 켜짐은 나타남이다.
        _bActivatorInside = false;
        markAppeared();
    }

    void TriggerComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( propertyName == getActivePropertyName() )
        {
            _bActivatorInside = false; // 컴포넌트를 끄고 켜도 위와 같다
            markAppeared();
        }
    }

    bool TriggerComponent::hasFired() const
    {
        if ( _fireCount > 0 )
            return true;
        const GameFlags* pFlags = _progressFlag.empty() ? nullptr : findProgressFlags( nullptr );
        return pFlags != nullptr && pFlags->hasFlag( _progressFlag );
    }

    bool TriggerComponent::activate( const GameObject* pActivator )
    {
        // 진행 플래그가 없으면 한 번은 이 볼륨의 발동 수로, 있으면 그 플래그로 정한다(플레이어별 기록이면 활성자마다 따로).
        if ( pActivator == nullptr || ( _bOnce && _progressFlag.empty() && _fireCount > 0 ) || hasActivationAuthority() == false )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        GameFlags* pFlags = _progressFlag.empty() ? nullptr : findProgressFlags( pActivator );
        if ( _progressFlag.empty() == false && pFlags == nullptr )
            return false; // 오류는 찾기가 남겼다 — 진행을 적을 곳이 없으면 일하지 않는다(볼륨의 발동 수로 대신하지 않는다)
        if ( _bOnce && pFlags != nullptr && pFlags->hasFlag( _progressFlag ) )
            return false; // 이미 지난 진행(세이브에서 돌아온 플래그 · 같은 플래그의 다른 볼륨)
        if ( onVolumeActivated( *pActivator ) == false )
            return false;
        ++_fireCount;
        if ( pFlags != nullptr )
            (void)pFlags->addFlag( _progressFlag, 1 );
        return true;
    }

    void TriggerComponent::setProximity( GameObjectHandle activator, float32 radius )
    {
        _activator = activator;
        _radius    = radius;
    }

    void TriggerComponent::resetActivation( TagID activatorTag )
    {
        _activatorTag     = activatorTag;
        _fireCount        = 0;
        _bActivatorInside = false;
    }

    bool TriggerAuthorityRule::hasAuthority( TriggerAuthority authority, const GameObjectManager& manager )
    {
        if ( authority == TriggerAuthority::Local )
            return true;
        // 서버 판정 — 클라이언트 월드도 같은 트리거 계약으로 진입을 내지만 일하지 않는다(서버가 낸 결과를 복제로 받는다).
        const SceneNetLink& link = manager.getSubsystem<SceneNetLink>();
        return link.isBound() == false || link.get()._role != NetLinkRole::Client;
    }

    bool TriggerComponent::hasActivationAuthority() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        return pManager == nullptr || TriggerAuthorityRule::hasAuthority( _authority, *pManager ); // 월드 밖(등록 전)은 넷 링크가 없는 것과 같다
    }

    GameFlags* TriggerComponent::findProgressFlags( const GameObject* pActivator ) const
    {
        (void)pActivator; // 바탕은 세션 기록이다 — 플레이어별 기록은 파생이 고른다(`SequenceTriggerComponent::_audience`)
        const GameObject* pOwner = getOwner();
        GameFlags*        pFlags = IGameFlagsOwner::findOpen( pOwner != nullptr ? pOwner->getManager() : nullptr ); // 감시 값 `Session.Flag` 와 같은 월드 상태(씬에 하나)
        if ( pFlags == nullptr )
        {
            SW_LOG_ERROR_ID( "Trigger.ProgressFlagNoSession", "Trigger '%#' records progress flag '%#' but the scene has no open GameStateComponent - it does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", _progressFlag.c_str() );
        }
        return pFlags;
    }

    void TriggerComponent::markAppeared()
    {
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        if ( pManager != nullptr )
            _spawnWindow.markAppeared( *pManager );
    }
} // namespace sw
