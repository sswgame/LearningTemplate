#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/ObjectIDAllocator.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlagsOwnerComponent.h"

namespace sw
{
    ActivatorVolumeComponent::ActivatorVolumeComponent()
        : _activatorTag{}
        , _activator{}
        , _radius{ 0.0f }
        , _fireCount{ 0 }
        , _progressFlag{}
        , _appearedFrame{ 0 }
        , _spawnWatermarkFrame{ 0 }
        , _spawnWatermarkPrevious{ 0 }
        , _spawnWatermarkCurrent{ 0 }
        , _bOnce{ true }
        , _spawnInside{ ActivatorSpawnInside::Activate }
        , _bActivatorInside{ false }
    {
        setCanEverTick( true );
    }

    void ActivatorVolumeComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        markAppeared();
    }

    void ActivatorVolumeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( isActive() == false )
        {
            _bActivatorInside = false; // 꺼진 볼륨(꺼진 컴포넌트가 틱을 받는 구성이어도)은 재지 않고 안의 활성자를 내보낸다
            return;
        }
        const GameObjectManager& manager = context.getManager();
        if ( _spawnInside == ActivatorSpawnInside::Ignore && manager.getUpdateCount() >= _spawnWatermarkFrame + kSpawnWindowFrames )
        {
            // 나타남 창을 민다 — 창 하나 전에 적은 다음 id 보다 크거나 같은 오브젝트는 창 둘 안에 생겼다(겹침 진입의 나타남 판정, `isSpawnEntry`).
            _spawnWatermarkPrevious = _spawnWatermarkCurrent;
            _spawnWatermarkCurrent  = manager.getObjectIDAllocator().peekNextObjectID();
            _spawnWatermarkFrame    = manager.getUpdateCount();
        }
        const bool bNothingToMeasure = _radius <= 0.0f || ( _bOnce && _fireCount > 0 );
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
        // 나타남의 진입을 받지 않는 볼륨은 안에 있는 것으로만 적는다 — 나갔다 다시 들어올 때 들어온 것이다. 판정은 겹침 길과 같은 규칙이다(`isSpawnEntry`).
        const bool bSpawnIgnored = bEntered && _spawnInside == ActivatorSpawnInside::Ignore && isSpawnEntry( *pActivator );
        if ( bEntered == false || bSpawnIgnored )
            return;
        context.deferPostTickOnComponent( *this, []( ActivatorVolumeComponent& volume, GameObjectManager& deferredManager )
        { (void)volume.activate( deferredManager.resolveGameObject( volume._activator ) ); } );
    }

    void ActivatorVolumeComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다. 거리로 재는 활성자는 거리 판정만 들인다(몸도 있으면 두 길이 한 진입을 두 번 낸다).
        if ( overlap._bOtherTrigger == SW_TRUE || overlap._pOther == nullptr )
            return;
        if ( _radius > 0.0f && overlap._pOther->getHandle() == _activator )
            return;
        // 나타남의 진입을 받지 않는 볼륨 — 이 쌍의 다음 진입은 이탈 뒤에만 오므로(트리거 계약) 따로 기억할 것이 없다.
        if ( _spawnInside == ActivatorSpawnInside::Ignore && isSpawnEntry( *overlap._pOther ) )
            return;
        (void)activate( overlap._pOther );
    }

    void ActivatorVolumeComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        // 꺼진 볼륨은 안의 활성자를 내보낸다 — 다시 켜면 안에 있는 활성자가 들어오는 순간이 된다(물리 트리거가 몸을 놓고 다시 얹는 것과 같다). 다시 켜짐은 나타남이다.
        _bActivatorInside = false;
        markAppeared();
    }

    void ActivatorVolumeComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( propertyName == getActivePropertyName() )
        {
            _bActivatorInside = false; // 컴포넌트를 끄고 켜도 위와 같다
            markAppeared();
        }
    }

    bool ActivatorVolumeComponent::hasFired() const
    {
        if ( _fireCount > 0 )
            return true;
        const GameFlags* pFlags = findProgressFlags();
        return pFlags != nullptr && pFlags->hasFlag( _progressFlag );
    }

    bool ActivatorVolumeComponent::activate( const GameObject* pActivator )
    {
        if ( pActivator == nullptr || ( _bOnce && _fireCount > 0 ) )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        GameFlags* pFlags = findProgressFlags();
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

    void ActivatorVolumeComponent::setProximity( GameObjectHandle activator, float32 radius )
    {
        _activator = activator;
        _radius    = radius;
    }

    void ActivatorVolumeComponent::resetActivation( TagID activatorTag )
    {
        _activatorTag     = activatorTag;
        _fireCount        = 0;
        _bActivatorInside = false;
    }

    bool ActivatorVolumeComponent::isSpawnEntry( const GameObject& activator ) const
    {
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        if ( pManager == nullptr )
            return false;
        const bool bVolumeAppeared = pManager->getUpdateCount() <= _appearedFrame + kSpawnWindowFrames;
        const bool bActivatorNew   = activator.getObjectID() >= _spawnWatermarkPrevious;
        return bVolumeAppeared || bActivatorNew;
    }

    GameFlags* ActivatorVolumeComponent::findProgressFlags() const
    {
        if ( _progressFlag.empty() )
            return nullptr;
        const GameObject* pOwner = getOwner();
        GameFlags*        pFlags = GameFlagsOwnerComponent::findFirstOpen( pOwner != nullptr ? pOwner->getManager() : nullptr ); // 감시 값 `Session.Flag` 와 같은 판 상태
        if ( pFlags == nullptr )
        {
            SW_LOG_ERROR_ID( "ActivatorVolume.ProgressFlagNoSession", "Activator volume '%#' records progress flag '%#' but the scene has no open SessionStateComponent - it does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?", _progressFlag.c_str() );
        }
        return pFlags;
    }

    void ActivatorVolumeComponent::markAppeared()
    {
        const GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        _appearedFrame          = pManager->getUpdateCount();
        _spawnWatermarkFrame    = _appearedFrame;
        _spawnWatermarkPrevious = pManager->getObjectIDAllocator().peekNextObjectID();
        _spawnWatermarkCurrent  = _spawnWatermarkPrevious;
    }
} // namespace sw
