#include "pch.h"

#include "Games/Playground/Velmora/Client/VelmoraEncounterViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

namespace sw
{
    VelmoraEncounterViewComponent::VelmoraEncounterViewComponent()
        : _attackAction{}
        , _dashAction{}
        , _spherePrefab{}
        , _roomLook{}
        , _invulnerableRadius{ 0.34f }
        , _invulnerableHeight{ 0.5f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _encounter{}
        , _sphereViews{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    VelmoraEncounterViewComponent::~VelmoraEncounterViewComponent() = default;

    void VelmoraEncounterViewComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                              pOwner     = getOwner();
        const VelmoraEncounterGameModeComponent* const pEncounter = pOwner != nullptr ? pOwner->getComponent<VelmoraEncounterGameModeComponent>() : nullptr;
        if ( pEncounter == nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.ViewWithoutEncounter", "Velmora encounter view on '%#': no VelmoraEncounterGameModeComponent in front of it on the same object - the view does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _encounter = TComponentHandle<VelmoraEncounterGameModeComponent>{};
            return;
        }
        _encounter = TComponentHandle<VelmoraEncounterGameModeComponent>( pEncounter );
    }

    void VelmoraEncounterViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _attackAction.empty() == false && _dashAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "Velmora.ViewActionMissing", "Velmora encounter view: attack '%#' and dash '%#' actions must both be named - the view reads no input", _attackAction.c_str(), _dashAction.c_str() );
    }

    void VelmoraEncounterViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void VelmoraEncounterViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 전투 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( VelmoraEncounterViewComponent& view, GameObjectManager& manager )
        { view.updateView( manager ); } );
    }

    void VelmoraEncounterViewComponent::updateView( GameObjectManager& manager )
    {
        VelmoraEncounterGameModeComponent* const pEncounter = manager.resolveComponent( _encounter );
        if ( pEncounter == nullptr || pEncounter->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            (void)pEncounter->submitCombatInput( input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) ); // 열린 판인지는 위에서 보았다
        }
        showRoom( *pEncounter, manager );
    }

    void VelmoraEncounterViewComponent::showRoom( const VelmoraEncounterGameModeComponent& encounter, GameObjectManager& manager )
    {
        _sphereViews.begin();
        if ( encounter.isFighting() )
        {
            _roomLook.forEachSphere( encounter.getRoom(), [this, &manager]( const AbsolutePosition& position, float32 radius, const float4& color )
            { (void)_sphereViews.show( manager, _spherePrefab, position, float3{ radius * 2.0f }, 0.0f, color ); } );
            const AdventureGameModeComponent* const pDirector = encounter.getRoom().isPlayerInvulnerable() ? PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this ) : nullptr;
            if ( pDirector != nullptr )
                (void)_sphereViews.show( manager, _spherePrefab, ActionRoomLook::makePosition( pDirector->getPlayerPosition(), _invulnerableHeight ), float3{ _invulnerableRadius * 2.0f }, 0.0f,
                                         _invulnerableColor );
        }
        _sphereViews.end( manager );
    }
} // namespace sw
