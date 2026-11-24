#include "pch.h"

#include "Games/Playground/Velmora/Client/VelmoraEncounterViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoomDebugDraw.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

namespace sw
{
    namespace
    {
        struct VelmoraEncounterViewComponentInternal
        {
            static constexpr float4  kInvulnerableColor  = float4( 0.6f, 0.9f, 1.0f, 1.0f ); ///< 무적인 플레이어를 감싸는 공
            static constexpr float32 kInvulnerableHeight = 0.5f;
            static constexpr float32 kInvulnerableRadius = 0.34f; ///< 키트가 그리는 플레이어 공보다 조금 크다
        };
    } // namespace

    VelmoraEncounterViewComponent::VelmoraEncounterViewComponent()
        : _attackAction{}
        , _dashAction{}
        , _encounter{}
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
        DebugDrawQueue* const pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr || pEncounter->isFighting() == false )
            return;
        using Internal            = VelmoraEncounterViewComponentInternal;
        const WorldOrigin& origin = manager.getWorldOrigin();
        ActionRoomDebugDraw::draw( *pDraw, origin, pEncounter->getRoom() );
        const AdventureGameModeComponent* const pDirector = pEncounter->getRoom().isPlayerInvulnerable() ? PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this ) : nullptr;
        if ( pDirector != nullptr )
            pDraw->drawSphere( origin, ActionRoomDebugDraw::makePosition( pDirector->getPlayerPosition(), Internal::kInvulnerableHeight ), Internal::kInvulnerableRadius, Internal::kInvulnerableColor );
    }
} // namespace sw
