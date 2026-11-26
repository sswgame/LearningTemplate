#include "pch.h"

#include "Games/Playground/Velmora/Client/VelmoraEncounterViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

namespace sw
{
    VelmoraEncounterViewComponent::VelmoraEncounterViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&VelmoraEncounterViewComponent::updateView, &VelmoraEncounterViewComponent::submitInput>() )
        , _attackAction{}
        , _dashAction{}
        , _spherePrefab{}
        , _roomLook{}
        , _invulnerableRadius{ 0.34f }
        , _invulnerableHeight{ 0.5f }
        , _invulnerableColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _sphereViews{}
    {
    }

    VelmoraEncounterViewComponent::~VelmoraEncounterViewComponent() = default;

    void VelmoraEncounterViewComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _sphereViews.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void VelmoraEncounterViewComponent::updateView( GameObjectManager& manager, VelmoraEncounterGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showRoom( director, manager );
    }

    void VelmoraEncounterViewComponent::submitInput( const GameModeInput& input, VelmoraEncounterGameModeComponent& director )
    {
        (void)director.submitCombatInput( input.wasActionTriggered( _attackAction ), input.wasActionTriggered( _dashAction ) ); // 열린 판인지는 위에서 보았다
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
