#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    ActivatorVolumeComponent::ActivatorVolumeComponent()
        : _activatorTag{}
        , _activator{}
        , _radius{ 0.0f }
        , _fireCount{ 0 }
        , _bOnce{ true }
        , _bActivatorInside{ false }
    {
        setCanEverTick( true );
    }

    void ActivatorVolumeComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( isActive() == false )
        {
            _bActivatorInside = false; // 꺼진 볼륨(꺼진 컴포넌트가 틱을 받는 구성이어도)은 재지 않고 안의 활성자를 내보낸다
            return;
        }
        const bool bNothingToMeasure = _radius <= 0.0f || ( _bOnce && _fireCount > 0 );
        if ( bNothingToMeasure )
            return;
        const GameObject*     pOwner     = getOwner();
        const GameObject*     pActivator = context.getManager().resolveGameObject( _activator );
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
        if ( bEntered == false )
            return;
        context.deferPostTickOnComponent( *this, []( ActivatorVolumeComponent& volume, GameObjectManager& manager )
        { (void)volume.activate( manager.resolveGameObject( volume._activator ) ); } );
    }

    void ActivatorVolumeComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다. 거리로 재는 활성자는 거리 판정만 들인다(몸도 있으면 두 길이 한 진입을 두 번 낸다).
        if ( overlap._bOtherTrigger == SW_TRUE || overlap._pOther == nullptr )
            return;
        if ( _radius > 0.0f && overlap._pOther->getHandle() == _activator )
            return;
        (void)activate( overlap._pOther );
    }

    void ActivatorVolumeComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        _bActivatorInside = false; // 꺼진 볼륨은 안의 활성자를 내보낸다 — 다시 켜면 안에 있는 활성자가 들어오는 순간이 된다(물리 트리거가 몸을 놓고 다시 얹는 것과 같다)
    }

    void ActivatorVolumeComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        if ( propertyName == getActivePropertyName() )
            _bActivatorInside = false; // 컴포넌트를 끄고 켜도 위와 같다
    }

    bool ActivatorVolumeComponent::activate( const GameObject* pActivator )
    {
        if ( pActivator == nullptr || ( _bOnce && _fireCount > 0 ) )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        if ( onVolumeActivated( *pActivator ) == false )
            return false;
        ++_fireCount;
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
} // namespace sw
