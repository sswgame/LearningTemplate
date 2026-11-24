#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionCombatAvatarComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    ActionCombatAvatarComponent::ActionCombatAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _dashAction{}
        , _playerView{}
        , _spherePrefab{}
        , _roomLook{}
        , _playerRadius{ 0.28f }
        , _playerHeight{ 0.5f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _director{}
        , _sphereViews{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _bViewReset{ SW_FALSE }
        , _bPlayerViewMissing{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionCombatAvatarComponent::~ActionCombatAvatarComponent() = default;

    void ActionCombatAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                          pOwner    = getOwner();
        const ActionCombatGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ActionCombatGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "ActionCombat.AvatarWithoutGameMode", "Action combat avatar on '%#': no ActionCombatGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ActionCombatGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ActionCombatGameModeComponent>( pDirector );
    }

    void ActionCombatAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _moveAction.empty() == false && _attackAction.empty() == false && _dashAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "ActionCombat.AvatarActionMissing", "Action combat avatar: move '%#', attack '%#' and dash '%#' actions must all be named - the avatar reads no input",
                             _moveAction.c_str(), _attackAction.c_str(), _dashAction.c_str() );
        _bViewReset         = SW_FALSE;
        _bPlayerViewMissing = SW_FALSE;
    }

    void ActionCombatAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void ActionCombatAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( ActionCombatAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void ActionCombatAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        ActionCombatGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput();
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
            (void)pDirector->submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) );
        }
        pushPlayerHealthView( manager, *pDirector ); // 시간이 멈춘 프레임 · 복원 직후에도 넣는다
        showRoom( *pDirector, manager );
    }

    void ActionCombatAvatarComponent::pushPlayerHealthView( GameObjectManager& manager, const ActionCombatGameModeComponent& director )
    {
        if ( _playerView.isValid() == false )
            return;
        GameObject* const              pViewObject = manager.resolveGameObject( _playerView );
        RuleHealthViewComponent* const pView       = pViewObject != nullptr ? pViewObject->getComponent<RuleHealthViewComponent>() : nullptr;
        if ( pView == nullptr )
        {
            if ( _bPlayerViewMissing == SW_FALSE )
                SW_LOG_ERROR_ID( "ActionCombat.PlayerViewMissing", "Action combat avatar: the Player View object has no RuleHealthViewComponent - the player HP bar does not move" );
            _bPlayerViewMissing = SW_TRUE;
            return;
        }
        if ( _bViewReset == SW_FALSE )
        {
            pView->resetSnapshot(); // 새 판의 HP 는 앞 판에서 이어지지 않는다 — 연출 없이 다시 둔다
            _bViewReset = SW_TRUE;
        }
        pView->applySnapshot( static_cast<float32>( director.getPlayerHP() ), static_cast<float32>( director.getPlayerMaxHP() ) );
    }

    void ActionCombatAvatarComponent::showRoom( const ActionCombatGameModeComponent& director, GameObjectManager& manager )
    {
        const ActionRoom&       room  = director.getRoom();
        const ActionCombatPhase phase = director.getCombatPhase();
        _sphereViews.begin();
        (void)_sphereViews.show( manager, _spherePrefab, ActionRoomLook::makePosition( director.getPlayerPosition(), _playerHeight ), float3{ _playerRadius * 2.0f }, 0.0f,
                                 room.isPlayerInvulnerable() ? _invulnerableColor : _playerColor );
        if ( phase == ActionCombatPhase::Hall || phase == ActionCombatPhase::Boss )
        {
            _roomLook.forEachSphere( room, [this, &manager]( const AbsolutePosition& position, float32 radius, const float4& color )
            { (void)_sphereViews.show( manager, _spherePrefab, position, float3{ radius * 2.0f }, 0.0f, color ); } );
        }
        _sphereViews.end( manager );
    }
} // namespace sw
