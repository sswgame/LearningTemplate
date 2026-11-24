#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Client/SurvivalHorrorAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Client/SurvivalHorrorHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/Component/SurvivalHorrorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

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
        : _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _lightAction{}
        , _healAction{}
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

    SurvivalHorrorAvatarComponent::~SurvivalHorrorAvatarComponent() = default;

    void SurvivalHorrorAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                            pOwner    = getOwner();
        const SurvivalHorrorGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<SurvivalHorrorGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "SurvivalHorror.AvatarWithoutGameMode", "Survival Horror avatar on '%#': no SurvivalHorrorGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<SurvivalHorrorGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<SurvivalHorrorGameModeComponent>( pDirector );
    }

    void SurvivalHorrorAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _moveAction.empty() == false && _useAction.empty() == false && _lightAction.empty() == false && _healAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "SurvivalHorror.AvatarActionMissing", "Survival Horror avatar: the actions must all be named (move '%#', use '%#', light '%#', heal '%#') - the avatar reads no input", _moveAction.c_str(), _useAction.c_str(), _lightAction.c_str(), _healAction.c_str() );
    }

    void SurvivalHorrorAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void SurvivalHorrorAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( SurvivalHorrorAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void SurvivalHorrorAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        SurvivalHorrorGameModeComponent* const pDirector = manager.resolveComponent( _director );
        GameObject* const                      pOwner    = getOwner();
        if ( pDirector == nullptr || pOwner == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput() )
            submitInput( *pDirector );
        _markers.begin();
        drawGame( manager, *pDirector );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<SurvivalHorrorHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, *pDirector );
    }

    void SurvivalHorrorAvatarComponent::submitInput( SurvivalHorrorGameModeComponent& director ) const
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const GameInputView   input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
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
