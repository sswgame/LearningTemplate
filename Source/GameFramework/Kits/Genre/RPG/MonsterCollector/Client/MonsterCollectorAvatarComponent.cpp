#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorAvatarComponent.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Client/MonsterCollectorHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/Component/MonsterCollectorGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IHUDViewModelBinder.h"

namespace sw
{
    namespace
    {
        /** @brief 디버그 그리기의 색 · 크기(m)와 입력 문턱입니다. */
        struct MonsterCollectorAvatarComponentInternal
        {
            static constexpr float4  kPlayerColor    = float4( 0.2f, 0.6f, 1.0f, 1.0f );
            static constexpr float4  kLeadColor      = float4( 0.35f, 0.85f, 0.4f, 1.0f );
            static constexpr float4  kFoeColor       = float4( 1.0f, 0.5f, 0.2f, 1.0f );
            static constexpr float4  kRestColor      = float4( 0.95f, 0.6f, 0.15f, 1.0f );
            static constexpr float3  kRestHalfExtent = float3( 0.6f, 0.15f, 0.6f );
            static constexpr float3  kLeadOffset     = float3( 1.0f, 0.0f, 0.6f );
            static constexpr float3  kFoeOffset      = float3( 3.0f, 0.0f, 0.0f );
            static constexpr float32 kBodyRadius     = 0.35f;
            static constexpr float32 kBodyHeight     = 0.4f; ///< 몸을 땅에서 띄우는 높이
            static constexpr float32 kMoveThreshold  = 0.5f; ///< 축이 이보다 기울어야 고른 기술을 옮긴다(스틱 떨림을 고르기로 읽지 않게)

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
        : _moveAction{ CommonInputActions::kMove }
        , _attackAction{}
        , _ballAction{}
        , _switchAction{}
        , _runAction{}
        , _restAction{}
        , _director{}
        , _hudViewModel{}
        , _moveSlot{ 0 }
        , _lastStep{ 0 }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    MonsterCollectorAvatarComponent::~MonsterCollectorAvatarComponent() = default;

    void MonsterCollectorAvatarComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                              pOwner    = getOwner();
        const MonsterCollectorGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<MonsterCollectorGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "MonsterCollector.AvatarWithoutGameMode", "Monster collector avatar on '%#': no MonsterCollectorGameModeComponent in front of it on the same object - the avatar does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<MonsterCollectorGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<MonsterCollectorGameModeComponent>( pDirector );
    }

    void MonsterCollectorAvatarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PostUpdate );
        const bool bMoveNamed   = _moveAction.empty() == false && _restAction.empty() == false;
        const bool bBattleNamed = _attackAction.empty() == false && _ballAction.empty() == false && _switchAction.empty() == false && _runAction.empty() == false;
        _bActionsValid          = bMoveNamed && bBattleNamed ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "MonsterCollector.AvatarActionMissing",
                             "Monster collector avatar: move '%#', rest '%#', attack '%#', ball '%#', switch '%#' and run '%#' actions must all be named - the avatar reads no input", _moveAction.c_str(),
                             _restAction.c_str(), _attackAction.c_str(), _ballAction.c_str(), _switchAction.c_str(), _runAction.c_str() );
        _moveSlot = 0;
        _lastStep = 0;
    }

    void MonsterCollectorAvatarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( MonsterCollectorAvatarComponent& avatar, GameObjectManager& manager )
        { avatar.updateAvatar( manager ); } );
    }

    void MonsterCollectorAvatarComponent::updateAvatar( GameObjectManager& manager )
    {
        MonsterCollectorGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        const bool                bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE;
        const InputManager* const pInput     = bReadInput ? game::findSubsystem<InputManager>() : nullptr;
        if ( pInput != nullptr )
        {
            const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
            if ( pDirector->getGamePhase() == MonsterCollectorPhase::Explore )
                submitExploreInput( *pDirector, input );
            else if ( pDirector->getGamePhase() == MonsterCollectorPhase::Battle )
                submitBattleInput( *pDirector, input );
        }
        updateHUD( *pDirector );
        draw( *pDirector, manager );
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

    void MonsterCollectorAvatarComponent::draw( const MonsterCollectorGameModeComponent& director, const GameObjectManager& manager )
    {
        using Internal              = MonsterCollectorAvatarComponentInternal;
        DebugDrawQueue* const pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        const WorldOrigin& origin = manager.getWorldOrigin();
        pDraw->drawBox( origin.toLocal( director.getRestPosition() + float3( 0.0f, Internal::kRestHalfExtent._y, 0.0f ) ), Internal::kRestHalfExtent, Internal::kRestColor );
        const AbsolutePosition player = director.getPlayerPosition() + float3( 0.0f, Internal::kBodyHeight, 0.0f );
        pDraw->drawSphere( origin.toLocal( player ), Internal::kBodyRadius, Internal::kPlayerColor );
        if ( director.getGamePhase() != MonsterCollectorPhase::Battle )
            return;
        pDraw->drawSphere( origin.toLocal( player + Internal::kLeadOffset ), Internal::kBodyRadius, Internal::kLeadColor );
        pDraw->drawSphere( origin.toLocal( player + Internal::kFoeOffset ), Internal::kBodyRadius, Internal::kFoeColor );
    }
} // namespace sw
