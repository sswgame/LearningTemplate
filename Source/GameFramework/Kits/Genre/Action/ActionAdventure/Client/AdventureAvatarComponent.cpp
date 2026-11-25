#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Client/AdventureAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    AdventureAvatarComponent::AdventureAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&AdventureAvatarComponent::updateAvatar, &AdventureAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _cubePrefab{}
        , _spherePrefab{}
        , _borderColor{ 0.75f, 0.8f, 0.7f, 1.0f }
        , _borderWidth{ 0.1f }
        , _beaconSize{ 0.6f, 2.0f, 0.6f }
        , _litBeaconColor{ 1.0f, 0.62f, 0.18f, 1.0f }
        , _unlitBeaconColor{ 0.35f, 0.35f, 0.4f, 1.0f }
        , _campRadius{ 0.4f }
        , _campHeight{ 0.2f }
        , _campColor{ 0.95f, 0.4f, 0.1f, 1.0f }
        , _playerRadius{ 0.28f }
        , _playerHeight{ 0.5f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _cubeViews{}
        , _sphereViews{}
    {
    }

    AdventureAvatarComponent::~AdventureAvatarComponent() = default;

    void AdventureAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cubeViews.destroyAll( *pManager );
            _sphereViews.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void AdventureAvatarComponent::updateAvatar( GameObjectManager& manager, AdventureGameModeComponent& director )
    {
        dispatchInput( manager, director );
        showWorld( director, manager );
    }

    void AdventureAvatarComponent::submitInput( const GameModeInput& input, AdventureGameModeComponent& director )
    {
        (void)director.submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _useAction ) ); // 열린 판인지는 위에서 보았다
    }

    void AdventureAvatarComponent::showWorld( const AdventureGameModeComponent& director, GameObjectManager& manager )
    {
        const AdventureRegionCatalog* const pRegions = director.findRegions();
        const GameStateComponent* const     pState   = GameStateComponent::findOnOwner( director );
        if ( pRegions == nullptr || pState == nullptr )
            return;
        _cubeViews.begin();
        _sphereViews.begin();
        for ( const AdventureRegionDef& region : pRegions->getAll() )
        {
            showBorder( region, manager );
            if ( region._beacon.isSet() )
            {
                const bool             bLit = director.getCampaign().isBeaconLit( region, pState->getFlags() );
                const AbsolutePosition center{ float3( region._beacon._position._x, _beaconSize._y * 0.5f, region._beacon._position._y ) };
                (void)_cubeViews.show( manager, _cubePrefab, center, _beaconSize, 0.0f, bLit ? _litBeaconColor : _unlitBeaconColor );
            }
            if ( region._camp.isSet() )
            {
                const AbsolutePosition center{ float3( region._camp._position._x, _campHeight, region._camp._position._y ) };
                (void)_sphereViews.show( manager, _spherePrefab, center, float3{ _campRadius * 2.0f }, 0.0f, _campColor );
            }
        }
        const float2& position = director.getPlayerPosition();
        (void)_sphereViews.show( manager, _spherePrefab, AbsolutePosition{ float3( position._x, _playerHeight, position._y ) }, float3{ _playerRadius * 2.0f }, 0.0f, _playerColor );
        _cubeViews.end( manager );
        _sphereViews.end( manager );
    }

    void AdventureAvatarComponent::showBorder( const AdventureRegionDef& region, GameObjectManager& manager )
    {
        const float32 half    = _borderWidth * 0.5f; // 땅 위에 앉힌다
        const float32 centerX = ( region._min._x + region._max._x ) * 0.5f;
        const float32 centerZ = ( region._min._y + region._max._y ) * 0.5f;
        const float3  alongX( region._max._x - region._min._x, _borderWidth, _borderWidth );
        const float3  alongZ( _borderWidth, _borderWidth, region._max._y - region._min._y );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( centerX, half, region._min._y ) }, alongX, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( centerX, half, region._max._y ) }, alongX, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( region._min._x, half, centerZ ) }, alongZ, 0.0f, _borderColor );
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( region._max._x, half, centerZ ) }, alongZ, 0.0f, _borderColor );
    }
} // namespace sw
