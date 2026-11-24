#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Client/ScavengerAvatarComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Client/ScavengerHUDViewModel.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Component/ScavengerGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    namespace
    {
        /** @brief 표식의 색 · 크기(m)입니다. */
        struct ScavengerAvatarComponentInternal
        {
            static constexpr float4  kShipColor       = float4( 0.75f, 0.75f, 0.8f, 1.0f );
            static constexpr float4  kOutsideColor    = float4( 0.35f, 0.3f, 0.25f, 1.0f );
            static constexpr float4  kRoomColor       = float4( 0.25f, 0.27f, 0.32f, 1.0f );
            static constexpr float4  kOpenDoorColor   = float4( 0.45f, 0.5f, 0.55f, 1.0f );
            static constexpr float4  kLockedDoorColor = float4( 0.8f, 0.25f, 0.2f, 1.0f );
            static constexpr float4  kScrapColor      = float4( 0.85f, 0.7f, 0.3f, 1.0f );
            static constexpr float4  kBodyColor       = float4( 0.5f, 0.1f, 0.1f, 1.0f ); ///< 동료 시신
            static constexpr float4  kThreatColor     = float4( 0.75f, 0.1f, 0.6f, 1.0f );
            static constexpr float4  kPlayerColor     = float4( 1.0f, 0.55f, 0.15f, 1.0f );
            static constexpr float4  kCrewColor       = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float3  kAreaSize        = float3( 4.0f, 0.1f, 4.0f );
            static constexpr float3  kDoorSize        = float3( 0.5f, 0.3f, 0.5f );
            static constexpr float3  kScrapSize       = float3( 0.4f, 0.4f, 0.4f );
            static constexpr float3  kCrewSize        = float3( 0.6f, 1.7f, 0.6f );
            static constexpr float3  kThreatSize      = float3( 1.0f, 1.2f, 1.0f );
            static constexpr float32 kCrewSpacing     = 0.9f; ///< 한 곳에 선 승무원 사이
            static constexpr float32 kScrapSpacing    = 0.6f; ///< 한 곳에 놓인 고철 사이
            static constexpr int32   kScrapColumns    = 4;
            static constexpr float32 kScrapRow        = -1.5f; ///< 고철 줄이 시작하는 깊이
        };
    } // namespace

    ScavengerAvatarComponent::ScavengerAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _leverAction{}
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

    ScavengerAvatarComponent::~ScavengerAvatarComponent() = default;

    void ScavengerAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                       pOwner    = getOwner();
        const ScavengerGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<ScavengerGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Scavenger.AvatarWithoutGameMode", "Scavenger avatar on '%#': no ScavengerGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<ScavengerGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<ScavengerGameModeComponent>( pDirector );
    }

    void ScavengerAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _markers.setPrefab( _markerPrefab.path() );
        _bActionsValid = _moveAction.empty() == false && _useAction.empty() == false && _leverAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "Scavenger.AvatarActionMissing", "Scavenger avatar: the actions must all be named (move '%#', use '%#', lever '%#') - the avatar reads no input", _moveAction.c_str(), _useAction.c_str(), _leverAction.c_str() );
    }

    void ScavengerAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _markers.destroyAll( *pManager );
        Component::onEndPlay();
    }

    void ScavengerAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( ScavengerAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void ScavengerAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        ScavengerGameModeComponent* const pDirector = manager.resolveComponent( _director );
        GameObject* const                 pOwner    = getOwner();
        if ( pDirector == nullptr || pOwner == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAcceptingInput() )
            submitInput( *pDirector );
        _markers.begin();
        drawGame( manager, *pDirector );
        _markers.end( manager );
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<ScavengerHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        fillHUD( *_hudViewModel, *pDirector );
    }

    void ScavengerAvatarComponent::submitInput( ScavengerGameModeComponent& director ) const
    {
        InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const IInputCaptureQuery* const pCapture = game::findSubsystem<IInputCaptureQuery>();
        for ( uint32 user = 0; user < pInput->getLocalUserCount(); ++user )
        {
            const InputMap* const pUserMap = pInput->findUserMap( user ); // 로컬 사용자 n 이 승무원 n 을 잡는다
            if ( pUserMap == nullptr )
                continue;
            const GameInputView input( *pUserMap, pCapture );
            const int32         player = static_cast<int32>( user );
            ScavengerCommand    command;
            command._moveTo          = director.findNeighborToward( player, input.getVector2D( _moveAction ) );
            command._bUseTriggered   = input.wasActionTriggered( _useAction ) ? SW_TRUE : SW_FALSE;
            command._bLeverTriggered = input.wasActionTriggered( _leverAction ) ? SW_TRUE : SW_FALSE;
            (void)director.submitCrewCommand( player, command ); // 승무원보다 많은 사용자의 입력은 쓸 곳이 없다
        }
    }

    void ScavengerAvatarComponent::drawGame( GameObjectManager& manager, const ScavengerGameModeComponent& director )
    {
        using Internal                        = ScavengerAvatarComponentInternal;
        const ScavengerExpedition& expedition = director.getExpedition();
        const ScavengerFacility&   facility   = expedition.getFacility();
        const hashed_string        ship( ScavengerExpedition::kShipAreaID );
        const hashed_string        outside( ScavengerExpedition::kOutsideAreaID );
        if ( expedition.getPhase() == ScavengerPhase::Landed )
        {
            for ( const AreaDef& area : facility.getGraph().getAreas() )
            {
                const float4& color = area._id == ship ? Internal::kShipColor : ( area._id == outside ? Internal::kOutsideColor : Internal::kRoomColor );
                _markers.drawStanding( manager, director.computeAreaPosition( area._id ), Internal::kAreaSize, color );
            }
            for ( const AreaLink& link : facility.getGraph().getLinks() )
            {
                const float3 middle = ( director.computeAreaPosition( link._from )._value + director.computeAreaPosition( link._to )._value ) * 0.5f;
                _markers.drawStanding( manager, AbsolutePosition{ middle }, Internal::kDoorSize,
                                       facility.canTraverse( link._from, link._to ) ? Internal::kOpenDoorColor : Internal::kLockedDoorColor );
            }
            int32 scrapIndex = 0;
            for ( const ScavengerScrap& scrap : facility.getGroundScrap() )
            {
                const float32 column = static_cast<float32>( scrapIndex % Internal::kScrapColumns );
                const float3  offset{ ( column - static_cast<float32>( Internal::kScrapColumns - 1 ) * 0.5f ) * Internal::kScrapSpacing, Internal::kAreaSize._y, Internal::kScrapRow };
                _markers.drawStanding( manager, director.computeAreaPosition( scrap._areaID ) + offset, Internal::kScrapSize, scrap.isBody() ? Internal::kBodyColor : Internal::kScrapColor );
                ++scrapIndex;
            }
            for ( const ScavengerThreat& threat : director.getThreats() )
            {
                _markers.drawStanding( manager, director.computeAreaPosition( threat._areaID ), Internal::kThreatSize, Internal::kThreatColor );
            }
        }
        else
        {
            _markers.drawStanding( manager, director.computeAreaPosition( ship ), Internal::kAreaSize, Internal::kShipColor );
        }
        const int32 crewCount = expedition.getCrewCount();
        for ( int32 player = 0; player < crewCount; ++player )
        {
            const ScavengerCrewMember& member = *expedition.findCrewMember( player );
            if ( member.isDead() )
                continue;
            const float32 column = static_cast<float32>( player ) - static_cast<float32>( crewCount - 1 ) * 0.5f;
            const float3  offset{ column * Internal::kCrewSpacing, Internal::kAreaSize._y, 0.0f };
            _markers.drawStanding( manager, director.computeAreaPosition( member._areaID ) + offset, Internal::kCrewSize, player == 0 ? Internal::kPlayerColor : Internal::kCrewColor );
        }
    }

    void ScavengerAvatarComponent::fillHUD( ScavengerHUDViewModel& outViewModel, const ScavengerGameModeComponent& director )
    {
        constexpr int32                  kPlayer  = 0; // 이 화면의 HUD 는 승무원 0 을 보인다
        const ScavengerCrewMember* const pPlayer  = director.getExpedition().findCrewMember( kPlayer );
        const ScavengerCatalog* const    pCatalog = director.findCatalog();
        const bool                       bHealth  = pPlayer != nullptr && pCatalog != nullptr && pPlayer->isDead() == false;
        outViewModel.setQuotaLeft( MathUtil::max( 0, director.getQuota() - director.getFulfilled() ) );
        outViewModel.setDaysLeft( director.getDaysLeft() );
        outViewModel.setShipValue( director.getShipValue() );
        outViewModel.setCredits( director.getCredits() );
        outViewModel.setHealthRatio( bHealth ? pPlayer->_healthMachine.getHealth() / pCatalog->getCrewHealth() : 0.0f );
        outViewModel.setDayRatio( director.computeDayRatio() );
        outViewModel.setOrbitShown( director.isAcceptingInput() && director.getExpedition().getPhase() == ScavengerPhase::InOrbit );
        outViewModel.setWinShown( director.getExpeditionResult() == ScavengerResult::Win );
        outViewModel.setLoseShown( director.getExpeditionResult() == ScavengerResult::Lose );
    }
} // namespace sw
