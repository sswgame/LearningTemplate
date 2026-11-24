#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"

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
    SensorWeightOverrideComponent::SensorWeightOverrideComponent()
        : _weight{ 1.0f }
    {
    }

    GimmickSensorComponent::GimmickSensorComponent()
        : _requiredTags{}
        , _listOccupant{}
        , _listBodyOccupant{}
        , _listTriggerOccupant{}
        , _defaultWeight{ 1.0f }
        , _bCountTriggers{ false }
        , _pendingDamage{ 0.0f }
        , _pendingUse{ 0.0f }
        , _seenCompletedCount{ 0 }
        , _signal{ 0.0f }
    {
    }

    bool GimmickSensorComponent::canAcceptOccupant( const GameObject& object ) const
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

    void GimmickSensorComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        const bool bIgnoredTrigger = overlap._bOtherTrigger == SW_TRUE && _bCountTriggers == false;
        const bool bIgnoreOverlap  = overlap._pOther == nullptr || bIgnoredTrigger || canAcceptOccupant( *overlap._pOther ) == false;
        if ( bIgnoreOverlap )
            return;
        (void)VectorUtil::addUnique( overlap._bOtherTrigger == SW_TRUE ? _listTriggerOccupant : _listBodyOccupant, overlap._pOther->getHandle() );
        addOccupant( *overlap._pOther );
    }

    void GimmickSensorComponent::handleOverlapEnd( const OverlapInfo& overlap )
    {
        // 세지 않은 상대 트리거(감지 범위)의 이탈은 몸의 겹침이 아니다 — 몸이 아직 안이면 빼지 않는다(진입과 같은 거름).
        if ( overlap._bOtherTrigger == SW_TRUE && _bCountTriggers == false )
            return;
        if ( overlap._pOther != nullptr )
        {
            // 몸과 트리거를 따로 센다 — 한쪽이 나가도 다른 쪽이 안이면 남는다.
            const GameObjectHandle    handle    = overlap._pOther->getHandle();
            vector<GameObjectHandle>& listKind  = overlap._bOtherTrigger == SW_TRUE ? _listTriggerOccupant : _listBodyOccupant;
            vector<GameObjectHandle>& listOther = overlap._bOtherTrigger == SW_TRUE ? _listBodyOccupant : _listTriggerOccupant;
            (void)VectorUtil::removeSingleSwap( listKind, handle );
            if ( VectorUtil::contains( listOther, handle ) == false )
                removeOccupant( handle );
            return;
        }
        // 상대가 사라졌으므로 살아 있지 않은 핸들을 모두 뺀다.
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        const auto isGone = [pManager]( const GameObjectHandle& handle )
        { return pManager->resolveGameObject( handle ) == nullptr; };
        VectorUtil::removeIf( _listOccupant, isGone );
        VectorUtil::removeIf( _listBodyOccupant, isGone );
        VectorUtil::removeIf( _listTriggerOccupant, isGone );
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
            const SensorWeightOverrideComponent* pWeight = pOccupant->getComponent<SensorWeightOverrideComponent>();
            const RigidBodyComponent*            pBody3D = pOccupant->getComponent<RigidBodyComponent>();
            const RigidBody2DComponent*          pBody2D = pOccupant->getComponent<RigidBody2DComponent>();
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

    GimmickSensorImpulse GimmickSensorComponent::peekDamage() const
    {
        GimmickSensorImpulse read;
        read._pending = _pendingDamage.load( std::memory_order_relaxed );
        read._amount  = read._pending;
        return read;
    }

    GimmickSensorImpulse GimmickSensorComponent::peekUses() const
    {
        const GameObject*            pOwner        = getOwner();
        const InteractableComponent* pInteractable = pOwner != nullptr ? pOwner->getComponent<InteractableComponent>() : nullptr;
        GimmickSensorImpulse         read;
        read._pending        = _pendingUse.load( std::memory_order_relaxed );
        read._completedCount = pInteractable != nullptr ? pInteractable->getCompletedCount() : _seenCompletedCount.load( std::memory_order_relaxed );
        read._amount         = read._pending + static_cast<float32>( read._completedCount - _seenCompletedCount.load( std::memory_order_relaxed ) );
        return read;
    }

    void GimmickSensorComponent::acknowledgeDamage( const GimmickSensorImpulse& read )
    {
        addAtomic( _pendingDamage, -read._pending );
    }

    void GimmickSensorComponent::acknowledgeUses( const GimmickSensorImpulse& read )
    {
        addAtomic( _pendingUse, -read._pending );
        _seenCompletedCount.store( read._completedCount, std::memory_order_relaxed );
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct GimmickSensorWatchesInternal
        {
            /** @brief 감시 값 `Gimmick.OccupantWeight` 입니다. 대상 센서에 겹친 것들의 무게 합(kg — `SensorWeightOverrideComponent`, 강체 질량, 기본 무게 순)입니다. 눌림판이 읽는 값과 같다. */
            [[nodiscard]] static bool readOccupantWeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GimmickSensorComponent* pSensor = query.findTargetComponent<GimmickSensorComponent>( "gimmick sensor", outValue );
                if ( pSensor == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pSensor->computeOccupantWeight() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( gimmickOccupantWeight, "Gimmick.OccupantWeight", "Weight of what overlaps the target's gimmick sensor (kg, the pressure plate's reading)",
                               &GimmickSensorWatchesInternal::readOccupantWeight, true, false );

    void GimmickSensorComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapBeginSubscription = GameObjectEvents::listen<&GimmickSensorComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
        _overlapEndSubscription   = GameObjectEvents::listen<&GimmickSensorComponent::handleOverlapEnd>( *this, &GameObjectEvents::_overlapEnd );
    }
} // namespace sw
