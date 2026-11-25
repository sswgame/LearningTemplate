#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Client/GhostHuntAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Client/GhostHuntHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/Component/GhostHuntGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 표식의 색 · 크기(m)입니다. */
        struct GhostHuntAvatarComponentInternal
        {
            static constexpr float4  kDarkRoomColor  = float4( 0.12f, 0.1f, 0.18f, 1.0f );
            static constexpr float4  kLitRoomColor   = float4( 0.75f, 0.65f, 0.45f, 1.0f );
            static constexpr float4  kLockedColor    = float4( 0.8f, 0.25f, 0.2f, 1.0f );
            static constexpr float4  kOpenColor      = float4( 0.45f, 0.5f, 0.55f, 1.0f );
            static constexpr float4  kFurnitureColor = float4( 0.45f, 0.28f, 0.15f, 1.0f );
            static constexpr float4  kSearchedColor  = float4( 0.25f, 0.2f, 0.16f, 1.0f );
            static constexpr float4  kBooColor       = float4( 0.95f, 0.95f, 1.0f, 1.0f );
            static constexpr float4  kVisibleColor   = float4( 0.4f, 0.8f, 0.95f, 1.0f );
            static constexpr float4  kAttackColor    = float4( 0.95f, 0.3f, 0.3f, 1.0f );
            static constexpr float4  kStunnedColor   = float4( 0.95f, 0.9f, 0.4f, 1.0f );
            static constexpr float4  kPlayerColor    = float4( 0.25f, 0.75f, 0.35f, 1.0f );
            static constexpr float4  kBeamColor      = float4( 1.0f, 0.95f, 0.7f, 1.0f );
            static constexpr float32 kFloorHeight    = 0.1f;
            static constexpr float32 kFloorInset     = 0.2f; ///< 방 바닥 사이의 틈(m)
            static constexpr float32 kBeamSpacing    = 1.0f; ///< 손전등 빛 표식 사이(m)
            static constexpr int32   kBeamMarkers    = 4;
            static constexpr float3  kDoorSize       = float3( 1.0f, 0.4f, 1.0f );
            static constexpr float3  kFurnitureSize  = float3( 1.2f, 1.4f, 0.6f );
            static constexpr float3  kGhostSize      = float3( 0.8f, 1.2f, 0.8f );
            static constexpr float3  kBooSize        = float3( 0.5f, 0.5f, 0.5f );
            static constexpr float3  kPlayerSize     = float3( 0.6f, 1.6f, 0.6f );
            static constexpr float3  kBeamSize       = float3( 0.3f, 0.1f, 0.3f );

            /** @brief 유령 상태의 색입니다. 숨었거나 잡혔으면 보이지 않으므로 nullptr 입니다. */
            static const float4* findGhostColor( GhostState state )
            {
                switch ( state )
                {
                    case GhostState::Visible:
                        return &kVisibleColor;
                    case GhostState::Attacking:
                        return &kAttackColor;
                    case GhostState::Stunned:
                    case GhostState::Sucking:
                        return &kStunnedColor;
                    case GhostState::Hidden:
                    case GhostState::Caught:
                        return nullptr;
                }
                return nullptr; // 열거자는 여섯뿐이다(covered-switch) — 닿지 않는다
            }
        };
    } // namespace

    GhostHuntAvatarComponent::GhostHuntAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _lightAction{}
        , _strobeAction{}
        , _vacuumAction{}
        , _useAction{}
        , _markerPrefab{}
        , _director{}
        , _markers{}
        , _hudViewModel{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    GhostHuntAvatarComponent::~GhostHuntAvatarComponent() = default;

    void GhostHuntAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                       pOwner    = getOwner();
        const GhostHuntGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<GhostHuntGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "GhostHunt.AvatarWithoutGameMode", "Ghost Hunt avatar on '%#': no GhostHuntGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<GhostHuntGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<GhostHuntGameModeComponent>( pDirector );
    }

    void GhostHuntAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bActionsValid = _moveAction.empty() == false && _lightAction.empty() == false && _strobeAction.empty() == false && _vacuumAction.empty() == false && _useAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "GhostHunt.AvatarActionMissing", "Ghost Hunt avatar: the actions must all be named (move '%#', light '%#', strobe '%#', vacuum '%#', use '%#') - the avatar reads no input", _moveAction.c_str(), _lightAction.c_str(), _strobeAction.c_str(), _vacuumAction.c_str(), _useAction.c_str() );
    }

    void GhostHuntAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void GhostHuntAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( GhostHuntAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void GhostHuntAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        GhostHuntGameModeComponent* const pDirector = manager.resolveComponent( _director );
        GameObject* const                 pOwner    = getOwner();
        if ( pDirector == nullptr || pOwner == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput() )
            submitInput( *pDirector );
        _markers.begin();
        drawGame( manager, *pDirector );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<GhostHuntHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, *pDirector );
    }

    void GhostHuntAvatarComponent::submitInput( GhostHuntGameModeComponent& director ) const
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
        GhostHuntCommand    command;
        command._move          = input.getVector2D( _moveAction );
        command._bLight        = input.isActionDown( _lightAction ) ? SW_TRUE : SW_FALSE;
        command._bStrobe       = input.isActionDown( _strobeAction ) ? SW_TRUE : SW_FALSE;
        command._bVacuum       = input.isActionDown( _vacuumAction ) ? SW_TRUE : SW_FALSE;
        command._bUseTriggered = input.wasActionTriggered( _useAction ) ? SW_TRUE : SW_FALSE;
        (void)director.submitPlayerInput( command ); // 열린 판인지는 위에서 보았다
    }

    void GhostHuntAvatarComponent::drawGame( GameObjectManager& manager, const GhostHuntGameModeComponent& director )
    {
        using Internal                     = GhostHuntAvatarComponentInternal;
        using Director                     = GhostHuntGameModeComponent;
        const GhostMansion&       mansion  = director.getMansion();
        const GhostCatalog* const pCatalog = director.findCatalog();
        if ( pCatalog == nullptr )
            return;
        for ( const AreaDef& area : director.getGraph().getAreas() )
        {
            const float3 size{ area._width - Internal::kFloorInset, Internal::kFloorHeight, area._height - Internal::kFloorInset };
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( area.getCenter() ), size, mansion.isRoomLit( area._id ) ? Internal::kLitRoomColor : Internal::kDarkRoomColor );
        }
        for ( const AreaLink& link : director.getGraph().getLinks() )
        {
            const AreaDef* const pFrom = director.getGraph().findArea( link._from );
            const AreaDef* const pTo   = director.getGraph().findArea( link._to );
            if ( pFrom == nullptr || pTo == nullptr )
                continue;
            const float2 middle = ( pFrom->getCenter() + pTo->getCenter() ) * 0.5f;
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( middle ), Internal::kDoorSize, director.isLinkOpen( link ) ? Internal::kOpenColor : Internal::kLockedColor );
        }
        for ( const GhostFurnitureDef& furniture : pCatalog->getFurniture() )
        {
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( director.computeFurniturePosition( furniture ) ), Internal::kFurnitureSize,
                                   mansion.isSearched( furniture._id ) ? Internal::kSearchedColor : Internal::kFurnitureColor );
        }
        for ( const HidingGhostDef& boo : pCatalog->getBoos() )
        {
            const HidingGhostRuntime* const pBoo       = mansion.findBoo( boo._id );
            const GhostFurnitureDef* const  pFurniture = pBoo != nullptr ? pCatalog->findFurniture( pBoo->_furniture ) : nullptr;
            if ( pFurniture == nullptr || pBoo->_state != HidingGhostState::Revealed )
                continue;
            const AbsolutePosition top = Director::toAbsolute( director.computeFurniturePosition( *pFurniture ) ) + float3{ 0.0f, Internal::kFurnitureSize._y, 0.0f };
            _markers.showStanding( manager, _markerPrefab, top, Internal::kBooSize, Internal::kBooColor );
        }
        for ( const GhostInstance& ghost : mansion.getEncounter().getGhosts() )
        {
            const float4* const pColor = Internal::findGhostColor( ghost._state );
            if ( pColor != nullptr )
                _markers.showStanding( manager, _markerPrefab, AbsolutePosition{ ghost._position }, Internal::kGhostSize, *pColor );
        }
        _markers.showStanding( manager, _markerPrefab, director.getPlayerPosition(), Internal::kPlayerSize, Internal::kPlayerColor );
        for ( int32 step = 1; director.isLightOn() && step <= Internal::kBeamMarkers; ++step )
        {
            _markers.showStanding( manager, _markerPrefab, director.getPlayerPosition() + director.getFacing() * ( static_cast<float32>( step ) * Internal::kBeamSpacing ), Internal::kBeamSize,
                                   Internal::kBeamColor );
        }
    }

    void GhostHuntAvatarComponent::fillHUD( GhostHuntHUDViewModel& outViewModel, const GhostHuntGameModeComponent& director )
    {
        outViewModel.setDarkRooms( director.countRooms() - director.countLitRooms() );
        outViewModel.setCoins( director.getCoins() );
        outViewModel.setKeys( director.getKeyCount() );
        outViewModel.setHealthRatio( director.getHealthRatio() );
        outViewModel.setSurgeRatio( director.getMansion().getEncounter().getSurgeGauge() );
        outViewModel.setWinShown( director.getHuntResult() == GhostHuntResult::Win );
        outViewModel.setLoseShown( director.getHuntResult() == GhostHuntResult::Lose );
    }
} // namespace sw
