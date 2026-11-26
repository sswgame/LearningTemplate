#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/RTSGameModeComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSSimulation.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"

namespace sw
{
    class WalletComponent;
    class GameStateComponent;

    /**
     * @class FrontierSkirmishGameModeComponent
     * @brief RTS 한 판입니다. 앞에 붙은 도시 디렉터(`CityGameModeComponent`)가 연 공유 상태를 빌리고, 새 판이면 시작 광물 · 일꾼 · 해병을 놓습니다.
     * @details RTS 는 공유 땅을 빌린다(`RTSSimulation::bindLand`, 원점 (0, 0)) — 도시가 지은 칸은 막혀 짓지 못하고 경로가 돌아간다. 돈은 공유 지갑(플레이어 0)이다.
     *          둘째 틱에 일꾼에게 `_barracksX`, `_barracksY` 에 병영 짓기를, 해병에게 두 지점(`_patrol*`) 사이 이동을 준다. 해병은 도착할 때마다 반대쪽으로 다시 간다.
     *          이 디렉터는 입력과 시계를 만지지 않는다(도시 디렉터가 민다). 모습은 키트 클라이언트 뷰가 세운다.
     */
    REFLECT( Category = "Frontier", DisplayName = "Frontier Skirmish Game Mode", Tooltip = "Runs the RTS kit on the shared land and wallet" )
    class FrontierSkirmishGameModeComponent : public RTSGameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "FTSK" };

        FrontierSkirmishGameModeComponent();
        virtual ~FrontierSkirmishGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        const RTSSimulation& getWorld() const override { return _world; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( GameSeconds frameTime ) override;

    private:
        /** @brief 빈 월드를 열고 공유 땅과 지갑을 겁니다(시작과 복원이 같은 설정으로 연다). 격자 크기는 공유 땅의 크기이고, 월드가 열리지 않으면 false 입니다. */
        [[nodiscard]] bool initializeWorld( RTSSimulation& outWorld, GameStateComponent& state, WalletComponent& wallet ) const;
        /** @brief 둘째 틱의 명령 — 병영 짓기와 첫 순찰 이동입니다. 거절되면 이유를 남긴다(데이터 오류). */
        void issueOpeningOrders();
        /** @brief 해병이 멈췄으면 순찰의 다른 끝으로 보냅니다. */
        void continuePatrol();

    private:
        PROPERTY( Category = "Units", DisplayName = "Worker", Tooltip = "RTS catalog id of the starting worker" )
        CatalogKey<RTSUnitDef> _workerUnit;
        PROPERTY( Category = "Units", DisplayName = "Soldier", Tooltip = "RTS catalog id of the patrolling soldier" )
        CatalogKey<RTSUnitDef> _soldierUnit;
        PROPERTY( Category = "Units", DisplayName = "Barracks", Tooltip = "RTS catalog id of the building the worker builds" )
        CatalogKey<RTSUnitDef> _barracksUnit;
        PROPERTY( Category = "Layout", DisplayName = "Barracks X", Min = 0 )
        int32 _barracksX;
        PROPERTY( Category = "Layout", DisplayName = "Barracks Y", Min = 0 )
        int32 _barracksY;
        PROPERTY( Category = "Layout", DisplayName = "Player Start", Tooltip = "Start position of the player on the shared land; required (the skirmish does not open off the land)", Units = m )
        float3 _playerStart;
        PROPERTY( Category = "Layout", DisplayName = "Worker Start", Units = m )
        float3 _workerStart;
        PROPERTY( Category = "Layout", DisplayName = "Patrol A", Tooltip = "First patrol end (also the soldier's start)", Units = m )
        float3 _patrolA;
        PROPERTY( Category = "Layout", DisplayName = "Patrol B", Units = m )
        float3 _patrolB;

        SW_CODEC_STATE RTSSimulation _world;
        SW_CODEC_STATE RTSUnitID     _worker;
        SW_CODEC_STATE RTSUnitID     _soldier;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _tickCount;
        PROPERTY( RuntimeState )
        uint8                  _bPatrolToB : 1; ///< 해병이 지금 B 쪽으로 간다
        [[maybe_unused]] uint8 _reserved   : 7;
    };

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
        io.header( kStateFormat );
        // 틱 수 · 순찰 방향은 상태 칸이다(`GameModeComponent::serializeState`). 유닛 id 는 묶은 값으로 손 본문이 싣는다.
        uint64 workerValue  = _worker.packed();
        uint64 soldierValue = _soldier.packed();
        io.value( workerValue );
        io.value( soldierValue );
        if ( io.isOk() == false )
            return;
        // RTSSimulation 는 옮길 수 없다(경로 풀이가 같은 객체의 격자를 참조한다) — 읽기는 지형을 다시 세운 _world 에 바로 한다. 유닛 · 시야 복원은 RTSSimulation 직렬화가 임시 값에 읽고 다 읽은 뒤에만 옮긴다.
        if ( io.isReading() && io.check( initializeWorld( _world, *pState, *pWallet ) ) == false )
            return;
        io.value( _world );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _worker  = RTSUnitID::fromPacked( workerValue );
        _soldier = RTSUnitID::fromPacked( soldierValue );
    }

    bool FrontierSkirmishGameModeComponent::initializeWorld( RTSSimulation& outWorld, GameStateComponent& state, WalletComponent& wallet ) const
    {
        const GridTopology& land         = state.getLand().getTopology();
        const bool          bStartOnLand = 0.0f <= _playerStart._x && _playerStart._x < static_cast<float32>( land._width ) && 0.0f <= _playerStart._z && _playerStart._z < static_cast<float32>( land._height );
        if ( bStartOnLand == false )
        {
            SW_LOG_ERROR_ID( "FrontierSkirmish.PlayerStartOffLand", "[Frontier] _playerStart (%#, %#) is not on the %#x%# land - the skirmish cannot start", _playerStart._x, _playerStart._z, land._width, land._height );
            return false;
        }
        if ( findCatalog() == nullptr || outWorld.initialize( *findCatalog(), land._width, land._height ) == false )
            return false;
        outWorld.bindLand( state.getLand(), int2{ 0, 0 } );
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

    void FrontierSkirmishGameModeComponent::updateGame( GameSeconds frameTime )
    {
        if ( ++_tickCount == 2 )
            issueOpeningOrders();
        else if ( _tickCount > 2 )
            continuePatrol();
        (void)_world.update( frameTime, 1.0f ); // 배속 없는 시연 — 진행한 시간은 쓰지 않는다
    }
} // namespace sw

#include "FrontierSkirmishGameModeComponent.cpp.gen.inl"
