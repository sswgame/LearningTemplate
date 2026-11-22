#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

#include "Core/Event/EventDispatcher.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractionAuthority.h"

namespace sw
{
    namespace
    {
        /** @brief 퀘스트 일지를 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 형제 `PlayerStateComponent` 와 같다). */
        const StatefulComponentRegistrar<QuestLogComponent> s_questLogStateRegistration{ "QuestLogComponent" };
    } // namespace

    QuestLogComponent::QuestLogComponent()
        : _questLog{}
        , _restore{}
        , _ownerDiedSubscription{}
        , _interactionCompletedSubscription{}
        , _bOpenAtBeginPlay{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    QuestLogComponent::~QuestLogComponent() = default;

    GameStateInitResult QuestLogComponent::initialize( const QuestCatalog* pCatalog )
    {
        if ( _restore.isOpen() )
            return GameStateInitResult::AlreadyInitialized; // 앞 디렉터가 연 일지를 다시 비우지 않는다
        _questLog.initialize( pCatalog );
        return _restore.open( *this );
    }

    void QuestLogComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<QuestLogComponent>( this );
        // 퀘스트 진행은 선택적 일대다 알림이라 이벤트 버스로 받는다(docs/agents/Gameplay.md "시스템 사이 알림") — 일지 주인이 구독해야 플레이어마다 따로 센다.
        EventDispatcher* const pDispatcher = game::findSubsystem<EventDispatcher>();
        if ( pDispatcher == nullptr )
            return;
        _ownerDiedSubscription            = pDispatcher->subscribe<AbilityOwnerDiedEvent>( gameEventChannel(),
                                                                                           SW_DELEGATE_METHOD( Delegate<void( const AbilityOwnerDiedEvent& )>, &QuestLogComponent::handleOwnerDied, this ) );
        _interactionCompletedSubscription = pDispatcher->subscribe<InteractionCompletedEvent>(
            gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const InteractionCompletedEvent& )>, &QuestLogComponent::handleInteractionCompleted, this ) );
    }

    void QuestLogComponent::onUnregister( GameObjectManager& manager )
    {
        _ownerDiedSubscription            = Subscription{};
        _interactionCompletedSubscription = Subscription{};
        manager.getComponentRegistry().remove<QuestLogComponent>( this );
        Component::onUnregister( manager );
    }

    void QuestLogComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _bOpenAtBeginPlay == SW_TRUE )
            (void)initialize( nullptr ); // 카탈로그가 없어도 거절하지 않는다 — 결과는 새 판 · 복원 · 이미 열림뿐이다
    }

    bool QuestLogComponent::isOwnedPawn( const GameObjectHandle actor ) const
    {
        const GameObject* const        pOwner   = getOwner();
        const GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject* const        pActor   = pManager != nullptr ? pManager->resolveGameObject( actor ) : nullptr;
        return pActor != nullptr && PawnComponent::findControllerObject( *pManager, *pActor ) == pOwner;
    }

    void QuestLogComponent::handleOwnerDied( const AbilityOwnerDiedEvent& event )
    {
        if ( _restore.isOpen() == false || isOwnedPawn( event._instigator ) == false )
            return;
        const GameObjectManager* const pManager = getOwner()->getManager();
        const GameObject* const        pTarget  = pManager->resolveGameObject( event._target );
        if ( pTarget == nullptr )
            return;
        static const hashed_string s_defeatKind( kDefeatKind );
        for ( const TagID tag : pTarget->getTags().getTags() )
        {
            (void)_questLog.notify( s_defeatKind, hashed_string( tag.getString() ) );
        }
    }

    void QuestLogComponent::handleInteractionCompleted( const InteractionCompletedEvent& event )
    {
        if ( _restore.isOpen() == false || event._request._interaction.empty() || isOwnedPawn( event._request._interactor ) == false )
            return;
        static const hashed_string s_interactKind( kInteractKind );
        (void)_questLog.notify( s_interactKind, event._request._interaction );
    }

    void QuestLogComponent::serializeState( StateIO& io )
    {
        _restore.serialize( io, kStateTag, kStateVersion, _questLog, "the quest log" );
    }

    void QuestLogComponent::restoreState( vector<uint8>&& bytes )
    {
        _restore.receive( *this, std::move( bytes ) );
    }
} // namespace sw
