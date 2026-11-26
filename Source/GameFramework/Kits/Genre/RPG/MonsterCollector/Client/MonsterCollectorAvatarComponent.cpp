#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorAvatarComponent.h"

#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /** @brief 모습의 색 · 크기(m)와 입력 문턱입니다. */
        struct MonsterCollectorAvatarComponentInternal
        {
            static constexpr float4  kPlayerColor   = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4  kLeadColor     = float4( 0.35f, 0.85f, 0.4f, 1.0f );
            static constexpr float4  kFoeColor      = float4( 1.0f, 0.5f, 0.2f, 1.0f );
            static constexpr float4  kRestColor     = float4( 0.95f, 0.6f, 0.15f, 1.0f );
            static constexpr float3  kRestSize      = float3( 1.2f, 0.3f, 1.2f );
            static constexpr float3  kLeadOffset    = float3( 1.0f, 0.0f, 0.6f );
            static constexpr float3  kFoeOffset     = float3( 3.0f, 0.0f, 0.0f );
            static constexpr float3  kPlayerScale   = float3( 1.2f, 1.2f, 1.2f );
            static constexpr float3  kMonsterScale  = float3( 1.4f, 0.8f, 1.4f );
            static constexpr float32 kMoveThreshold = 0.5f; ///< 축이 이보다 기울어야 고른 기술을 옮긴다(스틱 떨림을 고르기로 읽지 않게)

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kMoveThreshold )
                    return 1;
                return axis < -kMoveThreshold ? -1 : 0;
            }

            /** @brief 종의 이름(현지화 키)입니다. 카탈로그에 없으면 비어 있습니다. */
            static string_view findSpeciesName( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster )
            {
                const MonsterSpeciesDef* const pSpecies = catalog.findSpecies( monster._speciesID );
                return pSpecies != nullptr ? string_view{ pSpecies->_name } : string_view{};
            }
        };
    } // namespace

    MonsterCollectorAvatarComponent::MonsterCollectorAvatarComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&MonsterCollectorAvatarComponent::updateAvatar, &MonsterCollectorAvatarComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _ballAction{}
        , _switchAction{}
        , _runAction{}
        , _restAction{}
        , _bodyPrefab{}
        , _markerPrefab{}
        , _bodies{}
        , _markers{}
        , _hudViewModel{}
        , _moveSlot{ 0 }
        , _lastStep{ 0 }
    {
    }

    MonsterCollectorAvatarComponent::~MonsterCollectorAvatarComponent() = default;

    void MonsterCollectorAvatarComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _moveSlot = 0;
        _lastStep = 0;
    }

    void MonsterCollectorAvatarComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _bodies.destroyAll( *pManager );
            _markers.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void MonsterCollectorAvatarComponent::updateAvatar( GameObjectManager& manager, MonsterCollectorGameModeComponent& director )
    {
        dispatchInput( manager, director );
        updateHUD( director );
        _bodies.begin();
        _markers.begin();
        draw( director, manager );
        _bodies.end( manager );
        _markers.end( manager );
    }

    void MonsterCollectorAvatarComponent::submitInput( const GameModeInput& input, MonsterCollectorGameModeComponent& director )
    {
        if ( director.getGamePhase() == MonsterCollectorPhase::Explore )
            submitExploreInput( director, input );
        else if ( director.getGamePhase() == MonsterCollectorPhase::Battle )
            submitBattleInput( director, input );
    }

    void MonsterCollectorAvatarComponent::submitExploreInput( MonsterCollectorGameModeComponent& director, const GameInputView& input )
    {
        // 쉼터 밖은 디렉터가 거절한다 — 거절은 할 일이 없다는 뜻이라 버린다.
        const float2 move = input.getVector2D( _moveAction );
        _lastStep         = MonsterCollectorAvatarComponentInternal::computeStep( move._x ); // 걷던 축을 누른 채 전투가 열려도 기술을 고른 것으로 읽지 않는다
        (void)director.submitMove( move );
        if ( input.wasActionTriggered( _restAction ) )
            (void)director.submitRest();
    }

    void MonsterCollectorAvatarComponent::updateMoveSlot( const MonsterCollectorGameModeComponent& director, float32 axis )
    {
        const MonsterInstance* const pLead = director.findLead();
        const int32                  step  = MonsterCollectorAvatarComponentInternal::computeStep( axis );
        const int32                  fresh = step != _lastStep ? step : 0;
        _lastStep                          = step;
        const int32 moveCount              = pLead != nullptr ? pLead->countMoves() : 0; // 슬롯은 앞에서부터 찬다
        if ( moveCount <= 0 )
            return;
        _moveSlot = ( ( _moveSlot + fresh ) % moveCount + moveCount ) % moveCount;
    }

    void MonsterCollectorAvatarComponent::submitBattleInput( MonsterCollectorGameModeComponent& director, const GameInputView& input )
    {
        updateMoveSlot( director, input.getVector2D( _moveAction )._x );
        // 쓸 수 없는 명령(빈 볼 · 교체 대기 중의 기술)은 디렉터가 거절하고 판은 그대로다.
        if ( input.wasActionTriggered( _attackAction ) )
            (void)director.submitAttack( _moveSlot );
        else if ( input.wasActionTriggered( _ballAction ) )
            (void)director.submitBall();
        else if ( input.wasActionTriggered( _runAction ) )
            (void)director.submitRun();
        else if ( input.wasActionTriggered( _switchAction ) )
        {
            const int32 active    = director.getBattle().getActiveIndex( MonsterBattle::kPlayerSide );
            const int32 partySize = static_cast<int32>( director.getBattle().getParty( MonsterBattle::kPlayerSide ).size() );
            for ( int32 offset = 1; offset < partySize; ++offset )
            {
                if ( director.submitSwitch( ( active + offset ) % partySize ) )
                    break;
            }
        }
    }

    void MonsterCollectorAvatarComponent::updateHUD( const MonsterCollectorGameModeComponent& director )
    {
        using Internal                                = MonsterCollectorAvatarComponentInternal;
        GameObject* const                    pOwner   = getOwner();
        const MonsterCollectorCatalog* const pCatalog = director.findCatalog();
        const MonsterInstance* const         pLead    = director.findLead();
        if ( pOwner == nullptr || pCatalog == nullptr || pLead == nullptr )
            return;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<MonsterCollectorHUDViewModel>();
        // 같은 뷰모델이면 거는 일은 아무것도 하지 않는다. HUD 가 없는 씬이면 값만 들고 보일 곳이 없다.
        if ( IHUDViewModelBinder* pBinder = game::findSubsystem<IHUDViewModelBinder>() )
            (void)pBinder->bindHUDViewModel( *pOwner, _hudViewModel.get() );
        const bool bBattle = director.getGamePhase() == MonsterCollectorPhase::Battle;
        _hudViewModel->setPhase( director.getGamePhase(), director.isAwaitingSwitch() );
        _hudViewModel->setLead( Internal::findSpeciesName( *pCatalog, *pLead ), pLead->_level, pLead->_hp, pLead->getMaxHP() );
        _hudViewModel->setCapture( director.getRemainingBallCount(), director.getCaptureCount(), director.getCaptureGoal() );
        _hudViewModel->setRestShown( director.getGamePhase() == MonsterCollectorPhase::Explore && director.isNearRest() );
        if ( bBattle == false )
            return;
        const MonsterInstance& foe = director.getBattle().getActive( MonsterBattle::kFoeSide );
        _hudViewModel->setFoe( Internal::findSpeciesName( *pCatalog, foe ), foe._level, foe._hp );
        const MonsterMoveDef* const pMove = pCatalog->findMove( pLead->_arrMove[_moveSlot]._moveID );
        _hudViewModel->setMoveName( pMove != nullptr ? string_view{ pMove->_name } : string_view{} );
    }

    void MonsterCollectorAvatarComponent::draw( const MonsterCollectorGameModeComponent& director, GameObjectManager& manager )
    {
        using Internal = MonsterCollectorAvatarComponentInternal;
        _markers.showStanding( manager, _markerPrefab, director.getRestPosition(), Internal::kRestSize, Internal::kRestColor );
        const AbsolutePosition player = director.getPlayerPosition();
        (void)_bodies.show( manager, _bodyPrefab, player, Internal::kPlayerScale, 0.0f, Internal::kPlayerColor ); // 실패는 배치가 한 번 알린다
        if ( director.getGamePhase() != MonsterCollectorPhase::Battle )
            return;
        (void)_bodies.show( manager, _bodyPrefab, player + Internal::kLeadOffset, Internal::kMonsterScale, 0.0f, Internal::kLeadColor );
        (void)_bodies.show( manager, _bodyPrefab, player + Internal::kFoeOffset, Internal::kMonsterScale, 0.0f, Internal::kFoeColor );
    }
} // namespace sw
