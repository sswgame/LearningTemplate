#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/HidingSpotComponent.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

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

    void HidingSpotComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _usedSubscription = InteractableComponent::listenCompleted<&HidingSpotComponent::handleUsed>( *this ); // 상호작용이 없는 곳은 코드(`enter` · `exit`)가 부른다
    }

    void HidingSpotComponent::handleUsed( const GameObject& user )
    {
        // 완료는 틱 뒤 게임 스레드에서 온다 — 사용한 쪽(남의 오브젝트)의 태그 · 자리를 바로 바꾼다.
        GameObjectManager* pManager = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        GameObject*        pWho     = pManager != nullptr ? pManager->resolveGameObject( user.getHandle() ) : nullptr;
        if ( pWho != nullptr && exit( *pWho ) == false )
            (void)enter( *pWho ); // 자리가 없으면 들어가지 못할 뿐이다
    }
} // namespace sw
