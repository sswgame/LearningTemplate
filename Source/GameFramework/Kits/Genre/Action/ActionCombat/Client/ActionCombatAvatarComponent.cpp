#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Client/ActionCombatAvatarComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    ActionCombatAvatarComponent::ActionCombatAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&ActionCombatAvatarComponent::updateAvatar, &ActionCombatAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _dashAction{}
        , _playerView{}
        , _spherePrefab{}
        , _roomLook{}
        , _playerRadius{ 0.28f }
        , _playerHeight{ 0.5f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _sphereViews{}
        , _bViewReset{ SW_FALSE }
        , _bPlayerViewMissing{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionCombatAvatarComponent::~ActionCombatAvatarComponent() = default;

    void ActionCombatAvatarComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _bViewReset         = SW_FALSE;
        _bPlayerViewMissing = SW_FALSE;
    }

    void ActionCombatAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void ActionCombatAvatarComponent::updateAvatar( GameObjectManager& manager, ActionCombatGameModeComponent& director )
    {
        dispatchInput( manager, director );
        pushPlayerHealthView( manager, director ); // 시간이 멈춘 프레임 · 복원 직후에도 넣는다
        showRoom( director, manager );
    }

    void ActionCombatAvatarComponent::submitInput( const GameModeInput& input, ActionCombatGameModeComponent& director )
    {
        // 판이 끝났으면 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        (void)director.submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) );
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
