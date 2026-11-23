#include "pch.h"

#include "Games/Playground/Velmora/Client/VelmoraEncounterViewComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
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
    namespace
    {
        /** @brief 룸 디버그 그리기 색 · 크기(m)입니다(`ActionCombatAvatarComponent` 와 같은 색). */
        struct VelmoraEncounterViewComponentInternal
        {
            static constexpr float4  kActorColor        = float4( 1.0f, 0.55f, 0.2f, 1.0f ); ///< 홀 적
            static constexpr float4  kBossColor         = float4( 1.0f, 0.25f, 0.2f, 1.0f ); ///< 보스 룸 적
            static constexpr float4  kProjectileColor   = float4( 1.0f, 0.9f, 0.2f, 1.0f );  ///< 적 투사체
            static constexpr float4  kInvulnerableColor = float4( 0.6f, 0.9f, 1.0f, 1.0f );  ///< 무적인 플레이어를 감싸는 공
            static constexpr float32 kActorHeight       = 0.5f;
            static constexpr float32 kProjectileHeight  = 0.4f;
            static constexpr float32 kPlayerHeight      = 0.5f;
            static constexpr float32 kInvulnerableRange = 0.34f; ///< 무적 공의 반지름 — 키트가 그리는 플레이어 공보다 조금 크다
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
        using Internal                = VelmoraEncounterViewComponentInternal;
        const ActionRoom&  room       = pEncounter->getRoom();
        const float4       actorColor = room.getKind() == ActionRoomKind::Boss ? Internal::kBossColor : Internal::kActorColor;
        const WorldOrigin& origin     = manager.getWorldOrigin(); // 룸의 자리는 지역 표와 같은 절대 자리다
        room.forEachLiveActor( [pDraw, &actorColor, &origin]( const float2& position, float32 radius )
        { pDraw->drawSphere( origin.toLocal( float3( position._x, Internal::kActorHeight, position._y ) ), radius, actorColor ); } );
        for ( const ProjectileState& projectile : room.getProjectiles() )
        {
            pDraw->drawSphere( origin.toLocal( float3( projectile._position._x, Internal::kProjectileHeight, projectile._position._y ) ), projectile._radius, Internal::kProjectileColor );
        }
        const AdventureGameModeComponent* const pDirector = room.isPlayerInvulnerable() ? PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this ) : nullptr;
        if ( pDirector != nullptr )
        {
            const float2& position = pDirector->getPlayerPosition();
            pDraw->drawSphere( origin.toLocal( float3( position._x, Internal::kPlayerHeight, position._y ) ), Internal::kInvulnerableRange, Internal::kInvulnerableColor );
        }
    }
} // namespace sw
