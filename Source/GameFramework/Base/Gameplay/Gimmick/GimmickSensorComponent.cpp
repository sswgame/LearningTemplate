#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickSensorComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"

namespace sw
{
    namespace
    {
        void addAtomic( atomic<float32>& inoutValue, float32 amount )
        {
            float32 expected = inoutValue.load( std::memory_order_relaxed );
            while ( inoutValue.compare_exchange_weak( expected, expected + amount ) == false )
            {
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    GimmickWeightComponent::GimmickWeightComponent()
        : _weight{ 1.0f }
    {
    }

    GimmickSensorComponent::GimmickSensorComponent()
        : _requiredTags{}
        , _listOccupant{}
        , _defaultWeight{ 1.0f }
        , _bCountTriggers{ false }
        , _pendingDamage{ 0.0f }
        , _pendingUse{ 0.0f }
        , _seenCompletedCount{ 0 }
        , _signal{ 0.0f }
    {
    }

    bool GimmickSensorComponent::acceptsOccupant( const GameObject& object ) const
    {
        return _requiredTags.getTagCount() == 0 || object.getTags().hasAllTags( _requiredTags );
    }

    void GimmickSensorComponent::addOccupant( const GameObject& object )
    {
        const GameObjectHandle handle = object.getHandle();
        for ( const GameObjectHandle& occupant : _listOccupant )
        {
            if ( occupant == handle )
                return;
        }
        _listOccupant.push_back( handle );
    }

    void GimmickSensorComponent::removeOccupant( GameObjectHandle handle )
    {
        for ( size_t index = 0; index < _listOccupant.size(); ++index )
        {
            if ( _listOccupant[index] == handle )
            {
                _listOccupant.erase( _listOccupant.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void GimmickSensorComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        const bool bIgnoredTrigger = overlap._bOtherTrigger == SW_TRUE && _bCountTriggers == false;
        const bool bIgnoreOverlap  = overlap._pOther == nullptr || bIgnoredTrigger || acceptsOccupant( *overlap._pOther ) == false;
        if ( bIgnoreOverlap )
            return;
        addOccupant( *overlap._pOther );
    }

    void GimmickSensorComponent::onOverlapEnd( const OverlapInfo& overlap )
    {
        Component::onOverlapEnd( overlap );
        if ( overlap._pOther != nullptr )
        {
            removeOccupant( overlap._pOther->getHandle() );
            return;
        }
        // 상대가 사라졌으므로 살아 있지 않은 핸들을 모두 뺀다.
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        for ( size_t index = _listOccupant.size(); index > 0; --index )
        {
            if ( pManager->resolveGameObject( _listOccupant[index - 1] ) == nullptr )
                _listOccupant.erase( _listOccupant.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    float32 GimmickSensorComponent::computeOccupantWeight() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return 0.0f;
        float32 total = 0.0f;
        for ( const GameObjectHandle& handle : _listOccupant )
        {
            const GameObject* pOccupant = pManager->resolveGameObject( handle );
            if ( pOccupant == nullptr )
                continue;
            // 데이터에 적힌 무게를 먼저 쓰고, 없으면 강체의 질량(바디가 있으면 바디의 값)을 쓰고, 그것도 없으면 센서의 기본 무게를 쓴다.
            const GimmickWeightComponent* pWeight = pOccupant->getComponent<GimmickWeightComponent>();
            const RigidBodyComponent*     pBody3D = pOccupant->getComponent<RigidBodyComponent>();
            const RigidBody2DComponent*   pBody2D = pOccupant->getComponent<RigidBody2DComponent>();
            if ( pWeight != nullptr )
                total += pWeight->getWeight();
            else if ( pBody3D != nullptr )
                total += pBody3D->getBodyMass() > 0.0f ? pBody3D->getBodyMass() : pBody3D->getMass();
            else if ( pBody2D != nullptr )
                total += pBody2D->getBodyMass() > 0.0f ? pBody2D->getBodyMass() : pBody2D->getMass();
            else
                total += _defaultWeight;
        }
        return total;
    }

    void GimmickSensorComponent::applyDamage( float32 amount )
    {
        if ( amount > 0.0f )
            addAtomic( _pendingDamage, amount );
    }

    void GimmickSensorComponent::notifyUsed()
    {
        addAtomic( _pendingUse, 1.0f );
    }

    float32 GimmickSensorComponent::consumeDamage()
    {
        return _pendingDamage.exchange( 0.0f );
    }

    float32 GimmickSensorComponent::consumeUses()
    {
        // 상호작용은 센서를 모른다. 그래서 같은 오브젝트의 완료 수를 직접 읽고, 지난번 이후 늘어난 만큼을 사용 횟수로 센다.
        const GameObject*            pOwner        = getOwner();
        const InteractableComponent* pInteractable = pOwner != nullptr ? pOwner->getComponent<InteractableComponent>() : nullptr;
        uint32                       newCompleted  = 0;
        if ( pInteractable != nullptr )
        {
            const uint32 completedCount = pInteractable->getCompletedCount();
            newCompleted                = completedCount - _seenCompletedCount.exchange( completedCount );
        }
        return _pendingUse.exchange( 0.0f ) + static_cast<float32>( newCompleted );
    }
} // namespace sw
