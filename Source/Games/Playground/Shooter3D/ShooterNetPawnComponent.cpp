#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/WallClock.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardScoreSender.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchBackfill.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/FirstPersonCameraComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/TickedFloorStep.h"

#include "Games/Playground/Shooter3D/ShooterAvatarComponent.h"
#include "Games/Playground/Shooter3D/ShooterEnemyComponent.h"
#include "Games/Playground/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Playground/Shooter3D/ShooterPlayerComponent.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief 슈터 서버가 받는 서버 키트 인터페이스 형식 목록입니다. 백필은 매칭 키트가 `gameServer` 로 선 서버에, 점수 제출은 순위표 키트가 선 서버에만 있어 선택이다. */
        using ShooterServerKits = ServerKitDependencies<ServerKitRequired<>, ServerKitOptional<IMatchBackfillSender, ILeaderboardScoreSender>>;

        struct ShooterNetPawnComponentInternal
        {
            /** @brief 매칭이 배정한 게임 서버의 백필 창구입니다. 매칭 키트가 `gameServer` 로 선 전용 서버에만 있다 — 없으면 nullptr(클라이언트 · 한 프로세스 재생 · 매칭 없는 서버). */
            static IMatchBackfillSender* findBackfillSender()
            {
                const optional<ShooterServerKits> kits = ShooterServerKits::resolve( game::findSubsystem<IServerKitLocator>() );
                return kits.has_value() ? kits->find<IMatchBackfillSender>() : nullptr;
            }

            /**
             * @brief 조종하던 계정 @p accountID 가 나갔다고 매칭에 알려 그 자리를 채우게 합니다(경기 중 백필).
             * @details 전용 서버에 매칭 키트가 `gameServer` 로 서 있을 때만 창구가 있다 — 클라이언트 · 한 프로세스 재생 · 매칭 없는 서버는 아무것도 하지 않는다.
             *          경기 · 팀은 게임이 적지 않고 키트가 읽은 명단에서 찾는다. 명단 밖 접속(개발 접속)이면 NotQueued 로 끝나고 맡기지 않는다.
             */
            static void reportPlayerLeft( uint64 accountID )
            {
                IMatchBackfillSender* const pBackfill = findBackfillSender();
                if ( pBackfill == nullptr )
                    return;
                const MatchmakingServiceResult result = pBackfill->submitPlayerLeft( static_cast<AccountID>( accountID ), WallClock::nowUnixMilliseconds() );
                if ( result.isFailed() )
                    SW_LOG_WARNING_ID( "Shooter.BackfillPlayerLeftFailed", "[Shooter] backfill for the player who left %# was refused (error %#)", accountID, result._errorCode );
            }

            /** @brief 한 프레임에 따라잡는 고정 틱 상한입니다. 멈춘 뒤의 긴 프레임이 틱을 몰아 돌리지 않게 한다(남은 시간은 버린다). */
            static constexpr int32 kMaxTicksPerFrame = 4;

            /** @brief 같은 오브젝트 몸 이동 하나의 한 틱입니다 — 서버 시뮬레이션과 클라이언트 재시뮬레이션이 같은 이 함수를 쓴다. */
            static ShooterMoveState simulateTick( const ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const vector<ShooterFloorStep>& listFloor, const ShooterMoveState& state,
                                                  const ControlIntent& intent, float32 tickInterval )
            {
                bool bLanded = false;
                return movement.simulate( listBox, listFloor, state, movement.makeInput( intent ), tickInterval, bLanded );
            }

            /** @brief 움직임 없이 시선만 남긴 의도입니다 — 쓰러진 몸 · 멈춘 몸이 쓴다. */
            static ControlIntent makeStillIntent( const ControlIntent& intent )
            {
                ControlIntent still;
                still._controlYaw   = intent._controlYaw;
                still._controlPitch = intent._controlPitch;
                still._viewTick     = intent._viewTick;
                return still;
            }

            /** @brief 칸 선언대로 상태 하나를 바이트로 씁니다. 칸이 범위 밖이면 false 입니다. */
            template <typename TState>
            [[nodiscard]] static bool writeState( const TState& state, vector<uint8>& outBytes )
            {
                BitWriter writer{ std::move( outBytes ) };
                writer.clear();
                const bool bWritten = ReplicatedLayout::writeFull( state, writer );
                outBytes            = writer.releaseBytes();
                return bWritten;
            }

            /** @brief 칸 선언대로 상태 하나를 읽습니다. 깨졌거나 범위 밖이면 false 입니다. */
            template <typename TState>
            [[nodiscard]] static bool readState( const vector<uint8>& bytes, TState& outState )
            {
                BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                return ReplicatedLayout::readFull( reader, outState, nullptr ) == FixedReadResult::Ok;
            }

            static_assert( ShooterPlayerComponent::kWeaponCount == 3, "ShooterPawnNetState::_weaponIndex 의 Max 를 무기 슬롯 수와 맞춘다" );
            static_assert( static_cast<uint8>( ShooterEnemyPhase::Dying ) == 4, "ShooterEnemyNetState::_phase 의 Max 를 열거의 끝 값과 맞춘다" );

            /** @brief 적 하나의 엔티티 상태를 씁니다 — 칸은 `ShooterEnemyNetState` 의 선언이다. 자리가 복제 범위 밖이면 false 입니다. */
            [[nodiscard]] static bool writeEnemy( const ShooterEnemyView& view, vector<uint8>& outBytes )
            {
                ShooterEnemyNetState state;
                state._position       = view._position._value;
                state._yaw            = MathUtil::wrapAngle( view._yaw ); // 회전은 같고 범위([-π, π])에 든다
                state._phase          = static_cast<uint8>( view._phase );
                state._kindIndex      = view._kindIndex;
                state._appearanceSeed = view._appearanceSeed;
                state._teleportCount  = view._teleportCount;
                return writeState( state, outBytes );
            }

            /**
             * @brief 클라이언트가 예측하는 월드의 서버 틱 @p serverTick 을 씬에 알린다 — 틱 함수 바닥(`InterpToMovementComponent`)이 자기 서브틱에서 그 자리를 읽는다(바닥을 돌지 않는다).
             *        조종 시스템이 없는 씬이면 바닥도 움직이지 않으므로 알릴 곳이 없다. 서버의 바닥은 월드의 서버 시계를 바로 본다.
             */
            static void publishServerTick( const GameObject* pOwner, int64 serverTick )
            {
                const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                ControlSystem*           pControl = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
                if ( pControl != nullptr )
                    pControl->publishServerTick( serverTick );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ShooterNetPawnComponent::ShooterNetPawnComponent()
        : _tickInterval{ 1.0f / 30.0f }
        , _smoothingHalfLife{ 0.05f }
        , _smoothingSnapDistance{ 2.0f }
        , _reconcileTolerance{ 0.01f }
        , _interestCellSize{ 16.0f }
        , _interestEnterRadius{ 60.0f }
        , _interestLeaveRadius{ 70.0f }
        , _listGuestSpawn{}
        , _pawnHitHeight{ 1.8f }
        , _server{}
        , _client{}
        , _interest{}
        , _routerSubscription{}
        , _prediction{}
        , _smoother{}
        , _bytesScratch{}
        , _listClaim{}
        , _listServerPawn{}
        , _listRemotePawn{}
        , _listConnectionScratch{}
        , _listLagScratch{}
        , _listFloorScratch{}
        , _listEnemyEntityID{}
        , _listEntityScratch{}
        , _lagHistory{ ReplicationServerConfig{}._maxViewLagTicks + 1 } // 보던 시각의 되감기 한도(서버 클램프)까지 기억한다
        , _accumulator{ 0.0f }
        , _listPendingTrigger{}
        , _stride{ 1 }
        , _tick{ 0 }
        , _pawnEntityID{ 0 }
        , _reconciledTick{ 0 }
        , _rewoundShotCount{ 0 }
        , _rewoundHitCount{ 0 }
        , _lastRewindTicks{ 0.0 }
        , _sentClaimCount{ 0 }
        , _confirmedClaimCount{ 0 }
        , _rejectedClaimCount{ 0 }
        , _floorLocalPosition{}
        , _floorTickOffset{ 0 }
        , _pendingStepCount{ 0 }
        , _role{ Role::None }
        , _bFloorTickKnown{ SW_FALSE }
        , _floorNumber{ 0 }
        , _bSeatsReported{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    ShooterNetPawnComponent::~ShooterNetPawnComponent() = default;

    void ShooterNetPawnComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _role = Role::None;
        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 — 전역 세션을 찾지 않는다. 한 프로세스에 월드가 여럿이어도(가상 서버 하니스) 각자 자기 링크를 본다.
        const NetLinkValue*           pLink      = SceneNetLink::findBoundFor( *this );
        ShooterBodyMovementComponent* pMovement  = findMovement();
        const bool                    bNoSession = pLink == nullptr || pMovement == nullptr;
        if ( bNoSession )
            return; // 혼자 하는 게임 — 몸 이동은 프레임마다 예전 길로 돈다
        const NetLinkValue&    link   = *pLink;
        const optional<uint32> stride = NetClock::computeStride( _tickInterval );
        if ( stride.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.TickIntervalNotMultiple", "Shooter net pawn: tick interval %# s is not a multiple of the server tick (%# s) - the networked movement stays off", _tickInterval, NetClock::kTickSeconds );
            return;
        }
        _stride = *stride;

        PredictionSmootherSettings smoothing;
        smoothing._halfLifeSeconds = _smoothingHalfLife;
        smoothing._snapDistance    = _smoothingSnapDistance;
        if ( _smoother.initialize( smoothing ) == false )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnSmoothing", "Shooter net pawn: smoothing half life %# s is not usable - the networked movement stays off", _smoothingHalfLife );
            return;
        }
        if ( link._role == NetLinkRole::Server )
        {
            if ( _listGuestSpawn.size() >= kMaxSeat )
            {
                SW_LOG_ERROR_ID( "ShooterNetPawn.TooManySeats", "Shooter net pawn: %# guest spawns do not fit %# seats - the networked movement stays off", static_cast<uint32>( _listGuestSpawn.size() ), kMaxSeat );
                return;
            }
            ReplicationServerConfig settings;
            settings._tickInterval  = _tickInterval;
            const bool bInitialized = _interest.initialize( _interestCellSize, _interestEnterRadius, _interestLeaveRadius ) &&
                                      _server.initialize( link._pPeers, settings, &_interest );
            if ( bInitialized )
                _routerSubscription = link._pRouter->registerHandler( &_server );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnReplication", "Shooter net pawn: the replication server did not start (reason above) - the networked movement stays off" );
                return;
            }
            _role = Role::Server;
            (void)MeleeSweepHistory::getOrCreate( *getOwner()->getMutableManager() ); // 서버만 켠다 — 권위 있는 스윕이 여기에 적힌다
            _listServerPawn.clear();
            _listServerPawn.resize( _listGuestSpawn.size() + 1u );
            for ( ServerPawn& pawn : _listServerPawn )
            {
                pawn._viewTickByInput.initialize( RemoteControllerComponent::kReceiveCapacity );
            }
            _listServerPawn.front()._object = getOwner()->getHandle();
        }
        else
        {
            ReplicationClientConfig settings;
            settings._tickInterval = _tickInterval;
            if ( _client.initialize( link._pPeers, link._pRouter->getWorldClock(), settings ) )
                _routerSubscription = link._pRouter->registerHandler( &_client );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnReplication2", "Shooter net pawn: the replication client did not start (reason above) - the networked movement stays off" );
                return;
            }
            _role = Role::Client;
        }
        _accumulator      = 0.0f;
        _tick             = _role == Role::Server ? link._pRouter->getWorldClock().getWholeTick( _stride ) : 0u;
        _pawnEntityID     = 0;
        _reconciledTick   = 0;
        _pendingStepCount = 0;
        _bFloorTickKnown  = SW_FALSE;
        _bSeatsReported   = SW_FALSE;
        pMovement->setNetDriven( true );
        // 클라이언트가 알리는 서버 틱을 읽는 바닥이 이 틱 뒤에 돌게 한다(바닥 서브틱의 선행 조건).
        ControlSystem* pControl = _role == Role::Client ? ControlSystem::find( *getOwner()->getMutableManager() ) : nullptr;
        if ( pControl != nullptr )
            pControl->setServerTickSource( getTickHandle() );
        SW_LOG_INFO_ID( "ShooterNetPawn.ShooterNetPawnMovement", "Shooter net pawn: %# movement at %# Hz", _role == Role::Server ? "server-authoritative" : "predicted", static_cast<int32>( MathUtil::round( 1.0f / _tickInterval ) ) );
    }

    void ShooterNetPawnComponent::onEndPlay()
    {
        stopRole();
        Component::onEndPlay();
    }

    void ShooterNetPawnComponent::stopRole()
    {
        _routerSubscription.reset();
        if ( _role == Role::Server && getOwner() != nullptr && getOwner()->getMutableManager() != nullptr )
            MeleeSweepHistory::release( *getOwner()->getMutableManager() );
        ControlSystem* pControl = getOwner() != nullptr && getOwner()->getMutableManager() != nullptr ? ControlSystem::find( *getOwner()->getMutableManager() ) : nullptr;
        if ( pControl != nullptr && pControl->getServerTickSource() == getTickHandle() )
            pControl->setServerTickSource( SubTickHandle{} );
        ShooterBodyMovementComponent* pMovement = findMovement();
        if ( pMovement != nullptr )
        {
            pMovement->setNetDriven( false );
            pMovement->setDisplayOffset( float3{ 0.0f, 0.0f, 0.0f } );
        }
        GameObjectManager* const pManager = getOwner() != nullptr ? getOwner()->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            clearRemotePawnList( *pManager );
            for ( size_t seatIndex = 1; seatIndex < _listServerPawn.size(); ++seatIndex )
            {
                GameObject* pRemote = pManager->resolveGameObject( _listServerPawn[seatIndex]._remoteController );
                GameObject* pObject = pManager->resolveGameObject( _listServerPawn[seatIndex]._object );
                if ( pRemote != nullptr )
                    pManager->destroyGameObject( pRemote );
                if ( pObject != nullptr )
                    pManager->destroyGameObject( pObject );
            }
        }
        _listServerPawn.clear();
        _listClaim.clear();
        _listPendingTrigger.clear();
        if ( pControl != nullptr )
            pControl->publishViewTick( ControlViewTick::makePresent() );
        _role = Role::None;
    }

    void ShooterNetPawnComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _role != Role::None && _routerSubscription.isActive() == false )
        {
            // 플레이 중에 세션이 닫혔다 — 닫힌 창구로 입력을 보내지 않고, 아무도 옮기지 않는 몸(netDriven)도 남기지 않는다. 조종 시스템을 쓰므로 틱 뒤에 놓는다.
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn )
            { netPawn.stopRole(); } );
            return;
        }
        ShooterBodyMovementComponent* pMovement = findMovement();
        const bool                    bIdle     = _role == Role::None || pMovement == nullptr || findGameMode() == nullptr || context.getDeltaTime() <= 0.0f;
        if ( bIdle )
            return;
        // 발동은 한 프레임만 참이다 — 입력 틱(30 Hz)이 없던 프레임의 발동이 사라지지 않게 매 프레임 모은다.
        if ( _role == Role::Client )
            collectPendingTriggers( *pMovement );
        // 걸음은 여러 몸을 옮기고 바로 판정한다(물리 질의 · 남의 체력) — 서버 · 클라이언트 모두 틱 뒤(게임 스레드)의 한 자리에서 돈다.
        if ( _role == Role::Server )
        {
            // 입력 몫도 틱과 같은 시계(게임 시간)로 채운다 — 클라이언트의 입력 틱이 이 시계로 돌고, 고정 간격으로 되풀이하는 넷 재생에서도 같은 몫이 된다.
            _server.accumulateInputBudget( context.getDeltaTime() );
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn, GameObjectManager& manager )
            { netPawn.runServerFrame( manager ); } );
            return;
        }
        _accumulator += context.getDeltaTime();
        int32 tickCount = 0;
        while ( _accumulator >= _tickInterval && tickCount < ShooterNetPawnComponentInternal::kMaxTicksPerFrame )
        {
            _accumulator -= _tickInterval;
            ++tickCount;
        }
        if ( _accumulator >= _tickInterval )
            _accumulator = 0.0f; // 따라잡지 못한 시간은 버린다 — 몰아 돌린 틱이 다음 프레임을 또 밀지 않게
        _pendingStepCount += tickCount;
        _smoother.update( context.getDeltaTime() );
        context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn, GameObjectManager& manager )
        { netPawn.runClientFrame( manager ); } );
    }

    // ------------------------------------------------------------------------------
    // 서버(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void ShooterNetPawnComponent::runServerFrame( GameObjectManager& manager )
    {
        const NetLinkValue*             pLink     = SceneNetLink::findBoundFor( *this );
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( _role != Role::Server || pGameMode == nullptr || pLink == nullptr )
            return;
        if ( _bSeatsReported == SW_FALSE )
        {
            _bSeatsReported = SW_TRUE;
            reportSeatCapacity();
        }
        // 월드의 서버 시계가 지난 이 컴포넌트의 틱을 차례로 돈다 — 번호를 따로 세지 않는다(시계가 한 프레임에 흐르는 상한을 갖는다).
        const uint32 presentTick = pLink->_pRouter->getWorldClock().getWholeTick( _stride );
        while ( _tick < presentTick )
        {
            ++_tick;
            stepServer( manager, *pLink->_pPeers, *pGameMode );
        }
        syncServerPawnList( manager );
        for ( ServerPawn& pawn : _listServerPawn )
        {
            resolveClaims( manager, pawn );
        }
    }

    void ShooterNetPawnComponent::stepServer( GameObjectManager& manager, INetPeers& peers, const ShooterGameModeComponent& gameMode )
    {
        _server.beginTick( _tick ); // 입력을 꺼내기 전에 — 보던 틱은 서버의 지금 틱으로 자른다(`getClientViewTick`)
        assignSeats( peers );
        for ( ServerPawn& pawn : _listServerPawn )
        {
            stepServerPawn( manager, pawn, gameMode );
        }
        recordBodies( manager );
        publishPawnStates( manager );
        _server.endTick();
        sendOwnerStates( manager );
        _interest.updateRelevancy();
        _server.sendSnapshots();
    }

    void ShooterNetPawnComponent::assignSeats( INetPeers& peers )
    {
        bool bFreeSeat = false;
        for ( ServerPawn& pawn : _listServerPawn )
        {
            // 끊긴 사이 같은 id 로 새 연결이 섰어도 앞사람의 자리를 물려주지 않는다 — 주체(계정)가 다르면 떠난 것이다.
            const bool bGone = pawn._connectionID >= 0 && ( peers.getConnectionState( pawn._connectionID ) != NetConnectionState::Connected ||
                                                            peers.getConnectionPrincipal( pawn._connectionID ) != pawn._accountID );
            if ( bGone )
            {
                // 나감 처리(점수 · 빈 자리)는 이 프레임의 걸음 뒤다 — 끊긴 연결의 주체는 이미 0 이라 앉을 때 적어 둔 계정을 쓴다
                _interest.removeObserver( pawn._connectionID );
                pawn._leftAccountID = pawn._accountID;
                pawn._accountID     = 0;
                pawn._connectionID  = constant::invalid_index::kInt32;
                pawn._nextInputTick = kInputTickUnknown;
                pawn._bLeft         = SW_TRUE;
                if ( RemoteControllerComponent* pRemote = findRemoteController( *getOwner()->getManager(), pawn ) )
                    pRemote->clearReceived(); // 나간 사람의 마지막 의도를 되풀이하지 않는다
            }
            bFreeSeat = bFreeSeat || ( pawn._connectionID < 0 && pawn._bLeft == SW_FALSE );
        }
        if ( bFreeSeat == false )
            return;
        // 주체는 접속 티켓이 정한 계정이다 — 클라이언트가 보낸 값이 아니다. 매칭이 배정한 서버에서는 주체 없는 연결(티켓 없이 들어온 것)에 몸을 맡기지 않는다 —
        // 세션 역할만 보면 티켓을 요구하지 않는 설정의 매치 서버가 아무 연결에나 배정받은 사람의 몸을 넘긴다. 매칭 없는 서버(개발 접속 · 한 프로세스 재생)는 붙은 차례다.
        const bool bAccountRequired = ShooterNetPawnComponentInternal::findBackfillSender() != nullptr;
        _listConnectionScratch.clear();
        peers.collectInWorld( _listConnectionScratch ); // 씬을 읽는 중인 연결의 몸은 아직 세우지 않는다 — 조종할 사람이 없다
        for ( const int32 connectionID : _listConnectionScratch )
        {
            const uint64 accountID = peers.getConnectionPrincipal( connectionID );
            if ( ( bAccountRequired && accountID == 0 ) || VectorUtil::findBy<&ServerPawn::_connectionID>( _listServerPawn, connectionID ) != nullptr )
                continue;
            ServerPawn* pFree = nullptr;
            for ( ServerPawn& pawn : _listServerPawn )
            {
                if ( pFree == nullptr && pawn._connectionID < 0 && pawn._bLeft == SW_FALSE )
                    pFree = &pawn;
            }
            if ( pFree == nullptr )
                return;
            pFree->_connectionID               = connectionID;
            pFree->_accountID                  = accountID;
            pFree->_nextInputTick              = kInputTickUnknown;
            pFree->_bBodyRequested             = SW_FALSE;
            pFree->_bScoreReported             = SW_FALSE;
            RemoteControllerComponent* pRemote = findRemoteController( *getOwner()->getManager(), *pFree );
            if ( pRemote != nullptr )
            {
                pRemote->clearReceived();
                pRemote->setNextInputTick( pFree->_nextInputTick );
                pRemote->transferToConnection( connectionID ); // 다음 클라이언트가 맡는다 — 폰의 소유 연결도 따라간다
            }
        }
    }

    void ShooterNetPawnComponent::stepServerPawn( GameObjectManager& manager, ServerPawn& pawn, const ShooterGameModeComponent& gameMode )
    {
        using Internal                          = ShooterNetPawnComponentInternal;
        pawn._lastInputTick                     = nullopt;
        GameObject*                   pObject   = manager.resolveGameObject( pawn._object );
        ShooterBodyMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>() : nullptr;
        ShooterPlayerComponent*       pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        RemoteControllerComponent*    pRemote   = findRemoteController( manager, pawn );
        if ( pMovement == nullptr )
            return; // 막 앉아 몸이 아직 서지 않았다
        ControlIntent    intent;
        uint32           pawnEntityID = 0; // 자리는 연결이 정한다 — 입력이 적은 폰 번호는 쓰지 않는다
        optional<uint32> inputTick    = pawn._connectionID >= 0 ? _server.popNextIntent( pawn._connectionID, intent, pawnEntityID, _listClaim ) : nullopt;
        while ( inputTick.has_value() )
        {
            pawn._lastInputTick = inputTick;
            if ( pRemote != nullptr )
            {
                // 클라이언트는 접속 전에도 틱을 센다 — 조종을 맡은 뒤 처음 받은 틱에서 시작한다.
                if ( pawn._nextInputTick == kInputTickUnknown )
                    pRemote->setNextInputTick( *inputTick );
                pRemote->receiveIntent( *inputTick, intent ); // 폰의 시선 · 감시 값(다음 조종 시스템 틱). 판정은 아래 걸음이 한다
            }
            pawn._nextInputTick                         = *inputTick + 1u;
            pawn._viewTickByInput.acquire( *inputTick ) = intent._viewTick.getServerTick( static_cast<float64>( _tick ) );
            for ( const MeleeHitClaim& claim : _listClaim )
            {
                if ( claim._step <= *inputTick && pawn._listPendingClaim.size() < kMaxPendingClaim )
                    pawn._listPendingClaim.push_back( claim );
                else
                    ++_rejectedClaimCount; // 아직 오지 않은 걸음을 지어냈거나 줄이 찼다
            }
            const bool bDown = pPlayer != nullptr && pPlayer->isAlive() == false;                         // 쓰러진 몸은 걷지 않는다 — 같은 프레임의 앞 걸음에서 쓰러졌을 수 있다
            TickedFloorOffset::update( _tick, *inputTick, pawn._floorTickOffset, pawn._bFloorTickKnown ); // 명령마다 바닥은 그 명령의 틱이다
            pawn._floorTick = TickedFloorOffset::findTick( pawn._floorTickOffset, pawn._bFloorTickKnown, *inputTick ).value_or( static_cast<int64>( _tick ) );
            stepMovement( *pMovement, gameMode, pawn._floorTick, bDown ? Internal::makeStillIntent( intent ) : intent );
            if ( pPlayer != nullptr )
                pPlayer->stepNetCommand( manager, intent, _tickInterval, *inputTick ); // 이동한 자리에서 바로 쏜다 — 명령 하나가 한 걸음이다
            inputTick = _server.popNextIntent( pawn._connectionID, intent, pawnEntityID, _listClaim );
        }
        // 조종자가 있는데 입력이 오지 않았으면 그 자리다(언리얼 서버의 원격 폰 — 온 입력은 다음 틱에 몰아서 걷는다). 조종자가 없는 몸은 빈 의도로 계속 돈다(떨어지던 몸이 공중에 남지 않게).
        if ( pawn._connectionID < 0 )
        {
            pawn._floorTick = static_cast<int64>( _tick );
            stepMovement( *pMovement, gameMode, pawn._floorTick, ControlIntent{} );
        }
    }

    void ShooterNetPawnComponent::publishPawnStates( const GameObjectManager& manager )
    {
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            const uint32                        entityID  = makePawnEntityID( pawn );
            const GameObject*                   pObject   = manager.resolveGameObject( pawn._object );
            const ShooterBodyMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>() : nullptr;
            const ShooterPlayerComponent*       pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pMovement == nullptr || isPlaying( pawn ) == false )
            {
                _interest.removeEntity( entityID );
                continue;
            }
            const ShooterMoveState move = pMovement->getState();
            ShooterPawnNetState    state;
            state._position      = move._position;
            state._bOnGround     = move._bOnGround;
            state._teleportCount = pMovement->getTeleportCount();
            if ( pPlayer != nullptr )
            {
                state._yaw         = MathUtil::wrapAngle( pPlayer->getLookYaw() );
                state._weaponIndex = static_cast<uint8>( pPlayer->getWeaponIndex() );
                state._bAlive      = pPlayer->isAlive() ? SW_TRUE : SW_FALSE;
            }
            // 바닥 위의 몸은 바닥 기준으로 보낸다 — 받는 쪽은 자기가 그리는 시각의 바닥에 얹는다(절대 자리면 지연만큼 바닥에서 미끄러져 보인다).
            const ShooterGameModeComponent* pGameMode = findGameMode();
            float3                          floorPosition;
            float32                         floorYaw = 0.0f;
            if ( pGameMode != nullptr )
                computeFloorSteps( *pGameMode, pawn._floorTick, _listFloorScratch );
            const uint8 floorNumber = pGameMode != nullptr && state._bOnGround == SW_TRUE ? ShooterFloorStep::findNumber( _listFloorScratch, move._position ) : uint8{ 0 };
            if ( floorNumber != 0 && pGameMode->computeFloorPose( floorNumber, static_cast<float64>( pawn._floorTick ) * static_cast<float64>( _tickInterval ), floorPosition, floorYaw ) )
            {
                state._position    = ShooterFloorStep::toLocal( floorPosition, floorYaw, move._position );
                state._yaw         = MathUtil::wrapAngle( state._yaw - floorYaw );
                state._floorNumber = floorNumber;
            }
            if ( ShooterNetPawnComponentInternal::writeState( state, _bytesScratch ) )
                _server.setEntity( entityID, kPawnTypeID, _bytesScratch );
            else
                SW_LOG_ERROR_ID( "ShooterNetPawn.StateOutOfRange", "Pawn state is outside the replicated range (%#, %#, %#) - not replicated this tick", move._position._x, move._position._y, move._position._z );
            _interest.setEntityPosition( entityID, AbsolutePosition{ move._position } );
            if ( pawn._connectionID >= 0 )
                _interest.setObserver( pawn._connectionID, AbsolutePosition{ move._position } );
        }
    }

    void ShooterNetPawnComponent::sendOwnerStates( const GameObjectManager& manager )
    {
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            const GameObject*                   pObject   = manager.resolveGameObject( pawn._object );
            const ShooterBodyMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>() : nullptr;
            const ShooterPlayerComponent*       pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pMovement == nullptr || pawn._lastInputTick.has_value() == false )
                continue;
            ShooterOwnerNetState state;
            state._move            = pMovement->getState();
            state._pawnEntityID    = makePawnEntityID( pawn );
            state._floorTickOffset = pawn._floorTickOffset;
            state._bFloorTickKnown = pawn._bFloorTickKnown;
            if ( pPlayer != nullptr )
            {
                state._confirmedHitCount = pPlayer->getHitCount();
                state._killCount         = pPlayer->getKillCount();
                state._health            = pPlayer->getHealth();
                state._bOut              = pPlayer->isOut() ? SW_TRUE : SW_FALSE;
            }
            if ( writeOwnerState( state, _bytesScratch ) )
                (void)_server.setOwnerState( pawn._connectionID, *pawn._lastInputTick, _bytesScratch ); // 상태는 상한(작은 고정 크기) 안이다
        }
    }

    void ShooterNetPawnComponent::syncServerPawnList( GameObjectManager& manager )
    {
        for ( size_t seatIndex = 0; seatIndex < _listServerPawn.size(); ++seatIndex )
        {
            ServerPawn& pawn = _listServerPawn[seatIndex];
            if ( pawn._bLeft == SW_TRUE )
                finishLeftPlayer( manager, seatIndex );
            if ( pawn._connectionID < 0 || pawn._bBodyRequested == SW_TRUE )
                continue;
            pawn._bBodyRequested = SW_TRUE;
            if ( manager.resolveGameObject( pawn._object ) == nullptr )
                pawn._object = spawnServerBody( manager, seatIndex );
            possessRemote( manager, pawn );
        }
    }

    GameObjectHandle ShooterNetPawnComponent::spawnServerBody( GameObjectManager& manager, size_t seatIndex ) const
    {
        const GameObject*                   pSource         = getOwner();
        const PawnComponent*                pSourcePawn     = pSource->getComponent<PawnComponent>();
        const ShooterBodyMovementComponent* pSourceMovement = pSource->getComponent<ShooterBodyMovementComponent>();
        const ShooterPlayerComponent*       pSourcePlayer   = pSource->getComponent<ShooterPlayerComponent>();
        if ( pSourcePawn == nullptr || pSourceMovement == nullptr || pSourcePlayer == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.SceneBodyIncomplete", "Shooter net pawn: the scene body has no pawn, movement or player - no further player can be seated" );
            return GameObjectHandle{};
        }
        // 설정은 씬의 플레이어 몸 하나가 원본이다 — 서버의 폰이 클라이언트가 예측하는 몸과 다른 걸음 · 버튼 차례 · 무기를 갖지 않는다(프리팹에 한 벌 더 적지 않는다).
        // 화면 · 소리 · HUD 는 갖지 않는다. 카메라는 눈 자리와 조준만 들고 화면 후보가 아니다.
        GameObject* pObject = manager.createGameObject( hashed_string( "ShooterNetPawn" ) );
        pObject->setAlwaysLoaded( true ); // 존재는 접속이 정한다 — 스트리밍 셀이 내리지 않는다
        pObject->addComponent<CameraComponent>()->setRole( CameraRole::Custom );
        PawnComponent* pPawn = pObject->addComponent<PawnComponent>();
        pPawn->setButtonNames( pSourcePawn->getButtonNames() );
        pPawn->setAnalogNames( pSourcePawn->getAnalogNames() );
        ShooterBodyMovementComponent* pMovement = pObject->addComponent<ShooterBodyMovementComponent>();
        pMovement->copyTuning( *pSourceMovement );
        pMovement->teleport( AbsolutePosition{ _listGuestSpawn[seatIndex - 1u] } );
        pMovement->setNetDriven( true );
        (void)pObject->addComponent<FirstPersonCameraComponent>();
        pObject->addComponent<ShooterPlayerComponent>()->copySetup( *pSourcePlayer, _listGuestSpawn[seatIndex - 1u] );
        return pObject->getHandle();
    }

    void ShooterNetPawnComponent::possessRemote( GameObjectManager& manager, ServerPawn& pawn ) const
    {
        GameObject*    pObject = manager.resolveGameObject( pawn._object );
        PawnComponent* pPawn   = pObject != nullptr ? pObject->getComponent<PawnComponent>() : nullptr;
        if ( pPawn == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnNoPawn", "Shooter net pawn: the server body has no pawn - the client's shots and weapons do not reach it" );
            return;
        }
        if ( findRemoteController( manager, pawn ) != nullptr )
            return; // 앞사람의 조종자를 이어 맡았다(`assignSeats`)
        // 언리얼 서버가 접속마다 PlayerController 를 세우는 자리다 — 씬 데이터가 아니라 네트 역할이 정한다(혼자 하는 게임에는 없다). 씬과 함께 지워진다.
        // 앉은 연결로 세워 폰의 소유 연결(`PawnComponent::findInputConnection`)이 잡힌다.
        RemoteControllerComponent* pRemote = RemoteControllerComponent::spawnForConnection( manager, hashed_string( "ShooterRemoteController" ), pawn._connectionID, *pPawn );
        if ( pRemote == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnNoRemote", "Shooter net pawn: the remote controller could not be created - the client's shots and weapons do not reach the server body" );
            return;
        }
        pRemote->setNextInputTick( pawn._nextInputTick );
        pawn._remoteController = pRemote->getOwner()->getHandle();
    }

    void ShooterNetPawnComponent::finishLeftPlayer( GameObjectManager& manager, size_t seatIndex )
    {
        ServerPawn&             pawn      = _listServerPawn[seatIndex];
        const uint64            accountID = pawn._leftAccountID;
        GameObject*             pObject   = manager.resolveGameObject( pawn._object );
        ShooterPlayerComponent* pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        pawn._leftAccountID               = 0;
        pawn._bLeft                       = SW_FALSE;
        pawn._bBodyRequested              = SW_FALSE;
        pawn._listPendingClaim.clear();
        pawn._viewTickByInput.reset();
        if ( pPlayer != nullptr )
            reportMatchScore( pawn, *pPlayer, accountID );
        if ( accountID != 0 )
            ShooterNetPawnComponentInternal::reportPlayerLeft( accountID );
        if ( seatIndex != 0 )
        {
            GameObject* pRemote = manager.resolveGameObject( pawn._remoteController );
            if ( pRemote != nullptr )
                manager.destroyGameObject( pRemote );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
            pawn._remoteController = GameObjectHandle{};
            pawn._object           = GameObjectHandle{};
            _interest.removeEntity( makePawnEntityID( pawn ) );
        }
        if ( isMatchServer() == false )
            return;
        // 매치 서버의 판은 아무도 남지 않았을 때 다음에 배정받은 사람이 처음부터 잇는다 — 앞사람의 처치 수 · 적을 물려받지 않는다. 남은 사람이 있으면 그 판은 이어지고 빈 몸만 되돌린다.
        ShooterPlayerComponent* const pScenePlayer = getOwner()->getComponent<ShooterPlayerComponent>();
        if ( pScenePlayer != nullptr && hasConnectedPlayer() == false )
            pScenePlayer->restartRound();
        else if ( seatIndex == 0 && pPlayer != nullptr )
            pPlayer->resetMatch();
    }

    bool ShooterNetPawnComponent::hasConnectedPlayer() const
    {
        bool bConnected = false;
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            bConnected = bConnected || pawn._connectionID >= 0;
        }
        return bConnected;
    }

    bool ShooterNetPawnComponent::isPlaying( const ServerPawn& pawn ) const
    {
        return pawn._connectionID >= 0 || ( &pawn == _listServerPawn.data() && hasConnectedPlayer() == false );
    }

    const ShooterNetPawnComponent::ServerPawn* ShooterNetPawnComponent::findServerPawn( GameObjectHandle object ) const
    {
        return VectorUtil::findBy<&ServerPawn::_object>( _listServerPawn, object );
    }

    bool ShooterNetPawnComponent::hasController( const GameObject& pawnObject ) const
    {
        const ServerPawn* const pPawn = findServerPawn( pawnObject.getHandle() );
        return pPawn != nullptr && pPawn->_connectionID >= 0;
    }

    void ShooterNetPawnComponent::collectPlayerObjects( vector<GameObjectHandle>& outListObject ) const
    {
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            if ( isPlaying( pawn ) && pawn._object != GameObjectHandle{} )
                outListObject.push_back( pawn._object );
        }
        if ( outListObject.empty() && getOwner() != nullptr )
            outListObject.push_back( getOwner()->getHandle() );
    }

    void ShooterNetPawnComponent::computeFloorSteps( const ShooterGameModeComponent& gameMode, optional<int64> floorTick, vector<ShooterFloorStep>& outListFloor ) const
    {
        outListFloor.clear();
        if ( floorTick.has_value() == false )
            return; // 서버가 아직 바닥 틱을 알려 주지 않았다
        const float64 interval = static_cast<float64>( _tickInterval );
        gameMode.computeFloorSteps( static_cast<float64>( *floorTick - 1 ) * interval, static_cast<float64>( *floorTick ) * interval, outListFloor );
    }

    optional<int64> ShooterNetPawnComponent::findDisplayFloorTick() const
    {
        return TickedFloorOffset::findTick( _floorTickOffset, _bFloorTickKnown, _tick );
    }

    void ShooterNetPawnComponent::stepMovement( ShooterBodyMovementComponent& movement, const ShooterGameModeComponent& gameMode, int64 floorTick, const ControlIntent& intent )
    {
        computeFloorSteps( gameMode, floorTick, _listFloorScratch );
        const ShooterMoveState before = movement.getState();
        const ShooterMoveState next   = ShooterNetPawnComponentInternal::simulateTick( movement, gameMode.getBoxes(), _listFloorScratch, before, intent, _tickInterval );
        movement.setState( next, float3{ next._position._x - before._position._x, 0.0f, next._position._z - before._position._z } * ( 1.0f / _tickInterval ) );
    }

    // ------------------------------------------------------------------------------
    // 클라이언트
    // ------------------------------------------------------------------------------
    void ShooterNetPawnComponent::runClientFrame( GameObjectManager& manager )
    {
        const int32 stepCount                     = _pendingStepCount;
        _pendingStepCount                         = 0;
        ShooterBodyMovementComponent*   pMovement = findMovement();
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( _role != Role::Client || pMovement == nullptr || pGameMode == nullptr )
            return;
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepClient( manager, *pMovement, *pGameMode );
        }
        pMovement->setDisplayOffset( _smoother.getOffset() );
        syncRemotePawnList( manager );
        // 이번 프레임에 그린 시각이다 — 다음 프레임의 입력은 이 화면을 보고 낸 것이라 발동에 이 값이 실린다.
        ControlSystem* const pControl = ControlSystem::find( manager );
        if ( pControl != nullptr )
            pControl->publishViewTick( _client.getViewTick() );
    }

    void ShooterNetPawnComponent::stepClient( GameObjectManager& manager, ShooterBodyMovementComponent& movement, const ShooterGameModeComponent& gameMode )
    {
        ++_tick;
        // 예측하는 이 입력 틱의 바닥은 서버가 정해 준 차이만큼 앞의 틱이다(소유자 상태) — 서버가 그 명령을 푸는 바닥과 같고, 보이는 바닥도 그 틱을 따른다.
        const optional<int64> floorTick = findDisplayFloorTick();
        if ( floorTick.has_value() )
            ShooterNetPawnComponentInternal::publishServerTick( getOwner(), *floorTick * static_cast<int64>( _stride ) );
        computeFloorSteps( gameMode, floorTick, _listFloorScratch );
        const ControlIntent intent = takeNetIntent( movement );
        // 입력 하나의 상한을 넘는 주장은 다음 입력 틱에 싣는다. 주장의 걸음은 휘두른 입력 틱이다.
        const size_t claimCount = MathUtil::min( _listClaim.size(), static_cast<size_t>( NetClientServerMessage::kMaxClaimsPerIntent ) );
        if ( _client.sendIntent( _tick, intent, vector_reference<const MeleeHitClaim>( _listClaim.data(), claimCount ) ) == false )
            SW_LOG_WARNING_ID( "ShooterNetPawn.ShooterNetPawnInput", "Shooter net pawn: intent of tick %# was not sent", _tick );
        _sentClaimCount += static_cast<uint32>( claimCount );
        _listClaim.erase( _listClaim.begin(), _listClaim.begin() + static_cast<std::ptrdiff_t>( claimCount ) );
        const ShooterMoveState before  = movement.getState();
        ShooterMoveState       current = ShooterNetPawnComponentInternal::simulateTick( movement, gameMode.getBoxes(), _listFloorScratch, before, intent, _tickInterval );
        if ( _prediction.record( _tick, intent, current ) == false )
            SW_LOG_WARNING_ID( "ShooterNetPawn.ShooterNetPawnPrediction", "Shooter net pawn: prediction tick %# does not follow %# - not recorded", _tick, _prediction.getLatestTick() );

        // 서버가 새 소유자 상태(그 입력 틱까지 처리한 내 몸)를 보냈으면 예측을 맞춘다.
        const NetOwnerState* pOwner = _client.findOwnerState();
        ShooterOwnerNetState serverOwner;
        const bool           bNewOwnerState = pOwner != nullptr && pOwner->_bPresent == SW_TRUE && pOwner->_inputTick > _reconciledTick && readOwnerState( pOwner->_buffer, serverOwner );
        if ( bNewOwnerState )
        {
            _reconciledTick                     = pOwner->_inputTick;
            _pawnEntityID                       = serverOwner._pawnEntityID;
            _floorTickOffset                    = serverOwner._floorTickOffset;
            _bFloorTickKnown                    = serverOwner._bFloorTickKnown;
            const ShooterMoveState& serverState = serverOwner._move;
            ShooterPlayerComponent* pPlayer     = getOwner()->getComponent<ShooterPlayerComponent>();
            if ( pPlayer != nullptr )
                pPlayer->applyServerReport( serverOwner._confirmedHitCount, serverOwner._killCount, serverOwner._health, serverOwner._bOut == SW_TRUE );
            const float3                    shown      = current._position;
            uint32                          replayTick = pOwner->_inputTick; // 다시 돌리는 명령은 서버 틱 다음부터 하나씩 차례다
            const PredictionReconcileResult result     = _prediction.reconcile(
                pOwner->_inputTick, serverState, current,
                [&movement, &gameMode, &replayTick, this]( const ShooterMoveState& state, const ControlIntent& tickIntent )
            {
                ++replayTick;
                computeFloorSteps( gameMode, TickedFloorOffset::findTick( _floorTickOffset, _bFloorTickKnown, replayTick ), _listFloorScratch );
                return ShooterNetPawnComponentInternal::simulateTick( movement, gameMode.getBoxes(), _listFloorScratch, state, tickIntent, _tickInterval );
            },
                [this]( const ShooterMoveState& left, const ShooterMoveState& right )
            { return isClose( left, right ); } );
            switch ( result )
            {
                case PredictionReconcileResult::Corrected:
                {
                    _smoother.addCorrection( shown, current._position ); // 보정한 만큼 화면에서 녹인다
                    break;
                }
                case PredictionReconcileResult::TickOverwritten:
                {
                    // 예측이 기록보다 앞서 있다 — 서버 상태로 바로 맞춘다(남은 입력은 기록에 없다).
                    _smoother.addCorrection( shown, serverState._position );
                    current = serverState;
                    break;
                }
                case PredictionReconcileResult::Matched:
                case PredictionReconcileResult::TickAhead:
                {
                    break;
                }
            }
        }
        movement.setState( current, float3{ current._position._x - before._position._x, 0.0f, current._position._z - before._position._z } * ( 1.0f / _tickInterval ) );
        float3  floorPosition;
        float32 floorYaw   = 0.0f;
        _floorNumber       = current._bOnGround == SW_TRUE ? ShooterFloorStep::findNumber( _listFloorScratch, current._position ) : uint8{ 0 };
        const bool bRiding = _floorNumber != 0 && floorTick.has_value() && gameMode.computeFloorPose( _floorNumber, static_cast<float64>( *floorTick ) * static_cast<float64>( _tickInterval ), floorPosition, floorYaw );
        if ( bRiding )
            _floorLocalPosition = ShooterFloorStep::toLocal( floorPosition, floorYaw, current._position );
        // 서버와 같은 순서다 — 이동한 자리에서 이 명령의 발동을 예측한다.
        ShooterPlayerComponent* const pPlayer = getOwner()->getComponent<ShooterPlayerComponent>();
        if ( pPlayer != nullptr && movement.isSuspended() == false )
            pPlayer->stepNetCommand( manager, intent, _tickInterval, _tick );
    }

    ShooterBodyMovementComponent* ShooterNetPawnComponent::findMovement() const
    {
        GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<ShooterBodyMovementComponent>() : nullptr;
    }

    const PawnComponent* ShooterNetPawnComponent::findPawn() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
    }

    const ShooterPlayerComponent* ShooterNetPawnComponent::findPlayer() const
    {
        const GameObject* pOwner = getOwner();
        return pOwner != nullptr ? pOwner->getComponent<ShooterPlayerComponent>() : nullptr;
    }

    RemoteControllerComponent* ShooterNetPawnComponent::findRemoteController( const GameObjectManager& manager, const ServerPawn& pawn )
    {
        const GameObject* pObject = manager.resolveGameObject( pawn._remoteController );
        return pObject != nullptr ? pObject->getComponent<RemoteControllerComponent>() : nullptr;
    }

    void ShooterNetPawnComponent::collectPendingTriggers( const ShooterBodyMovementComponent& movement )
    {
        const PawnComponent* pPawn = findPawn();
        if ( pPawn == nullptr || movement.isSuspended() )
            return;
        // 줄에는 상한이 있다 — 입력 틱이 오래 멈춘 동안(끊김)의 누름을 끝없이 쌓지 않는다.
        constexpr size_t              kMaxPendingTrigger = static_cast<size_t>( ControlIntent::kMaxTriggers ) * 4u;
        const vector<ControlTrigger>& listTrigger        = pPawn->getIntent()._listTrigger;
        for ( const ControlTrigger& trigger : listTrigger )
        {
            if ( _listPendingTrigger.size() < kMaxPendingTrigger )
                _listPendingTrigger.push_back( trigger );
        }
    }

    ControlIntent ShooterNetPawnComponent::takeNetIntent( const ShooterBodyMovementComponent& movement )
    {
        ControlIntent        intent;
        const PawnComponent* pPawn = findPawn();
        if ( pPawn != nullptr )
        {
            const ShooterPlayerComponent* pPlayer = findPlayer();
            const bool                    bStill  = movement.isSuspended() || ( pPlayer != nullptr && pPlayer->isServerDown() ); // 서버가 쓰러뜨린 몸은 서버에서도 걷지 않는다
            intent                                = bStill ? ShooterNetPawnComponentInternal::makeStillIntent( pPawn->getIntent() ) : pPawn->getIntent();
        }
        // 선에 싣는 상한을 넘는 사건은 다음 입력 틱이 싣는다(사건마다 보던 시각을 들어 늦어도 판정이 같다).
        const size_t triggerCount = MathUtil::min( _listPendingTrigger.size(), static_cast<size_t>( ControlIntent::kMaxTriggers ) );
        intent._listTrigger.assign( _listPendingTrigger.begin(), _listPendingTrigger.begin() + static_cast<std::ptrdiff_t>( triggerCount ) );
        _listPendingTrigger.erase( _listPendingTrigger.begin(), _listPendingTrigger.begin() + static_cast<std::ptrdiff_t>( triggerCount ) );
        return intent;
    }

    const ShooterGameModeComponent* ShooterNetPawnComponent::findGameMode() const
    {
        const GameObject*             pOwner   = getOwner();
        const GameObjectManager*      pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ShooterPlayerComponent* pPlayer  = findPlayer();
        return pManager != nullptr && pPlayer != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
    }

    void ShooterNetPawnComponent::recordBodies( const GameObjectManager& manager )
    {
        _listLagScratch.clear();
        _listEntityScratch.clear();
        for ( const ServerPawn& pawn : _listServerPawn )
        {
            const GameObject*                   pObject   = manager.resolveGameObject( pawn._object );
            const ShooterBodyMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>() : nullptr;
            const ShooterPlayerComponent*       pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
            if ( pMovement == nullptr || pPlayer == nullptr || pawn._connectionID < 0 || pPlayer->isAlive() == false )
                continue;
            LagRecord& record = _listLagScratch.emplace_back();
            record._position  = pMovement->getFeetPosition()._value;
            record._radius    = pMovement->getRadius();
            record._height    = _pawnHitHeight;
            record._entityID  = makePawnEntityID( pawn );
            // 바닥 위의 몸은 바닥 기준으로 기억한다 — 쏜 사람은 그 몸을 자기 시각의 바닥 위에서 봤다.
            const ShooterGameModeComponent* pArena = findGameMode();
            float3                          floorPosition;
            float32                         floorYaw = 0.0f;
            if ( pArena != nullptr )
                computeFloorSteps( *pArena, pawn._floorTick, _listFloorScratch );
            const uint8 floorNumber = pArena != nullptr && pMovement->isOnGround() ? ShooterFloorStep::findNumber( _listFloorScratch, record._position ) : uint8{ 0 };
            if ( floorNumber != 0 && pArena->computeFloorPose( floorNumber, static_cast<float64>( pawn._floorTick ) * static_cast<float64>( _tickInterval ), floorPosition, floorYaw ) )
            {
                record._position    = ShooterFloorStep::toLocal( floorPosition, floorYaw, record._position );
                record._frameNumber = floorNumber;
            }
            record._teleportCount = pMovement->getTeleportCount();
        }
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( pGameMode != nullptr )
        {
            for ( const ShooterEnemyView& listed : pGameMode->getEnemyViews() )
            {
                // 감독의 목록은 이번 프레임 앞의 자리다 — 누가 있는지만 쓰고 자리 · 자세는 물리가 끝난 지금 읽는다. 복제 상태와 되감기 기록이 같은 한 시점이다.
                const GameObject*            pObject = manager.resolveGameObject( listed._object );
                const ShooterEnemyComponent* pEnemy  = pObject != nullptr ? pObject->getComponent<ShooterEnemyComponent>() : nullptr;
                if ( pEnemy == nullptr || pEnemy->isAlive() == false )
                    continue; // 이 프레임의 앞 걸음에서 쓰러졌다
                ShooterEnemyView view                          = listed;
                view._position                                 = pEnemy->getPosition();
                view._yaw                                      = pEnemy->getYaw();
                view._phase                                    = pEnemy->getPhase();
                const ShooterBodyMovementComponent* pEnemyBody = pObject->getComponent<ShooterBodyMovementComponent>();
                view._teleportCount                            = pEnemyBody != nullptr ? pEnemyBody->getTeleportCount() : uint8{ 0 };
                LagRecord& record                              = _listLagScratch.emplace_back();
                record._position                               = view._position._value;
                record._radius                                 = view._radius;
                record._height                                 = view._height;
                record._entityID                               = view._netEntityID;
                record._teleportCount                          = view._teleportCount;
                collectBodyPoses( manager, *pObject, record._listBody );
                if ( ShooterNetPawnComponentInternal::writeEnemy( view, _bytesScratch ) )
                    _server.setEntity( view._netEntityID, kEnemyTypeID, _bytesScratch );
                else
                    SW_LOG_ERROR_ID( "ShooterNetPawn.EnemyOutOfRange", "Enemy %# is outside the replicated range (%#, %#, %#) - not replicated this tick", view._netEntityID,
                                     view._position._value._x, view._position._value._y, view._position._value._z );
                _interest.setEntityPosition( view._netEntityID, view._position );
                _listEntityScratch.push_back( view._netEntityID );
            }
        }
        _lagHistory.record( _tick, _listLagScratch );
        VectorUtil::sort( _listEntityScratch );
        for ( const uint32 entityID : _listEnemyEntityID )
        {
            if ( std::binary_search( _listEntityScratch.begin(), _listEntityScratch.end(), entityID ) == false )
                _interest.removeEntity( entityID );
        }
        _listEnemyEntityID.swap( _listEntityScratch );
    }

    void ShooterNetPawnComponent::queueMeleeClaim( uint32 enemyEntityID, uint32 commandTick )
    {
        MeleeHitClaim& claim = _listClaim.emplace_back();
        claim._step          = commandTick;
        claim._targetID      = enemyEntityID;
    }

    void ShooterNetPawnComponent::reportMatchScore( ServerPawn& pawn, const ShooterPlayerComponent& player, uint64 accountID )
    {
        const MinigameFlow* const       pFlow     = game::findSubsystem<MinigameFlow>();
        const MinigameDefinition* const pActive   = pFlow != nullptr ? pFlow->findActiveDefinition() : nullptr;
        GameObjectManager* const        pManager  = getOwner()->getMutableManager();
        GameObject* const               pObject   = pManager != nullptr ? pManager->resolveGameObject( player.getGameMode() ) : nullptr;
        ShooterGameModeComponent* const pGameMode = pObject != nullptr ? pObject->getComponent<ShooterGameModeComponent>() : nullptr;
        if ( pGameMode == nullptr || isMatchServer() == false || accountID == 0 || pawn._bScoreReported == SW_TRUE )
            return;                                             // 매치 서버가 아니거나(개발 접속 포함) 이 사람의 판을 이미 냈다
        pGameMode->setResultKillCount( player.getKillCount() ); // 결과는 사람마다다 — 어떤 값이 점수인지는 미니게임 데이터가 고른다
        const optional<int64> score = pFlow->findMatchScore();
        if ( score.has_value() == false )
            return;
        pawn._bScoreReported                      = SW_TRUE;
        const optional<ShooterServerKits> kits    = ShooterServerKits::resolve( game::findSubsystem<IServerKitLocator>() );
        ILeaderboardScoreSender* const    pSender = kits.has_value() ? kits->find<ILeaderboardScoreSender>() : nullptr;
        if ( pSender == nullptr )
        {
            SW_LOG_ERROR_ID( "Shooter.MatchScoreNoLeaderboard", "[Shooter] the match score %# for the board '%#' is lost - the server config lists no leaderboard kit", *score, pActive->_matchBoard.c_str() );
            return;
        }
        const LeaderboardResult result = pSender->submitScore( pActive->_matchBoard, static_cast<AccountID>( accountID ), *score, NetIdempotencyKey::makeRandom(), WallClock::nowUnixMilliseconds() );
        if ( result == LeaderboardResult::Ok )
            SW_LOG_INFO_ID( "Shooter.MatchScoreReported", "[Shooter] match score of account %# reported: %# = %#", accountID, pActive->_matchBoard.c_str(), *score );
        else
            SW_LOG_ERROR_ID( "Shooter.MatchScoreRefused", "[Shooter] the match score %# for the board '%#' was refused (result %#)", *score, pActive->_matchBoard.c_str(), static_cast<int32>( result ) );
    }

    void ShooterNetPawnComponent::reportMatchScore( const GameObject& pawnObject )
    {
        ServerPawn* const             pPawn   = VectorUtil::findBy<&ServerPawn::_object>( _listServerPawn, pawnObject.getHandle() );
        const ShooterPlayerComponent* pPlayer = pawnObject.getComponent<ShooterPlayerComponent>();
        if ( _role == Role::Server && pPawn != nullptr && pPlayer != nullptr )
            reportMatchScore( *pPawn, *pPlayer, pPawn->_accountID );
    }

    bool ShooterNetPawnComponent::isMatchServer() const
    {
        const MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        return _role == Role::Server && pFlow != nullptr && pFlow->findMatchScore().has_value();
    }

    void ShooterNetPawnComponent::reportSeatCapacity()
    {
        IMatchBackfillSender* const pSender = ShooterNetPawnComponentInternal::findBackfillSender();
        if ( pSender != nullptr )
            pSender->setSeatCapacity( static_cast<int32>( _listServerPawn.size() ) ); // 관전 · 공동 조종이 없다 — 자리 수가 정원이다
    }

    void ShooterNetPawnComponent::resolveClaims( GameObjectManager& manager, ServerPawn& pawn )
    {
        if ( pawn._listPendingClaim.empty() )
            return;
        GameObject*                     pObject   = manager.resolveGameObject( pawn._object );
        ShooterPlayerComponent*         pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        const MeleeSweepHistory*        pLedger   = MeleeSweepHistory::find( manager );
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( pLedger == nullptr || pPlayer == nullptr || pGameMode == nullptr )
        {
            pawn._listPendingClaim.clear();
            return;
        }
        size_t keptCount = 0;
        for ( const MeleeHitClaim& claim : pawn._listPendingClaim )
        {
            // 그 걸음 안팎의 입력을 모두 처리해야 스윕이 다 적힌다 — 입력이 몰려 와도 걸음 번호로 맞춘다.
            if ( pawn._nextInputTick <= claim._step + MeleeSweepHistory::kClaimSlackSteps )
            {
                pawn._listPendingClaim[keptCount++] = claim;
                continue;
            }
            const uint32            entityID = static_cast<uint32>( claim._targetID );
            const ShooterEnemyView* pView    = nullptr;
            for ( const ShooterEnemyView& view : pGameMode->getEnemyViews() )
            {
                if ( view._netEntityID == entityID )
                    pView = &view;
            }
            LagRecord      past;
            GameObject*    pTarget   = pView != nullptr ? manager.resolveGameObject( pView->_object ) : nullptr;
            const float64* pViewTick = pawn._viewTickByInput.find( claim._step );
            if ( pTarget == nullptr || pViewTick == nullptr || _lagHistory.sampleAt( *pViewTick, entityID, past ) == false )
            {
                ++_rejectedClaimCount; // 표적이 그 사이 쓰러졌거나 지어낸 번호 · 걸음이다
                continue;
            }
            // 장부는 표적을 구로 본다 — 몸 가운데에 몸 전체를 덮는 구다(근접은 뼈마다 가르지 않는다).
            const float3              center         = manager.getWorldOrigin().toLocal( past._position + float3{ 0.0f, past._height * 0.5f, 0.0f } ); // 장부의 칼날은 씬 자리다
            const float32             radius         = MathUtil::max( past._radius, past._height * 0.5f );
            vector<GameObjectHandle>& listHit        = pPlayer->getMeleeHitTargets();
            const size_t              hitCountBefore = listHit.size();
            if ( pLedger->claimHit( *pObject, *pTarget, claim, center, radius, listHit ) != MeleeClaimResult::Confirmed )
            {
                ++_rejectedClaimCount;
                continue;
            }
            ++_confirmedClaimCount;
            if ( listHit.size() > hitCountBefore )
                pPlayer->recordMeleeHit(); // 서버 스윕이 먼저 맞히지 못한 것만 — 창 하나에 한 번이다
        }
        pawn._listPendingClaim.resize( keptCount );
    }

    void ShooterNetPawnComponent::collectReplicatedEnemies( vector<ShooterReplicatedEnemy>& outListEnemy ) const
    {
        vector<uint32> listEntity;
        _client.collectVisibleEntities( listEntity );
        for ( const uint32 entityID : listEntity )
        {
            const optional<NetStateSample<ShooterEnemyNetState>> sample = _client.sampleState<ShooterEnemyNetState>( entityID, kEnemyTypeID );
            if ( sample.has_value() == false )
                continue;
            const ShooterEnemyNetState& from  = sample->_from;
            const ShooterEnemyNetState& to    = sample->_to;
            ShooterReplicatedEnemy&     enemy = outListEnemy.emplace_back();
            enemy._netEntityID                = entityID;
            enemy._position                   = AbsolutePosition{ sample->_ratio.interpolate( from._position, to._position ) };
            enemy._velocity                   = sample->_ratio.computeVelocity( from._position, to._position, _tickInterval );
            enemy._yaw                        = sample->_ratio.interpolateAngle( from._yaw, to._yaw );
            enemy._phase                      = static_cast<ShooterEnemyPhase>( sample->_ratio.choose( from._phase, to._phase ) );
            enemy._kindIndex                  = sample->_ratio.choose( from._kindIndex, to._kindIndex );
            enemy._appearanceSeed             = sample->_ratio.choose( from._appearanceSeed, to._appearanceSeed );
        }
    }

    void ShooterNetPawnComponent::syncRemotePawnList( GameObjectManager& manager )
    {
        const ShooterPlayerComponent* pSelf = findPlayer();
        if ( _role != Role::Client || pSelf == nullptr )
            return;
        for ( RemotePawn& remote : _listRemotePawn )
        {
            remote._bSeen = SW_FALSE;
        }
        _listEntityScratch.clear();
        if ( _pawnEntityID != 0 ) // 내 몸을 알기 전에는 남을 가리지 못한다
            _client.collectVisibleEntities( _listEntityScratch );
        for ( const uint32 entityID : _listEntityScratch )
        {
            const optional<NetStateSample<ShooterPawnNetState>> sample = entityID != _pawnEntityID ? _client.sampleState<ShooterPawnNetState>( entityID, kPawnTypeID ) : nullopt;
            if ( sample.has_value() == false )
                continue;
            const ShooterPawnNetState& from        = sample->_from;
            const ShooterPawnNetState& to          = sample->_to;
            const uint8                weaponIndex = sample->_ratio.choose( from._weaponIndex, to._weaponIndex );
            RemotePawn*                pRemote     = VectorUtil::findBy<&RemotePawn::_entityID>( _listRemotePawn, entityID );
            if ( pRemote == nullptr )
            {
                pRemote            = &_listRemotePawn.emplace_back();
                pRemote->_entityID = entityID;
                pRemote->_object   = pSelf->spawnBodyObject( manager, "RemotePlayerBody" );
                pRemote->_bArmed   = SW_FALSE;
            }
            NetSampleRatio ratio = sample->_ratio;
            ratio.breakAt( from._floorNumber, to._floorNumber ); // 바닥을 옮겨 탄 두 자리는 기준이 다르다
            ShooterAvatarPose pose;
            pose._feet            = AbsolutePosition{ ratio.interpolate( from._position, to._position ) };
            pose._yaw             = ratio.interpolateAngle( from._yaw, to._yaw );
            pose._bAlive          = ratio.choose( from._bAlive, to._bAlive );
            pose._bOnGround       = ratio.choose( from._bOnGround, to._bOnGround );
            pose._bTwoHanded      = pSelf->getWeapon( weaponIndex ).getDef()._bTwoHanded;
            const float3 velocity = ratio.computeVelocity( from._position, to._position, _tickInterval );
            // 바닥 위의 남은 이 화면의 바닥에 얹는다 — 바닥은 예측 시각에 그려진다.
            const uint8                     floorNumber = ratio.choose( from._floorNumber, to._floorNumber );
            const optional<int64>           floorTick   = findDisplayFloorTick();
            const ShooterGameModeComponent* pArena      = findGameMode();
            float3                          floorPosition;
            float32                         floorYaw = 0.0f;
            if ( floorNumber != 0 )
            {
                const bool bPosed = pArena != nullptr && floorTick.has_value() && pArena->computeFloorPose( floorNumber, static_cast<float64>( *floorTick ) * static_cast<float64>( _tickInterval ), floorPosition, floorYaw );
                if ( bPosed == false )
                    continue; // 이 씬에 없는 바닥이거나 아직 바닥 틱을 모른다
                pose._feet = AbsolutePosition{ ShooterFloorStep::toAbsolute( floorPosition, floorYaw, pose._feet._value ) };
                pose._yaw  = MathUtil::wrapAngle( pose._yaw + floorYaw );
            }
            pose._velocity               = float3{ velocity._x, 0.0f, velocity._z };
            pRemote->_position           = pose._feet;
            pRemote->_floorNumber        = floorNumber;
            pRemote->_floorLocalPosition = floorNumber != 0 ? ShooterFloorStep::toLocal( floorPosition, floorYaw, pose._feet._value ) : float3{};

            GameObject*             pObject = manager.resolveGameObject( pRemote->_object );
            ShooterAvatarComponent* pAvatar = pObject != nullptr ? pObject->getComponent<ShooterAvatarComponent>() : nullptr;
            if ( pAvatar != nullptr )
                pAvatar->setReplicatedPose( pose );
            CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr && ( pRemote->_bArmed == SW_FALSE || pRemote->_weaponIndex != weaponIndex ) )
                pAppearance->setSlotItem( hashed_string( "MainHand" ), pSelf->getWeaponItem( weaponIndex ) );
            pRemote->_weaponIndex = weaponIndex;
            pRemote->_bArmed      = SW_TRUE;
            pRemote->_bSeen       = SW_TRUE;
        }
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            if ( remote._bSeen == SW_FALSE )
                ShooterPlayerComponent::destroyBodyObject( manager, remote._object );
        }
        VectorUtil::removeIf( _listRemotePawn, []( const RemotePawn& remote )
        { return remote._bSeen == SW_FALSE; } );
    }

    void ShooterNetPawnComponent::clearRemotePawnList( GameObjectManager& manager )
    {
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            ShooterPlayerComponent::destroyBodyObject( manager, remote._object );
        }
        _listRemotePawn.clear();
        _pawnEntityID = 0;
    }

    optional<AbsolutePosition> ShooterNetPawnComponent::findNearestRemotePawn( const AbsolutePosition& from ) const
    {
        optional<AbsolutePosition> nearest;
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            if ( nearest.has_value() == false || float3::getDistance( remote._position._value, from._value ) < float3::getDistance( nearest->_value, from._value ) )
                nearest = remote._position;
        }
        return nearest;
    }

    int32 ShooterNetPawnComponent::getRemoteFloorNumber() const
    {
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            if ( remote._floorNumber != 0 )
                return static_cast<int32>( remote._floorNumber );
        }
        return 0;
    }

    float3 ShooterNetPawnComponent::getRemoteFloorLocalPosition() const
    {
        for ( const RemotePawn& remote : _listRemotePawn )
        {
            if ( remote._floorNumber != 0 )
                return remote._floorLocalPosition;
        }
        return float3{};
    }

    void ShooterNetPawnComponent::collectBodyPoses( const GameObjectManager& manager, const GameObject& object, vector<LagBodyPose>& outListBody )
    {
        outListBody.clear();
        const RagdollComponent*    pRagdoll = object.getComponent<RagdollComponent>();
        const IPhysicsBodyScene3D* pScene   = manager.getSubsystem<ScenePhysics>().findScene3D();
        if ( pRagdoll == nullptr || pScene == nullptr )
            return;
        for ( const PhysicsBodyHandle body : pRagdoll->getRagdoll()._listBody )
        {
            LagBodyPose& pose = outListBody.emplace_back();
            if ( pScene->getBodyTransform( body, pose._position, pose._rotation ) == false )
            {
                outListBody.clear();
                return; // 바디가 막 사라졌다 — 일부만으로 판정하지 않는다
            }
            pose._position = manager.getWorldOrigin().toAbsolute( pose._position );
        }
    }

    bool ShooterNetPawnComponent::raycastRewoundBodies( const GameObjectManager& manager, const GameObject& object, const LagRecord& past, const Ray& ray, float32 maxDistance, uint32 layerMask, CharacterRayHit& outHit )
    {
        const RagdollComponent*     pRagdoll = object.getComponent<RagdollComponent>();
        const IPhysicsQueryScene3D* pScene   = manager.getSubsystem<ScenePhysics>().findScene3D();
        vector<LagBodyPose>         listNow;
        collectBodyPoses( manager, object, listNow );
        // 바디 수가 다르면 그 사이 바디가 다시 만들어졌다 — 짝이 없어 과거 자세로 물을 수 없다.
        if ( pRagdoll == nullptr || pScene == nullptr || listNow.empty() || listNow.size() != past._listBody.size() )
            return false;
        const WorldOrigin& origin      = manager.getWorldOrigin();
        Ray                absoluteRay = ray;
        absoluteRay._origin            = origin.toAbsolute( ray._origin );
        PhysicsQueryFilter filter;
        filter._layerMask             = layerMask;
        bool                     bHit = false;
        vector<PhysicsCastHit3D> listHit;
        for ( size_t bodyIndex = 0; bodyIndex < listNow.size(); ++bodyIndex )
        {
            // 과거 자세의 바디를 쏜 광선을 지금 자세의 바디로 옮겨 실제 셰이프에 묻는다. 다른 바디에 먼저 닿아도 이 바디만 본다.
            const Ray               moved = past._listBody[bodyIndex].moveRayTo( listNow[bodyIndex], absoluteRay );
            const PhysicsBodyHandle body  = pRagdoll->getRagdoll()._listBody[bodyIndex];
            listHit.clear();
            (void)pScene->raycastAll( origin.toLocal( moved._origin ), moved._direction, maxDistance, filter, listHit );
            for ( const PhysicsCastHit3D& hit : listHit )
            {
                if ( hit._body != body || ( bHit && hit._distance >= outHit._distance ) )
                    continue;
                bHit             = true;
                outHit._pObject  = &object;
                outHit._body     = body;
                outHit._point    = ray.getPoint( hit._distance ); // 맞은 자리는 쏜 사람이 본 자리다
                outHit._normal   = ray._direction * -1.0f;
                outHit._material = hit._material;
                outHit._distance = hit._distance;
                outHit._bIs2D    = false;
            }
        }
        return bHit;
    }

    bool ShooterNetPawnComponent::raycastRewound( const GameObjectManager& manager, const GameObject& shooter, const Ray& ray, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID, ControlViewTick viewTick,
                                                  CharacterRayHit& outHit )
    {
        const ServerPawn* const         pShooter  = findServerPawn( shooter.getHandle() );
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( pShooter == nullptr || pGameMode == nullptr )
            return false; // 자리가 없는 몸은 쏘지 않는다
        ++_rewoundShotCount;
        const float64 presentTick = static_cast<float64>( _tick );
        const float64 seenTick    = viewTick.getServerTick( presentTick );
        _lastRewindTicks          = presentTick - seenTick;
        Ray absoluteRay           = ray;
        absoluteRay._origin       = manager.getWorldOrigin().toAbsolute( ray._origin );
        vector<LagRecord> listPast;
        collectRewoundBodies( *pShooter, seenTick, listPast );
        bool bHit = false;
        for ( const LagRecord& past : listPast )
        {
            CharacterRayHit hit;
            if ( past._entityID < kEnemyEntityIDBase )
            {
                // 다른 플레이어다 — 되감은 캡슐이 곧 맞음 상자다(몸 프리팹에 히트박스가 없다).
                const size_t seatIndex = past._entityID - kPawnEntityID;
                if ( seatIndex >= _listServerPawn.size() || LagCompensationHistory::intersectBody( past, absoluteRay._origin, absoluteRay._direction, hit._distance ) == false )
                    continue;
                hit._pObject = manager.resolveGameObject( _listServerPawn[seatIndex]._object );
                hit._point   = ray.getPoint( hit._distance );
                hit._normal  = ray._direction * -1.0f;
            }
            else
            {
                const ShooterEnemyView* pView   = VectorUtil::findBy<&ShooterEnemyView::_netEntityID>( pGameMode->getEnemyViews(), past._entityID );
                const GameObject*       pObject = pView != nullptr ? manager.resolveGameObject( pView->_object ) : nullptr;
                const bool              bMissed = pObject == nullptr || past.mayIntersect( absoluteRay, maxDistance ) == false ||
                                     raycastRewoundBodies( manager, *pObject, past, ray, maxDistance, layerMask, hit ) == false;
                if ( bMissed )
                    continue;
            }
            const bool bFarther = hit._pObject == nullptr || hit._pObject->getObjectID() == ignoreObjectID || hit._distance > maxDistance || ( bHit && hit._distance >= outHit._distance );
            if ( bFarther )
                continue;
            outHit = hit;
            bHit   = true;
        }
        _rewoundHitCount += bHit ? 1u : 0u;
        return bHit;
    }

    void ShooterNetPawnComponent::collectRewoundBodies( const ServerPawn& shooter, float64 seenTick, vector<LagRecord>& outListRecord ) const
    {
        _lagHistory.collectAt( seenTick, makePawnEntityID( shooter ), outListRecord );
        const ShooterGameModeComponent* pGameMode = findGameMode();
        const float64                   seconds   = static_cast<float64>( shooter._floorTick ) * static_cast<float64>( _tickInterval );
        VectorUtil::removeIf( outListRecord, [pGameMode, seconds]( LagRecord& record )
        {
            if ( record._frameNumber == 0 )
                return false;
            float3  floorPosition;
            float32 floorYaw = 0.0f;
            if ( pGameMode == nullptr || pGameMode->computeFloorPose( static_cast<uint8>( record._frameNumber ), seconds, floorPosition, floorYaw ) == false )
                return true; // 그 바닥은 사라졌다 — 쏜 사람의 화면에도 없다
            record._position    = ShooterFloorStep::toAbsolute( floorPosition, floorYaw, record._position );
            record._frameNumber = 0;
            return false;
        } );
    }

    void ShooterNetPawnComponent::collectRewoundMelee( const GameObjectManager& manager, const GameObject& shooter, const vector<float3>& listPrevious, const vector<float3>& listCurrent, float32 radius,
                                                       ControlViewTick viewTick, vector<GameObjectHandle>& outListTarget ) const
    {
        const ServerPawn* const pShooter = findServerPawn( shooter.getHandle() );
        if ( pShooter == nullptr || listCurrent.empty() )
            return;
        const WorldOrigin& origin = manager.getWorldOrigin(); // 기록은 절대, 칼날은 씬 자리다
        vector<uint32>     listTouched;
        vector<LagRecord>  listPast;
        collectRewoundBodies( *pShooter, viewTick.getServerTick( static_cast<float64>( _tick ) ), listPast );
        if ( listPrevious.size() == listCurrent.size() )
        {
            for ( size_t pointIndex = 0; pointIndex < listCurrent.size(); ++pointIndex )
            {
                LagCompensationHistory::collectTouched( listPast, origin.toAbsolute( listPrevious[pointIndex] ), origin.toAbsolute( listCurrent[pointIndex] ), radius, listTouched );
            }
        }
        LagCompensationHistory::collectTouched( listPast, origin.toAbsolute( listCurrent.front() ), origin.toAbsolute( listCurrent.back() ), radius, listTouched );
        for ( const uint32 entityID : listTouched )
        {
            if ( entityID < kEnemyEntityIDBase )
                outListTarget.push_back( _listServerPawn[entityID - kPawnEntityID]._object );
        }
    }

    bool ShooterNetPawnComponent::writeOwnerState( const ShooterOwnerNetState& state, vector<uint8>& outBytes )
    {
        return ShooterNetPawnComponentInternal::writeState( state, outBytes ); // 칸 선언이 곧 바이트다 — 스키마는 핸드셰이크가 확인했다(`PlaygroundGame::onRegisterReplicatedTypes`)
    }

    bool ShooterNetPawnComponent::readOwnerState( const vector<uint8>& bytes, ShooterOwnerNetState& outState )
    {
        return ShooterNetPawnComponentInternal::readState( bytes, outState );
    }

    bool ShooterNetPawnComponent::isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const
    {
        const float3 delta = left._position - right._position;
        return delta.getLength() <= _reconcileTolerance && left._bOnGround == right._bOnGround;
    }

    SW_AUTOMATION_GETTER_WATCH( shooterFloorNumber, "Shooter.FloorNumber", "Number of the moving floor the own body stands on (0 none)", ShooterNetPawnComponent, &ShooterNetPawnComponent::getFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( shooterRemoteFloorNumber, "Shooter.RemoteFloorNumber", "Number of the moving floor another player is shown standing on (0 none)", ShooterNetPawnComponent, &ShooterNetPawnComponent::getRemoteFloorNumber );
    SW_AUTOMATION_GETTER_WATCH( shooterRemotePawnCount, "Shooter.RemotePawnCount", "Other players' bodies this client sees", ShooterNetPawnComponent, &ShooterNetPawnComponent::getRemotePawnCount );
} // namespace sw
