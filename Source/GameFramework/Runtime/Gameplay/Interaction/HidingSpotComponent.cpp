#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Interaction/HidingSpotComponent.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectComponent.h"

namespace sw
{
    namespace
    {
        struct HidingSpotComponentInternal
        {
            // 리터럴 태그다 — 스텝마다 부르는 곳에서 문자열을 intern 하지 않는다.
            static TagID           getHiddenTag() { return AIStimulusSourceComponent::getHiddenTag(); }
            static constexpr TagID getHideActivityTag() { return "Activity.Hide"_tag; }
            /** @brief 상호작용이 닿는 거리의 이 배율 밖이면 걸어 나간 것이다(진행 중 상호작용의 유지 배율과 같은 값 — `InteractorComponent`). */
            static constexpr float32 kKeepDistanceScale = 1.25f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    HidingSpotComponent::HidingSpotComponent()
        : _listOccupant{}
        , _usedSubscription{}
    {
        setCanEverTick( false ); // 숨은 쪽이 있는 동안만 켠다
    }

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
        VectorUtil::addUnique( _listOccupant, who.getHandle() );
        setCanEverTick( true );
        return true;
    }

    bool HidingSpotComponent::exit( GameObject& who )
    {
        GameObject*            pOwner = getOwner();
        SmartObjectComponent*  pSmart = pOwner != nullptr ? pOwner->getComponent<SmartObjectComponent>() : nullptr;
        const GameObjectHandle handle = who.getHandle();
        if ( VectorUtil::contains( _listOccupant, handle ) == false )
            return false;
        VectorUtil::removeIf( _listOccupant, [handle]( const GameObjectHandle& occupant )
        { return occupant == handle; } );
        setCanEverTick( _listOccupant.empty() == false );
        if ( pSmart != nullptr )
            (void)pSmart->releaseSlot( who ); // 차지가 다른 길로 이미 풀렸을 수 있다 — 태그는 이 컴포넌트가 붙인 것이라 늘 뗀다
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
        setCanEverTick( _listOccupant.empty() == false );                                                      // 숨은 채 되살아났다
        _usedSubscription = InteractableComponent::listenCompleted<&HidingSpotComponent::handleUsed>( *this ); // 상호작용이 없는 곳은 코드(`enter` · `exit`)가 부른다
    }

    void HidingSpotComponent::onEndPlay()
    {
        GameObjectManager* const       pManager     = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        const vector<GameObjectHandle> listOccupant = _listOccupant;
        for ( const GameObjectHandle& occupant : listOccupant )
        {
            GameObject* const pWho = pManager != nullptr ? pManager->resolveGameObject( occupant ) : nullptr;
            if ( pWho != nullptr )
                (void)exit( *pWho );
        }
        _listOccupant.clear();
        _usedSubscription.reset();
        Component::onEndPlay();
    }

    void HidingSpotComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const GameObjectManager& manager = context.getManager();
        const bool               bGone   = VectorUtil::containsIf( _listOccupant, [this, &manager]( const GameObjectHandle& occupant )
                        {
            const GameObject* const pWho = manager.resolveGameObject( occupant );
            return pWho == nullptr || hasWalkedAway( *pWho );
        } );
        if ( bGone )
            context.deferPostTickOnComponent( *this, []( HidingSpotComponent& component )
            { component.releaseGoneOccupants(); } );
    }

    bool HidingSpotComponent::hasWalkedAway( const GameObject& who ) const
    {
        const InteractableComponent* const pInteractable = getOwner() != nullptr ? getOwner()->getComponent<InteractableComponent>() : nullptr;
        const InteractionDef* const        pDef          = pInteractable != nullptr ? pInteractable->getDefinition() : nullptr;
        const SceneComponent* const        pSpot         = getOwner() != nullptr ? getOwner()->getPrimarySceneComponent() : nullptr;
        const SceneComponent* const        pRoot         = who.getPrimarySceneComponent();
        if ( pDef == nullptr || pSpot == nullptr || pRoot == nullptr )
            return false; // 거리를 정한 상호작용이 없는 곳은 코드가 내보낸다
        return float3::getDistance( pSpot->getWorldPosition(), pRoot->getWorldPosition() ) > pDef->_maxDistance * HidingSpotComponentInternal::kKeepDistanceScale;
    }

    void HidingSpotComponent::releaseGoneOccupants()
    {
        GameObjectManager* const       pManager     = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        const vector<GameObjectHandle> listOccupant = _listOccupant;
        for ( const GameObjectHandle& occupant : listOccupant )
        {
            GameObject* const pWho = pManager != nullptr ? pManager->resolveGameObject( occupant ) : nullptr;
            if ( pWho == nullptr )
                VectorUtil::removeIf( _listOccupant, [&occupant]( const GameObjectHandle& other )
                { return other == occupant; } );
            else if ( hasWalkedAway( *pWho ) )
                (void)exit( *pWho );
        }
        setCanEverTick( _listOccupant.empty() == false );
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
