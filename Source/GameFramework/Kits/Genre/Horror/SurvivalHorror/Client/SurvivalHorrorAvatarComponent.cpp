#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Client/SurvivalHorrorAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Client/SurvivalHorrorHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Component/SurvivalHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 표식의 색 · 크기(m)입니다. */
        struct SurvivalHorrorAvatarComponentInternal
        {
            static constexpr float4  kRoomColor    = float4( 0.4f, 0.36f, 0.3f, 1.0f );
            static constexpr float4  kDarkColor    = float4( 0.1f, 0.1f, 0.14f, 1.0f );
            static constexpr float4  kOpenColor    = float4( 0.45f, 0.5f, 0.55f, 1.0f );
            static constexpr float4  kLockedColor  = float4( 0.8f, 0.25f, 0.2f, 1.0f );
            static constexpr float4  kPickupColor  = float4( 0.9f, 0.8f, 0.3f, 1.0f );
            static constexpr float4  kPaperColor   = float4( 0.9f, 0.88f, 0.8f, 1.0f );
            static constexpr float4  kPuzzleColor  = float4( 0.3f, 0.5f, 0.75f, 1.0f );
            static constexpr float4  kBoxColor     = float4( 0.35f, 0.55f, 0.35f, 1.0f );
            static constexpr float4  kMonsterColor = float4( 0.45f, 0.1f, 0.15f, 1.0f );
            static constexpr float4  kPlayerColor  = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float32 kFloorHeight  = 0.1f;
            static constexpr float32 kFloorInset   = 0.2f; ///< 방 바닥 사이의 틈(m)
            static constexpr float3  kDoorSize     = float3( 0.8f, 0.4f, 0.8f );
            static constexpr float3  kSmallSize    = float3( 0.35f, 0.35f, 0.35f );
            static constexpr float3  kDeskSize     = float3( 0.9f, 0.9f, 0.6f );
            static constexpr float3  kMonsterSize  = float3( 0.8f, 1.8f, 0.8f );
            static constexpr float3  kPlayerSize   = float3( 0.6f, 1.7f, 0.6f );

            static const float4& findSpotColor( SurvivalHorrorSpotKind kind )
            {
                switch ( kind )
                {
                    case SurvivalHorrorSpotKind::Pickup:
                        return kPickupColor;
                    case SurvivalHorrorSpotKind::Document:
                    case SurvivalHorrorSpotKind::SavePoint:
                        return kPaperColor;
                    case SurvivalHorrorSpotKind::SequenceStep:
                    case SurvivalHorrorSpotKind::DialLock:
                    case SurvivalHorrorSpotKind::Deduction:
                        return kPuzzleColor;
                    case SurvivalHorrorSpotKind::ItemBox:
                        return kBoxColor;
                    case SurvivalHorrorSpotKind::Monster:
                        return kMonsterColor;
                }
                return kPickupColor; // 열거자는 여덟뿐이다(covered-switch) — 닿지 않는다
            }

            static const float3& findSpotSize( SurvivalHorrorSpotKind kind )
            {
                if ( kind == SurvivalHorrorSpotKind::Monster )
                    return kMonsterSize;
                return kind == SurvivalHorrorSpotKind::Pickup || kind == SurvivalHorrorSpotKind::Document ? kSmallSize : kDeskSize;
            }
        };
    } // namespace

    SurvivalHorrorAvatarComponent::SurvivalHorrorAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&SurvivalHorrorAvatarComponent::updateAvatar, &SurvivalHorrorAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _lightAction{}
        , _healAction{}
        , _markerPrefab{}
        , _markers{}
        , _hudViewModel{}
    {
        setCanEverTick( true );
    }

    SurvivalHorrorAvatarComponent::~SurvivalHorrorAvatarComponent() = default;

    void SurvivalHorrorAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        GameModeViewComponent::onEndPlay();
    }

    void SurvivalHorrorAvatarComponent::updateAvatar( GameObjectManager& manager, SurvivalHorrorGameModeComponent& director )
    {
        GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        dispatchInput( manager, director );
        _markers.begin();
        drawGame( manager, director );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<SurvivalHorrorHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, director );
    }

    void SurvivalHorrorAvatarComponent::submitInput( const GameModeInput& input, SurvivalHorrorGameModeComponent& director ) const
    {
        SurvivalHorrorCommand command;
        command._move            = input.getVector2D( _moveAction );
        command._bUseTriggered   = input.wasActionTriggered( _useAction ) ? SW_TRUE : SW_FALSE;
        command._bLightTriggered = input.wasActionTriggered( _lightAction ) ? SW_TRUE : SW_FALSE;
        command._bHealTriggered  = input.wasActionTriggered( _healAction ) ? SW_TRUE : SW_FALSE;
        (void)director.submitPlayerInput( command ); // 열린 판인지는 위에서 보았다
    }

    void SurvivalHorrorAvatarComponent::drawGame( GameObjectManager& manager, const SurvivalHorrorGameModeComponent& director )
    {
        using Internal                   = SurvivalHorrorAvatarComponentInternal;
        using Director                   = SurvivalHorrorGameModeComponent;
        const SurvivalHorrorStage& stage = director.getStage();
        for ( const AreaDef& area : director.getGraph().getAreas() )
        {
            const float3 size{ area._width - Internal::kFloorInset, Internal::kFloorHeight, area._height - Internal::kFloorInset };
            _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( area.getCenter() ), size, area._region == stage._darkRegion ? Internal::kDarkColor : Internal::kRoomColor );
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
        for ( size_t index = 0; index < stage._listSpot.size(); ++index )
        {
            const SurvivalHorrorSpot& spot = stage._listSpot[index];
            if ( director.isSpotUsed( index ) == false )
                _markers.showStanding( manager, _markerPrefab, Director::toAbsolute( spot._position ), Internal::findSpotSize( spot._kind ), Internal::findSpotColor( spot._kind ) );
        }
        _markers.showStanding( manager, _markerPrefab, director.getPlayerPosition(), Internal::kPlayerSize, Internal::kPlayerColor );
    }

    void SurvivalHorrorAvatarComponent::fillHUD( SurvivalHorrorHUDViewModel& outViewModel, const SurvivalHorrorGameModeComponent& director )
    {
        outViewModel.setItems( director.getItemCount() );
        outViewModel.setHealthRatio( director.getHealthRatio() );
        outViewModel.setSanityRatio( director.getSanityRatio() );
        outViewModel.setBatteryRatio( director.getBatteryRatio() );
        outViewModel.setDarkShown( director.isAcceptingInput() && director.isInDarkness() && director.isFlashlightOn() == false );
        outViewModel.setHallucinationShown( director.getSession().isHallucinating() );
        outViewModel.setFightShown( director.isFighting() );
        outViewModel.setWinShown( director.getGameResult() == SurvivalHorrorResult::Win );
        outViewModel.setLoseShown( director.getGameResult() == SurvivalHorrorResult::Lose );
    }
} // namespace sw
