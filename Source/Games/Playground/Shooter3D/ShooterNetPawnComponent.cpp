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
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
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
            static ShooterMoveState simulateTick( const ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const ShooterMoveState& state,
                                                  const ControlIntent& intent, float32 tickInterval )
            {
                bool bLanded = false;
                return movement.simulate( listBox, state, movement.makeInput( intent ), tickInterval, bLanded );
            }

            /** @brief 움직임 없이 시선만 남긴 의도입니다 — 쓰러진 몸 · 멈춘 몸이 쓴다. */
            static ControlIntent makeStillIntent( const ControlIntent& intent )
            {
                ControlIntent still;
                still._controlYaw   = intent._controlYaw;
                still._controlPitch = intent._controlPitch;
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
                return writeState( state, outBytes );
            }

            /** @brief 적 하나의 엔티티 상태를 읽습니다. 깨졌거나 칸이 선언 범위 밖이면 false 입니다. */
            [[nodiscard]] static bool readEnemy( const vector<uint8>& bytes, ShooterReplicatedEnemy& outEnemy )
            {
                BitReader            reader( bytes.data(), static_cast<int32>( bytes.size() ) );
                ShooterEnemyNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok )
                    return false;
                outEnemy._position       = AbsolutePosition{ state._position };
                outEnemy._yaw            = state._yaw;
                outEnemy._phase          = static_cast<ShooterEnemyPhase>( state._phase );
                outEnemy._kindIndex      = state._kindIndex;
                outEnemy._appearanceSeed = state._appearanceSeed;
                return true;
            }

            /**
             * @brief 씬에 서버 틱 @p serverTick 을 알린다 — 틱 함수 바닥(`InterpToMovementComponent`)이 자기 서브틱에서 그 자리를 읽는다(바닥을 돌지 않는다).
             *        조종 시스템이 없는 씬이면 바닥도 움직이지 않으므로 알릴 곳이 없다.
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
        , _listEnemyEntityID{}
        , _listEntityScratch{}
        , _lagHistory{ ReplicationServerConfig{}._maxViewLagTicks + 1 } // 입력 스탬프의 되감기 한도(서버 클램프)까지 기억한다
        , _accumulator{ 0.0f }
        , _pendingTriggerYaw{ 0.0f }
        , _pendingTriggerPitch{ 0.0f }
        , _pendingTriggered{ 0 }
        , _tick{ 0 }
        , _pawnEntityID{ 0 }
        , _reconciledTick{ 0 }
        , _rewoundShotCount{ 0 }
        , _rewoundHitCount{ 0 }
        , _lastRewindTicks{ 0.0f }
        , _sentClaimCount{ 0 }
        , _confirmedClaimCount{ 0 }
        , _rejectedClaimCount{ 0 }
        , _pendingStepCount{ 0 }
        , _frameSeconds{ 0.0f }
        , _role{ Role::None }
        , _bPendingTriggerLook{ SW_FALSE }
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
        const NetLinkValue& link = *pLink;

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
            if ( _client.initialize( link._pPeers, settings ) )
                _routerSubscription = link._pRouter->registerHandler( &_client );
            if ( _routerSubscription.isActive() == false )
            {
                SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnReplication2", "Shooter net pawn: the replication client did not start (reason above) - the networked movement stays off" );
                return;
            }
            _role = Role::Client;
        }
        _accumulator      = 0.0f;
        _tick             = 0;
        _pawnEntityID     = 0;
        _reconciledTick   = 0;
        _pendingStepCount = 0;
        _bSeatsReported   = SW_FALSE;
        pMovement->setNetDriven( true );
        // 서버 틱을 읽는 바닥이 이 틱 뒤에 돌게 한다(바닥 서브틱의 선행 조건).
        if ( ControlSystem* pControl = ControlSystem::find( *getOwner()->getMutableManager() ) )
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
        ShooterBodyMovementComponent*  pMovement = findMovement();
        const vector<ShooterArenaBox>* pListBox  = findArenaBoxes();
        const bool                     bIdle     = _role == Role::None || pMovement == nullptr || pListBox == nullptr || context.getDeltaTime() <= 0.0f;
        if ( bIdle )
            return;
        // 발동은 한 프레임만 참이다 — 입력 틱(30 Hz)이 없던 프레임의 발동이 사라지지 않게 매 프레임 모은다.
        if ( _role == Role::Client )
            collectPendingTriggers( *pMovement );
        // 입력 몫도 틱과 같은 시계(게임 시간)로 채운다 — 클라이언트의 입력 틱이 이 시계로 돌고, 고정 간격으로 되풀이하는 넷 재생에서도 같은 몫이 된다.
        if ( _role == Role::Server )
            _server.accumulateInputBudget( context.getDeltaTime() );
        _accumulator += context.getDeltaTime();
        int32 tickCount = 0;
        while ( _accumulator >= _tickInterval && tickCount < ShooterNetPawnComponentInternal::kMaxTicksPerFrame )
        {
            _accumulator -= _tickInterval;
            ++tickCount;
            if ( _role == Role::Client )
                stepClient( *pMovement, *pListBox );
        }
        if ( _accumulator >= _tickInterval )
            _accumulator = 0.0f; // 따라잡지 못한 시간은 버린다 — 몰아 돌린 틱이 다음 프레임을 또 밀지 않게
        if ( _role == Role::Server )
        {
            // 서버의 걸음은 여러 몸을 옮긴다 — 남의 오브젝트라 틱 뒤(게임 스레드)에 돈다. 바닥은 그 틱의 자리를 먼저 본다.
            _pendingStepCount += tickCount;
            if ( tickCount > 0 )
                ShooterNetPawnComponentInternal::publishServerTick( getOwner(), static_cast<int64>( _tick ) + _pendingStepCount );
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn, GameObjectManager& manager )
            { netPawn.runServerFrame( manager ); } );
        }
        if ( _role == Role::Client )
        {
            _client.update( context.getDeltaTime() );
            _smoother.update( context.getDeltaTime() );
            pMovement->setDisplayOffset( _smoother.getOffset() );
            _frameSeconds = context.getDeltaTime();
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn, GameObjectManager& manager )
            { netPawn.syncRemotePawnList( manager ); } );
        }
    }

    // ------------------------------------------------------------------------------
    // 서버(틱 뒤 · 게임 스레드)
    // ------------------------------------------------------------------------------
    void ShooterNetPawnComponent::runServerFrame( GameObjectManager& manager )
    {
        const int32 stepCount                   = _pendingStepCount;
        _pendingStepCount                       = 0;
        const vector<ShooterArenaBox>* pListBox = findArenaBoxes();
        if ( _role != Role::Server || pListBox == nullptr )
            return;
        if ( _bSeatsReported == SW_FALSE )
        {
            _bSeatsReported = SW_TRUE;
            reportSeatCapacity();
        }
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepServer( manager, *pListBox );
        }
        syncServerPawnList( manager );
        for ( ServerPawn& pawn : _listServerPawn )
        {
            resolveClaims( manager, pawn );
        }
    }

    void ShooterNetPawnComponent::stepServer( GameObjectManager& manager, const vector<ShooterArenaBox>& listBox )
    {
        ++_tick;
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return;
        _server.beginTick( _tick ); // 입력을 꺼내기 전에 — 보던 틱은 서버의 지금 틱으로 자른다(`getClientViewTick`)
        assignSeats( *pLink->_pPeers );
        for ( ServerPawn& pawn : _listServerPawn )
        {
            stepServerPawn( manager, pawn, listBox );
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
        peers.collectConnected( _listConnectionScratch );
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

    void ShooterNetPawnComponent::stepServerPawn( GameObjectManager& manager, ServerPawn& pawn, const vector<ShooterArenaBox>& listBox )
    {
        using Internal                          = ShooterNetPawnComponentInternal;
        pawn._lastInputTick                     = nullopt;
        GameObject*                   pObject   = manager.resolveGameObject( pawn._object );
        ShooterBodyMovementComponent* pMovement = pObject != nullptr ? pObject->getComponent<ShooterBodyMovementComponent>() : nullptr;
        const ShooterPlayerComponent* pPlayer   = pObject != nullptr ? pObject->getComponent<ShooterPlayerComponent>() : nullptr;
        RemoteControllerComponent*    pRemote   = findRemoteController( manager, pawn );
        const MeleeSweepHistory*      pLedger   = MeleeSweepHistory::find( manager );
        const bool                    bDown     = pPlayer != nullptr && pPlayer->isAlive() == false; // 쓰러진 몸은 걷지 않는다 — 클라이언트가 보낸 이동을 쓰지 않는다
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
                pRemote->receiveIntent( *inputTick, intent ); // 서버의 폰은 이 의도로 쏘고 무기를 바꾼다(다음 조종 시스템 틱)
            }
            pawn._nextInputTick                         = *inputTick + 1u;
            pawn._viewTickByInput.acquire( *inputTick ) = _server.getClientViewTick( pawn._connectionID );
            for ( const MeleeHitClaim& claim : _listClaim )
            {
                PendingClaim& pending = pawn._listPendingClaim.emplace_back();
                pending._claim        = claim;
                pending._claim._frame = pLedger != nullptr ? pLedger->getFrame() : 0u;
                pending._viewTick     = _server.getClientViewTick( pawn._connectionID );
            }
            stepMovement( *pMovement, listBox, bDown ? Internal::makeStillIntent( intent ) : intent );
            inputTick = _server.popNextIntent( pawn._connectionID, intent, pawnEntityID, _listClaim );
        }
        // 조종자가 있는데 입력이 오지 않았으면 그 자리다(언리얼 서버의 원격 폰 — 온 입력은 다음 틱에 몰아서 걷는다). 조종자가 없는 몸은 빈 의도로 계속 돈다(떨어지던 몸이 공중에 남지 않게).
        if ( pawn._connectionID < 0 )
            stepMovement( *pMovement, listBox, ControlIntent{} );
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
            state._position  = move._position;
            state._bOnGround = move._bOnGround;
            if ( pPlayer != nullptr )
            {
                state._yaw         = MathUtil::wrapAngle( pPlayer->getLookYaw() );
                state._weaponIndex = static_cast<uint8>( pPlayer->getWeaponIndex() );
                state._bAlive      = pPlayer->isAlive() ? SW_TRUE : SW_FALSE;
            }
            if ( ShooterNetPawnComponentInternal::writeState( state, _bytesScratch ) )
                _server.setEntity( entityID, kPawnTypeID, _bytesScratch );
            else
                SW_LOG_ERROR_ID( "ShooterNetPawn.StateOutOfRange", "Pawn state is outside the replicated range (%#, %#, %#) - not replicated this tick", move._position._x, move._position._y, move._position._z );
            _interest.setEntityPosition( entityID, move._position );
            if ( pawn._connectionID >= 0 )
                _interest.setObserver( pawn._connectionID, move._position );
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
            state._move         = pMovement->getState();
            state._pawnEntityID = makePawnEntityID( pawn );
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

    void ShooterNetPawnComponent::stepMovement( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const ControlIntent& intent ) const
    {
        const ShooterMoveState before = movement.getState();
        const ShooterMoveState next   = ShooterNetPawnComponentInternal::simulateTick( movement, listBox, before, intent, _tickInterval );
        movement.setState( next, float3{ next._position._x - before._position._x, 0.0f, next._position._z - before._position._z } * ( 1.0f / _tickInterval ) );
    }

    // ------------------------------------------------------------------------------
    // 클라이언트
    // ------------------------------------------------------------------------------
    void ShooterNetPawnComponent::stepClient( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox )
    {
        ++_tick;
        // 예측하는 이 입력 틱은 서버에서 (입력 틱 + 서버 차이) 틱에 시뮬레이션된다 — 바닥을 그 자리에 둬야 바닥 위 예측이 서버와 맞는다.
        const optional<int64> serverTickOffset = _client.findServerTickOffset();
        if ( serverTickOffset.has_value() )
            ShooterNetPawnComponentInternal::publishServerTick( getOwner(), static_cast<int64>( _tick ) + *serverTickOffset );
        const ControlIntent intent = takeNetIntent( movement );
        // 입력 하나의 상한을 넘는 주장은 다음 입력 틱에 싣는다. `_frame` 은 입력 틱이다 — 서버가 그 입력을 꺼낸 프레임으로 옮긴다.
        const size_t claimCount = MathUtil::min( _listClaim.size(), static_cast<size_t>( NetClientServerMessage::kMaxClaimsPerIntent ) );
        for ( size_t claimIndex = 0; claimIndex < claimCount; ++claimIndex )
        {
            _listClaim[claimIndex]._frame = _tick;
        }
        if ( _client.sendIntent( _tick, intent, vector_reference<const MeleeHitClaim>( _listClaim.data(), claimCount ) ) == false )
            SW_LOG_WARNING_ID( "ShooterNetPawn.ShooterNetPawnInput", "Shooter net pawn: intent of tick %# was not sent", _tick );
        _sentClaimCount += static_cast<uint32>( claimCount );
        _listClaim.erase( _listClaim.begin(), _listClaim.begin() + static_cast<std::ptrdiff_t>( claimCount ) );
        const ShooterMoveState before  = movement.getState();
        ShooterMoveState       current = ShooterNetPawnComponentInternal::simulateTick( movement, listBox, before, intent, _tickInterval );
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
            const ShooterMoveState& serverState = serverOwner._move;
            ShooterPlayerComponent* pPlayer     = getOwner()->getComponent<ShooterPlayerComponent>();
            if ( pPlayer != nullptr )
                pPlayer->applyServerReport( serverOwner._confirmedHitCount, serverOwner._killCount, serverOwner._health, serverOwner._bOut == SW_TRUE );
            const float3                    shown  = current._position;
            const PredictionReconcileResult result = _prediction.reconcile(
                pOwner->_inputTick, serverState, current,
                [&movement, &listBox, this]( const ShooterMoveState& state, const ControlIntent& tickIntent )
            { return ShooterNetPawnComponentInternal::simulateTick( movement, listBox, state, tickIntent, _tickInterval ); },
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
        const ControlIntent& intent = pPawn->getIntent();
        _pendingTriggered |= intent._buttonTriggered;
        if ( intent._bTriggerLook )
        {
            _bPendingTriggerLook = SW_TRUE;
            _pendingTriggerYaw   = intent._triggerYaw;
            _pendingTriggerPitch = intent._triggerPitch;
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
        intent._buttonTriggered = _pendingTriggered;
        intent._bTriggerLook    = _bPendingTriggerLook == SW_TRUE;
        intent._triggerYaw      = _pendingTriggerYaw;
        intent._triggerPitch    = _pendingTriggerPitch;
        _pendingTriggered       = 0;
        _bPendingTriggerLook    = SW_FALSE;
        _pendingTriggerYaw      = 0.0f;
        _pendingTriggerPitch    = 0.0f;
        return intent;
    }

    const ShooterGameModeComponent* ShooterNetPawnComponent::findGameMode() const
    {
        const GameObject*             pOwner   = getOwner();
        const GameObjectManager*      pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ShooterPlayerComponent* pPlayer  = findPlayer();
        return pManager != nullptr && pPlayer != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
    }

    const vector<ShooterArenaBox>* ShooterNetPawnComponent::findArenaBoxes() const
    {
        const ShooterGameModeComponent* pGameMode = findGameMode();
        return pGameMode != nullptr ? &pGameMode->getBoxes() : nullptr;
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
        }
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( pGameMode != nullptr )
        {
            for ( const ShooterEnemyView& view : pGameMode->getEnemyViews() )
            {
                LagRecord& record = _listLagScratch.emplace_back();
                record._position  = view._position._value;
                record._radius    = view._radius;
                record._height    = view._height;
                record._entityID  = view._netEntityID;
                if ( ShooterNetPawnComponentInternal::writeEnemy( view, _bytesScratch ) )
                    _server.setEntity( view._netEntityID, kEnemyTypeID, _bytesScratch );
                else
                    SW_LOG_ERROR_ID( "ShooterNetPawn.EnemyOutOfRange", "Enemy %# is outside the replicated range (%#, %#, %#) - not replicated this tick", view._netEntityID,
                                     view._position._value._x, view._position._value._y, view._position._value._z );
                _interest.setEntityPosition( view._netEntityID, view._position._value );
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

    void ShooterNetPawnComponent::queueMeleeClaim( uint32 enemyEntityID )
    {
        MeleeHitClaim& claim = _listClaim.emplace_back();
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
        for ( const PendingClaim& pending : pawn._listPendingClaim )
        {
            // 서버의 폰은 꺼낸 다음 프레임부터 그 의도로 휘두른다 — 맞춰 볼 프레임 안팎의 스윕이 모두 기록될 때까지 기다린다.
            if ( pLedger->getFrame() <= pending._claim._frame + MeleeSweepHistory::kClaimSlackFrames + 1u )
            {
                pawn._listPendingClaim[keptCount++] = pending;
                continue;
            }
            const uint32            entityID = static_cast<uint32>( pending._claim._targetID );
            const ShooterEnemyView* pView    = nullptr;
            for ( const ShooterEnemyView& view : pGameMode->getEnemyViews() )
            {
                if ( view._netEntityID == entityID )
                    pView = &view;
            }
            LagRecord   past;
            GameObject* pTarget = pView != nullptr ? manager.resolveGameObject( pView->_object ) : nullptr;
            if ( pTarget == nullptr || _lagHistory.sampleAt( pending._viewTick, entityID, past ) == false )
            {
                ++_rejectedClaimCount; // 표적이 그 사이 쓰러졌거나 지어낸 번호다
                continue;
            }
            // 장부는 표적을 구로 본다 — 몸 가운데에 몸 전체를 덮는 구다(근접은 뼈마다 가르지 않는다).
            const float3              center         = manager.getWorldOrigin().toLocal( past._position + float3{ 0.0f, past._height * 0.5f, 0.0f } ); // 장부의 칼날은 씬 자리다
            const float32             radius         = MathUtil::max( past._radius, past._height * 0.5f );
            vector<GameObjectHandle>& listHit        = pPlayer->getMeleeHitTargets();
            const size_t              hitCountBefore = listHit.size();
            if ( pLedger->claimHit( *pObject, *pTarget, pending._claim, center, radius, listHit ) != MeleeClaimResult::Confirmed )
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
            const NetEntityState* pFrom = nullptr;
            const NetEntityState* pTo   = nullptr;
            float32               alpha = 0.0f;
            if ( _client.sampleEntity( entityID, pFrom, pTo, alpha ) == false || pFrom->_typeID != kEnemyTypeID || pTo->_typeID != kEnemyTypeID )
                continue;
            ShooterReplicatedEnemy from;
            ShooterReplicatedEnemy to;
            if ( ShooterNetPawnComponentInternal::readEnemy( pFrom->_buffer, from ) == false || ShooterNetPawnComponentInternal::readEnemy( pTo->_buffer, to ) == false )
                continue;
            to._netEntityID = entityID;
            to._position    = from._position + ( to._position._value - from._position._value ) * alpha;
            to._yaw         = from._yaw + MathUtil::wrapAngle( to._yaw - from._yaw ) * alpha;
            outListEnemy.push_back( to );
        }
    }

    void ShooterNetPawnComponent::syncRemotePawnList( GameObjectManager& manager )
    {
        using Internal                      = ShooterNetPawnComponentInternal;
        const ShooterPlayerComponent* pSelf = findPlayer();
        if ( _role != Role::Client || pSelf == nullptr || _frameSeconds <= 0.0f )
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
            const NetEntityState* pFrom = nullptr;
            const NetEntityState* pTo   = nullptr;
            float32               alpha = 0.0f;
            ShooterPawnNetState   from;
            ShooterPawnNetState   to;
            const bool            bOther = entityID != _pawnEntityID && _client.sampleEntity( entityID, pFrom, pTo, alpha ) && pFrom->_typeID == kPawnTypeID && pTo->_typeID == kPawnTypeID;
            if ( bOther == false || Internal::readState( pFrom->_buffer, from ) == false || Internal::readState( pTo->_buffer, to ) == false )
                continue;
            RemotePawn* pRemote = VectorUtil::findBy<&RemotePawn::_entityID>( _listRemotePawn, entityID );
            if ( pRemote == nullptr )
            {
                pRemote            = &_listRemotePawn.emplace_back();
                pRemote->_entityID = entityID;
                pRemote->_object   = pSelf->spawnBodyObject( manager, "RemotePlayerBody" );
                pRemote->_position = AbsolutePosition{ from._position };
                pRemote->_bArmed   = SW_FALSE;
            }
            ShooterAvatarPose pose;
            pose._feet       = AbsolutePosition{ from._position + ( to._position - from._position ) * alpha };
            pose._yaw        = from._yaw + MathUtil::wrapAngle( to._yaw - from._yaw ) * alpha;
            pose._bAlive     = to._bAlive;
            pose._bOnGround  = to._bOnGround;
            pose._bTwoHanded = pSelf->getWeapon( to._weaponIndex ).getDef()._bTwoHanded;

            const float3 moved = pose._feet._value - pRemote->_position._value;
            pose._velocity     = float3{ moved._x, 0.0f, moved._z } * ( 1.0f / _frameSeconds );
            pRemote->_position = pose._feet;

            GameObject*             pObject = manager.resolveGameObject( pRemote->_object );
            ShooterAvatarComponent* pAvatar = pObject != nullptr ? pObject->getComponent<ShooterAvatarComponent>() : nullptr;
            if ( pAvatar != nullptr )
                pAvatar->setReplicatedPose( pose );
            CharacterAppearanceComponent* pAppearance = pObject != nullptr ? pObject->getComponent<CharacterAppearanceComponent>() : nullptr;
            if ( pAppearance != nullptr && ( pRemote->_bArmed == SW_FALSE || pRemote->_weaponIndex != to._weaponIndex ) )
                pAppearance->setSlotItem( hashed_string( "MainHand" ), pSelf->getWeaponItem( to._weaponIndex ) );
            pRemote->_weaponIndex = to._weaponIndex;
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

    float32 ShooterNetPawnComponent::findShotViewTick( const GameObjectManager& manager, const ServerPawn& pawn ) const
    {
        const RemoteControllerComponent* pRemote   = findRemoteController( manager, pawn );
        const uint32                     nextTick  = pRemote != nullptr ? pRemote->getNextInputTick() : 0u;
        const float32*                   pViewTick = nextTick > 0u ? pawn._viewTickByInput.find( nextTick - 1u ) : nullptr;
        return pViewTick != nullptr ? *pViewTick : _server.getClientViewTick( pawn._connectionID );
    }

    bool ShooterNetPawnComponent::raycastRewound( const GameObjectManager& manager, const GameObject& shooter, const Ray& ray, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        const ServerPawn* const pShooter = findServerPawn( shooter.getHandle() );
        if ( pShooter == nullptr )
            return false; // 자리가 없는 몸은 쏘지 않는다
        ++_rewoundShotCount;
        const float32 viewTick   = findShotViewTick( manager, *pShooter );
        const float32 newestTick = static_cast<float32>( _lagHistory.getNewestTick() );
        _lastRewindTicks         = newestTick - viewTick;
        float32      distance    = 0.0f;
        const uint32 entityID    = _lagHistory.raycastAt( viewTick, manager.getWorldOrigin().toAbsolute( ray._origin ), ray._direction, maxDistance, makePawnEntityID( *pShooter ), distance );
        LagRecord    past;
        LagRecord    now;
        const bool   bTracked = entityID != 0 && _lagHistory.sampleAt( viewTick, entityID, past ) && _lagHistory.sampleAt( newestTick, entityID, now );
        if ( bTracked == false )
            return false;
        if ( entityID < kEnemyEntityIDBase )
        {
            // 다른 플레이어다 — 되감은 캡슐이 곧 맞음 상자다(몸 프리팹에 히트박스가 없다).
            const size_t seatIndex = entityID - kPawnEntityID;
            outHit._pObject        = seatIndex < _listServerPawn.size() ? manager.resolveGameObject( _listServerPawn[seatIndex]._object ) : nullptr;
            outHit._point          = ray.getPoint( distance );
            outHit._normal         = ray._direction * -1.0f;
            outHit._distance       = distance;
            _rewoundHitCount += outHit._pObject != nullptr ? 1u : 0u;
            return outHit._pObject != nullptr;
        }
        // 포즈는 되감지 않는다 — 몸의 이동만 되감아 히트 존(뼈마다의 바디)은 지금 포즈로 본다.
        const float3 origin = ray._origin + ( now._position - past._position );
        if ( CharacterRayHit::raycast3D( manager, origin, ray._direction, maxDistance, layerMask, ignoreObjectID, outHit ) == false || outHit._pObject == nullptr )
            return false;
        const ShooterEnemyComponent* pEnemy = outHit._pObject->getComponent<ShooterEnemyComponent>();
        if ( pEnemy == nullptr || pEnemy->getNetEntityID() != entityID )
            return false;
        ++_rewoundHitCount;
        return true;
    }

    void ShooterNetPawnComponent::collectRewoundMelee( const GameObjectManager& manager, const GameObject& shooter, const vector<float3>& listPrevious, const vector<float3>& listCurrent, float32 radius,
                                                       vector<GameObjectHandle>& outListTarget ) const
    {
        const ServerPawn* const pShooter = findServerPawn( shooter.getHandle() );
        if ( pShooter == nullptr || listCurrent.empty() )
            return;
        const float32      viewTick  = findShotViewTick( manager, *pShooter );
        const uint32       shooterID = makePawnEntityID( *pShooter );
        const WorldOrigin& origin    = manager.getWorldOrigin(); // 기록은 절대, 칼날은 씬 자리다
        vector<uint32>     listTouched;
        if ( listPrevious.size() == listCurrent.size() )
        {
            for ( size_t pointIndex = 0; pointIndex < listCurrent.size(); ++pointIndex )
            {
                _lagHistory.collectTouchedAt( viewTick, origin.toAbsolute( listPrevious[pointIndex] ), origin.toAbsolute( listCurrent[pointIndex] ), radius, shooterID, listTouched );
            }
        }
        _lagHistory.collectTouchedAt( viewTick, origin.toAbsolute( listCurrent.front() ), origin.toAbsolute( listCurrent.back() ), radius, shooterID, listTouched );
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

    SW_AUTOMATION_GETTER_WATCH( shooterRemotePawnCount, "Shooter.RemotePawnCount", "Other players' bodies this client sees", ShooterNetPawnComponent, &ShooterNetPawnComponent::getRemotePawnCount );

    void ShooterNetPawnComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<ShooterNetPawnComponent>( this );
    }

    void ShooterNetPawnComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<ShooterNetPawnComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
