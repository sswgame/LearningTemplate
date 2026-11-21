#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/HidingSpotComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Trigger/GimmickSensorComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"

namespace sw
{
    namespace
    {
        struct HidingSpotComponentInternal
        {
            // 리터럴 태그다 — 스텝마다 부르는 곳에서 문자열을 intern 하지 않는다.
            static constexpr TagID getHiddenTag() { return "State.Hidden"_tag; }
            static constexpr TagID getHideActivityTag() { return "Activity.Hide"_tag; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HidingSpotComponent::HidingSpotComponent() = default;

    bool HidingSpotComponent::enter( GameObject& who )
    {
        GameObject*           pOwner = getOwner();
        SmartObjectComponent* pSmart = pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
        if ( pSmart == nullptr )
            return false;
        TagContainer hide;
        hide.addTag( HidingSpotComponentInternal::getHideActivityTag() );
        if ( pSmart->claimFreeSlot( who, hide ).has_value() == false )
            return false;
        who.addTag( HidingSpotComponentInternal::getHiddenTag() );
        return true;
    }

    bool HidingSpotComponent::exit( GameObject& who )
    {
        GameObject*           pOwner = getOwner();
        SmartObjectComponent* pSmart = pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
        if ( pSmart == nullptr || pSmart->releaseSlot( who ) == false )
            return false;
        who.removeTag( HidingSpotComponentInternal::getHiddenTag() );
        return true;
    }

    bool HidingSpotComponent::isHidden( const GameObject& object )
    {
        return object.hasTag( HidingSpotComponentInternal::getHiddenTag(), true );
    }

    void HidingSpotComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*             pOwner           = getOwner();
        GimmickSensorComponent* pSensor          = pOwner != nullptr ? pOwner->getComponent<GimmickSensorComponent>() : nullptr;
        const bool              bNoUsesAvailable = pSensor == nullptr || pSensor->consumeUses() <= 0.0f;
        if ( bNoUsesAvailable )
            return;
        // 숨기 · 나오기는 사용한 쪽(남의 오브젝트)의 태그 · 자리를 바꾼다 — 틱 뒤에 한다. 숨은 쪽이 다시 사용하면 나오고, 아니면 들어간다.
        context.deferPostTickOnComponent( *this, []( HidingSpotComponent& spot, GameObjectManager& manager )
        {
            const InteractableComponent* pInteractable = spot.getOwner()->getComponent<InteractableComponent>();
            GameObject*                  pWho          = pInteractable != nullptr ? manager.resolveGameObject( pInteractable->getLastInteractor() ) : nullptr;
            if ( pWho == nullptr )
                return;
            if ( spot.exit( *pWho ) == false )
                (void)spot.enter( *pWho );
        } );
    }
} // namespace sw
