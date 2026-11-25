#include "pch.h"

#include "Games/Playground/FrontierTown/FrontierSkirmishGameModeComponent.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    FrontierSkirmishGameModeComponent::FrontierSkirmishGameModeComponent()
        : _workerUnit{}
        , _soldierUnit{}
        , _barracksUnit{}
        , _barracksX{ 20 }
        , _barracksY{ 20 }
        , _playerStart{ -1.0f, 0.0f, -1.0f }
        , _workerStart{ 2.5f, 0.0f, 2.5f }
        , _patrolA{ 2.5f, 0.0f, 6.5f }
        , _patrolB{ 10.5f, 0.0f, 6.5f }
        , _world{}
        , _worker{}
        , _soldier{}
        , _tickCount{ 0 }
        , _bPatrolToB{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    FrontierSkirmishGameModeComponent::~FrontierSkirmishGameModeComponent() = default;

    void FrontierSkirmishGameModeComponent::serializeGameState( StateIO& io )
    {
        GameStateComponent* pState  = io.isReading() ? GameStateComponent::findOnOwner( *this ) : nullptr;
        WalletComponent*    pWallet = io.isReading() ? findOnPlayerState<WalletComponent>() : nullptr;
        if ( io.check( pState != nullptr && pWallet != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion );
        // 틱 수 · 순찰 방향은 상태 칸이다(`GameModeComponent::serializeState`). 유닛 id 는 묶은 값으로 손 본문이 싣는다.
        uint64 workerValue  = _worker.packed();
        uint64 soldierValue = _soldier.packed();
        io.value( workerValue );
        io.value( soldierValue );
        if ( io.isOk() == false )
            return;
        // RTSWorld 는 옮길 수 없다(경로 풀이가 같은 객체의 격자를 참조한다) — 읽기는 지형을 다시 세운 _world 에 바로 한다. 유닛 · 시야 복원은 RTSWorld 직렬화가 임시 값에 읽고 다 읽은 뒤에만 옮긴다.
        if ( io.isReading() && io.check( initializeWorld( _world, *pState, *pWallet ) ) == false )
            return;
        io.value( _world );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _worker  = RTSUnitID::fromPacked( workerValue );
        _soldier = RTSUnitID::fromPacked( soldierValue );
    }

    bool FrontierSkirmishGameModeComponent::initializeWorld( RTSWorld& outWorld, GameStateComponent& state, WalletComponent& wallet ) const
    {
        const GridTopology& land         = state.getLand().getTopology();
        const bool          bStartOnLand = 0.0f <= _playerStart._x && _playerStart._x < static_cast<float32>( land._width ) && 0.0f <= _playerStart._z && _playerStart._z < static_cast<float32>( land._height );
        if ( bStartOnLand == false )
        {
            SW_LOG_ERROR_ID( "FrontierSkirmish.PlayerStartOffLand", "[Frontier] _playerStart (%#, %#) is not on the %#x%# land - the skirmish cannot start", _playerStart._x, _playerStart._z, land._width, land._height );
            return false;
        }
        if ( findCatalog() == nullptr || outWorld.initialize( findCatalog(), land._width, land._height ) == false )
            return false;
        outWorld.bindLand( &state.getLand(), int2{ 0, 0 } );
        (void)outWorld.addPlayer( 0, wallet.getWallet(), _playerStart ); // 플레이어 0 이 플레이어 지갑을 쓴다
        return true;
    }

    bool FrontierSkirmishGameModeComponent::startGame()
    {
        // 유닛 id 는 씬 데이터가 정본이다(C++ 에 기본값이 없다). 비었거나 카탈로그에 없으면 조용히 건너뛰지 않고 판을 열지 않는다.
        if ( acquireCatalog() == false || hasUnits( { &_workerUnit, &_soldierUnit, &_barracksUnit } ) == false )
            return false;
        // 판 상태(땅 · 시계)는 앞에 붙은 도시 디렉터가 연다 — 열리지 않았으면 여기서 다른 설정으로 열지 않고 멈춘다.
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr || pState->isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "FrontierSkirmish.StateNotOpen", "[Frontier] the skirmish cannot start - no game mode in front of it on this object opened the shared state" );
            return false;
        }
        const GameObject* const pPlayerState = requirePlayerState();
        WalletComponent* const  pWallet      = pPlayerState != nullptr ? PlayerComponentUtil::requireOn<WalletComponent>( *pPlayerState ) : nullptr;
        if ( pWallet == nullptr )
            return false; // 까닭은 찾기가 남겼다
        if ( initializeWorld( _world, *pState, *pWallet ) == false )
        {
            SW_LOG_ERROR_ID( "FrontierSkirmish.WorldNotOpen", "[Frontier] the skirmish cannot start - the RTS world does not open on the shared land" );
            return false;
        }
        if ( pWallet->isFreshGame() )
            pWallet->getWallet().add( findCatalog()->getRules()._mineralCurrency, getStartMinerals() );
        if ( pState->isFreshGame() )
        {
            _worker  = _world.spawnUnit( _workerUnit.key(), 0, _workerStart );
            _soldier = _world.spawnUnit( _soldierUnit.key(), 0, _patrolA );
        }
        _tickCount  = 0;
        _bPatrolToB = SW_TRUE;
        return true;
    }

    void FrontierSkirmishGameModeComponent::issueOpeningOrders()
    {
        // 도시가 첫 틱에 땅을 칠한 뒤다 — 병영 자리가 도시 땅이면 RTS 가 거절한다(Occupied 계열). 그러면 씬 배치를 고친다.
        const RTSCommandResult buildResult = _world.issueBuild( _worker, _barracksUnit.key(), int2{ _barracksX, _barracksY } );
        if ( buildResult != RTSCommandResult::Ok )
            SW_LOG_ERROR_ID( "FrontierSkirmish.WorkerCannotBuildCheck", "[Frontier] the worker cannot build '%#' at (%#, %#) (%#) - check the scene layout", _barracksUnit.c_str(), _barracksX, _barracksY, static_cast<int32>( buildResult ) );
        const RTSCommandResult moveResult = _world.issueMove( _soldier, _patrolB );
        if ( moveResult != RTSCommandResult::Ok )
            SW_LOG_ERROR_ID( "FrontierSkirmish.SoldierCannotStartPatrol", "[Frontier] the soldier cannot start its patrol (%#)", static_cast<int32>( moveResult ) );
    }

    void FrontierSkirmishGameModeComponent::continuePatrol()
    {
        const RTSUnit* pSoldier = _world.findUnit( _soldier );
        if ( pSoldier == nullptr || pSoldier->isIdle() == false )
            return;
        _bPatrolToB = _bPatrolToB == SW_TRUE ? SW_FALSE : SW_TRUE;
        (void)_world.issueMove( _soldier, _bPatrolToB == SW_TRUE ? _patrolB : _patrolA ); // 경로가 막혀 거절되면 다음 틱에 반대쪽으로 다시 해 본다
    }

    void FrontierSkirmishGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( ++_tickCount == 2 )
            issueOpeningOrders();
        else if ( _tickCount > 2 )
            continuePatrol();
        (void)_world.update( deltaTime, 1.0f ); // 배속 없는 시연 — 진행한 시간은 쓰지 않는다
    }

    void FrontierSkirmishGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 키트의 클라이언트 모듈(`RTSBattlefieldViewComponent`)이 세운다.
        (void)manager;
        (void)bRespawnViews;
    }
} // namespace sw
