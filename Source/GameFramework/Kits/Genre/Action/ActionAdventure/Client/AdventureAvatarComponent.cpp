#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Client/AdventureAvatarComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 디버그 그리기의 색 · 크기(m)입니다. */
        struct AdventureAvatarComponentInternal
        {
            static constexpr float4  kBorderColor      = float4( 0.75f, 0.8f, 0.7f, 1.0f ); ///< 지역 경계
            static constexpr float4  kLitBeaconColor   = float4( 1.0f, 0.62f, 0.18f, 1.0f );
            static constexpr float4  kUnlitBeaconColor = float4( 0.35f, 0.35f, 0.4f, 1.0f );
            static constexpr float4  kCampColor        = float4( 0.95f, 0.4f, 0.1f, 1.0f );
            static constexpr float4  kPlayerColor      = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float32 kBorderHeight     = 0.05f; ///< 경계 선을 땅에서 띄우는 높이
            static constexpr float32 kBeaconHalfWidth  = 0.3f;
            static constexpr float32 kBeaconHalfHeight = 1.0f; ///< 땅 위에 선 상자라 가운데 높이이기도 하다
            static constexpr float32 kCampHeight       = 0.2f;
            static constexpr float32 kCampRadius       = 0.4f;
            static constexpr float32 kPlayerHeight     = 0.5f;
            static constexpr float32 kPlayerRadius     = 0.28f;

            static void drawBorder( DebugDrawQueue& draw, const WorldOrigin& origin, const AdventureRegionDef& region )
            {
                const AbsolutePosition corner00{ float3( region._min._x, kBorderHeight, region._min._y ) };
                const AbsolutePosition corner10{ float3( region._max._x, kBorderHeight, region._min._y ) };
                const AbsolutePosition corner11{ float3( region._max._x, kBorderHeight, region._max._y ) };
                const AbsolutePosition corner01{ float3( region._min._x, kBorderHeight, region._max._y ) };
                draw.drawLine( origin, corner00, corner10, kBorderColor );
                draw.drawLine( origin, corner10, corner11, kBorderColor );
                draw.drawLine( origin, corner11, corner01, kBorderColor );
                draw.drawLine( origin, corner01, corner00, kBorderColor );
            }
        };
    } // namespace

    AdventureAvatarComponent::AdventureAvatarComponent()
        : _moveAction{ CommonInputActions::kMove }
        , _useAction{}
        , _director{}
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    AdventureAvatarComponent::~AdventureAvatarComponent() = default;

    void AdventureAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                       pOwner    = getOwner();
        const AdventureGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<AdventureGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "Adventure.AvatarWithoutGameMode", "Adventure avatar on '%#': no AdventureGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<AdventureGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<AdventureGameModeComponent>( pDirector );
    }

    void AdventureAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        _bActionsValid = _moveAction.empty() == false && _useAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "Adventure.AvatarActionMissing", "Adventure avatar: move '%#' and use '%#' actions must both be named - the avatar reads no input", _moveAction.c_str(), _useAction.c_str() );
    }

    void AdventureAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( AdventureAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void AdventureAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        AdventureGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            (void)pDirector->submitPlayerInput( input.getVector2D( _moveAction ), input.wasActionTriggered( _useAction ) ); // 열린 판인지는 위에서 보았다
        }
        draw( *pDirector, manager );
    }

    void AdventureAvatarComponent::draw( const AdventureGameModeComponent& director, const GameObjectManager& manager )
    {
        using Internal                               = AdventureAvatarComponentInternal;
        DebugDrawQueue* const               pDraw    = game::findSubsystem<DebugDrawQueue>();
        const AdventureRegionCatalog* const pRegions = director.findRegions();
        const GameStateComponent* const     pState   = GameStateComponent::findOnOwner( director );
        if ( pDraw == nullptr || pRegions == nullptr || pState == nullptr )
            return;
        const WorldOrigin& origin = manager.getWorldOrigin();
        for ( const AdventureRegionDef& region : pRegions->getAll() )
        {
            Internal::drawBorder( *pDraw, origin, region ); // 씬에 땅 메시가 없는 지역(확장 팩)도 어디까지 걸을 수 있는지 보인다
            if ( region._beacon.isSet() )
            {
                const bool             bLit = director.getCampaign().isBeaconLit( region, pState->getFlags() );
                const AbsolutePosition center{ float3( region._beacon._position._x, Internal::kBeaconHalfHeight, region._beacon._position._y ) };
                const float3           halfExtent( Internal::kBeaconHalfWidth, Internal::kBeaconHalfHeight, Internal::kBeaconHalfWidth );
                pDraw->drawBox( origin, center, halfExtent, bLit ? Internal::kLitBeaconColor : Internal::kUnlitBeaconColor );
            }
            if ( region._camp.isSet() )
                pDraw->drawSphere( origin, AbsolutePosition{ float3( region._camp._position._x, Internal::kCampHeight, region._camp._position._y ) }, Internal::kCampRadius, Internal::kCampColor );
        }
        const float2& position = director.getPlayerPosition();
        pDraw->drawSphere( origin, AbsolutePosition{ float3( position._x, Internal::kPlayerHeight, position._y ) }, Internal::kPlayerRadius, Internal::kPlayerColor );
    }
} // namespace sw
