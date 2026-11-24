#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterNetPawnComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/WallClock.h"

#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Kits/Feature/Online/Leaderboard/Runtime/LeaderboardScoreSender.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Runtime/MatchBackfill.h"
#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

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
                BitWriter writer{ std::move( outBytes ) };
                writer.clear();
                const bool bWritten = ReplicatedLayout::writeFull( state, writer );
                outBytes            = writer.releaseBytes();
                return bWritten;
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
        , _server{}
        , _client{}
        , _interest{}
        , _routerSubscription{}
        , _prediction{}
        , _smoother{}
        , _bytesScratch{}
        , _listClaim{}
        , _listPendingClaim{}
        , _listLagScratch{}
        , _listEnemyEntityID{}
        , _listEntityScratch{}
        , _lagHistory{ ReplicationServerConfig{}._maxViewLagTicks + 1 } // 입력 스탬프의 되감기 한도(서버 클램프)까지 기억한다
        , _viewTickByInput{}
        , _remoteController{}
        , _accumulator{ 0.0f }
        , _pendingTriggerYaw{ 0.0f }
        , _pendingTriggerPitch{ 0.0f }
        , _pendingTriggered{ 0 }
        , _tick{ 0 }
        , _nextInputTick{ kInputTickUnknown }
        , _reconciledTick{ 0 }
        , _rewoundShotCount{ 0 }
        , _rewoundHitCount{ 0 }
        , _lastRewindTicks{ 0.0f }
        , _sentClaimCount{ 0 }
        , _confirmedClaimCount{ 0 }
        , _rejectedClaimCount{ 0 }
        , _controllerAccountID{ 0 }
        , _leftAccountID{ 0 }
        , _controllerID{ -1 }
        , _role{ Role::None }
        , _bPendingTriggerLook{ SW_FALSE }
        , _bRemoteRequested{ SW_FALSE }
        , _bClaimsScheduled{ SW_FALSE }
        , _bLeaveScheduled{ SW_FALSE }
        , _bSeatsReported{ SW_FALSE }
        , _bScoreReported{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
        _viewTickByInput.initialize( RemoteControllerComponent::kReceiveCapacity );
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
        _accumulator         = 0.0f;
        _tick                = 0;
        _nextInputTick       = kInputTickUnknown;
        _reconciledTick      = 0;
        _controllerAccountID = 0;
        _leftAccountID       = 0;
        _controllerID        = -1;
        _bSeatsReported      = SW_FALSE;
        _bScoreReported      = SW_FALSE;
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
        _listClaim.clear();
        _listPendingClaim.clear();
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
        // 원격 조종자는 맡을 연결이 정해진 뒤에 세운다 — 연결 id 없이는 세울 수 없다(`RemoteControllerComponent::spawnForConnection`).
        if ( _role == Role::Server && _bRemoteRequested == SW_FALSE && _controllerID >= 0 )
        {
            _bRemoteRequested = SW_TRUE;
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn )
            { netPawn.possessRemote(); } );
        }
        if ( _role == Role::Server && _bSeatsReported == SW_FALSE )
        {
            _bSeatsReported = SW_TRUE;
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn )
            { netPawn.reportSeatCapacity(); } );
        }
        // 입력 몫도 틱과 같은 시계(게임 시간)로 채운다 — 클라이언트의 입력 틱이 이 시계로 돌고, 고정 간격으로 되풀이하는 넷 재생에서도 같은 몫이 된다.
        if ( _role == Role::Server )
            _server.accumulateInputBudget( context.getDeltaTime() );
        _accumulator += context.getDeltaTime();
        int32 tickCount = 0;
        while ( _accumulator >= _tickInterval && tickCount < ShooterNetPawnComponentInternal::kMaxTicksPerFrame )
        {
            _accumulator -= _tickInterval;
            ++tickCount;
            if ( _role == Role::Server )
                stepServer( *pMovement, *pListBox );
            else
                stepClient( *pMovement, *pListBox );
        }
        if ( _accumulator >= _tickInterval )
            _accumulator = 0.0f; // 따라잡지 못한 시간은 버린다 — 몰아 돌린 틱이 다음 프레임을 또 밀지 않게
        if ( _listPendingClaim.empty() == false && _bClaimsScheduled == SW_FALSE )
        {
            _bClaimsScheduled = SW_TRUE;
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn )
            { netPawn.resolveClaims(); } );
        }
        if ( _leftAccountID != 0 && _bLeaveScheduled == SW_FALSE )
        {
            _bLeaveScheduled = SW_TRUE;
            context.deferPostTickOnComponent( *this, []( ShooterNetPawnComponent& netPawn )
            { netPawn.finishLeftPlayer(); } );
        }
        if ( _role == Role::Client )
        {
            _client.update( context.getDeltaTime() );
            _smoother.update( context.getDeltaTime() );
            pMovement->setDisplayOffset( _smoother.getOffset() );
        }
    }

    void ShooterNetPawnComponent::stepServer( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox )
    {
        ++_tick;
        ShooterNetPawnComponentInternal::publishServerTick( getOwner(), static_cast<int64>( _tick ) );
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return;
        INetPeers&                 peers   = *pLink->_pPeers;
        RemoteControllerComponent* pRemote = findRemoteController();
        _server.beginTick( _tick ); // 입력을 꺼내기 전에 — 보던 틱은 서버의 지금 틱으로 자른다(`getClientViewTick`)
        // 조종 연결 — 처음 붙은 클라이언트가 이 몸을 맡고, 끊기면 나감 처리(점수 · 판 다시 열기)가 끝난 뒤 다음 클라이언트가 처음부터 맡는다.
        const bool bControllerGone = _leftAccountID == 0 && ( _controllerID < 0 || peers.getConnectionState( _controllerID ) != NetConnectionState::Connected );
        if ( bControllerGone )
        {
            // 연결 목록은 조종자를 새로 고를 때만 쓴다 — 틱마다 모으지 않는다(목록 할당 · 순회가 조종자가 붙어 있는 틱에서 빠진다).
            vector<int32> listConnection;
            peers.collectConnected( listConnection );
            if ( _controllerAccountID != 0 && _leftAccountID == 0 )
                _leftAccountID = _controllerAccountID; // 나감 처리(점수 · 빈 자리)는 틱 뒤다 — 끊긴 연결의 주체는 이 틱에 이미 0 이라 맡을 때 적어 둔 계정을 쓴다
            // 주체는 접속 티켓이 정한 계정이다 — 클라이언트가 보낸 값이 아니다. 매칭이 배정한 서버에서는 주체 없는 연결(티켓 없이 들어온 것)에 몸을 맡기지 않는다 —
            // 세션 역할만 보면 티켓을 요구하지 않는 설정의 매치 서버가 아무 연결에나 배정받은 사람의 몸을 넘긴다. 매칭 없는 서버(개발 접속 · 한 프로세스 재생)는 그대로 첫 연결이다.
            const bool bAccountRequired = ShooterNetPawnComponentInternal::findBackfillSender() != nullptr;
            _controllerID               = -1;
            _controllerAccountID        = 0;
            if ( _leftAccountID != 0 )
                listConnection.clear(); // 앞사람의 나감 처리가 먼저다 — 다음 틱에 고른다
            for ( const int32 connectionID : listConnection )
            {
                const uint64 accountID = peers.getConnectionPrincipal( connectionID );
                if ( bAccountRequired && accountID == 0 )
                    continue;
                _controllerID        = connectionID;
                _controllerAccountID = accountID;
                _bScoreReported      = SW_FALSE;
                break;
            }
            _nextInputTick = kInputTickUnknown;
            if ( pRemote != nullptr )
            {
                pRemote->clearReceived();
                pRemote->setNextInputTick( _nextInputTick );
                if ( _controllerID >= 0 )
                    pRemote->transferToConnection( _controllerID ); // 다음 클라이언트가 맡는다 — 폰의 소유 연결도 따라간다
            }
        }
        const MeleeSweepHistory* pLedger = MeleeSweepHistory::find( *getOwner()->getManager() );
        ShooterMoveState         next    = movement.getState();
        ControlIntent            intent;
        uint32                   pawnEntityID = 0; // 이 서버의 폰은 하나다
        optional<uint32>         lastTick{};
        optional<uint32>         inputTick = _controllerID >= 0 ? _server.popNextIntent( _controllerID, intent, pawnEntityID, _listClaim ) : nullopt;
        while ( inputTick.has_value() )
        {
            lastTick = inputTick;
            if ( pRemote != nullptr )
            {
                // 클라이언트는 접속 전에도 틱을 센다 — 조종을 맡은 뒤 처음 받은 틱에서 시작한다.
                if ( _nextInputTick == kInputTickUnknown )
                    pRemote->setNextInputTick( *inputTick );
                pRemote->receiveIntent( *inputTick, intent ); // 서버의 폰은 이 의도로 쏘고 무기를 바꾼다(다음 조종 시스템 틱)
            }
            _nextInputTick                         = *inputTick + 1u;
            _viewTickByInput.acquire( *inputTick ) = _server.getClientViewTick( _controllerID );
            for ( const MeleeHitClaim& claim : _listClaim )
            {
                PendingClaim& pending = _listPendingClaim.emplace_back();
                pending._claim        = claim;
                pending._claim._frame = pLedger != nullptr ? pLedger->getFrame() : 0u;
                pending._viewTick     = _server.getClientViewTick( _controllerID );
            }
            next      = stepMovement( movement, listBox, intent );
            inputTick = _server.popNextIntent( _controllerID, intent, pawnEntityID, _listClaim );
        }
        // 조종자가 있는데 입력이 오지 않았으면 그 자리다(언리얼 서버의 원격 폰 — 온 입력은 다음 틱에 몰아서 걷는다). 조종자가 없는 몸은 빈 의도로 계속 돈다(떨어지던 몸이 공중에 남지 않게).
        if ( _controllerID < 0 )
            next = stepMovement( movement, listBox, ControlIntent{} );

        recordEnemies();
        if ( writeState( next, _bytesScratch ) )
            _server.setEntity( kPawnEntityID, kPawnTypeID, _bytesScratch );
        else
            SW_LOG_ERROR_ID( "ShooterNetPawn.StateOutOfRange", "Pawn state is outside the replicated range (%#, %#, %#) - not replicated this tick", next._position._x,
                             next._position._y, next._position._z );
        _server.endTick();
        _interest.setEntityPosition( kPawnEntityID, next._position );
        if ( _controllerID >= 0 )
        {
            _interest.setObserver( _controllerID, next._position );
            if ( lastTick.has_value() )
            {
                const ShooterPlayerComponent*   pPlayer   = getOwner()->getComponent<ShooterPlayerComponent>();
                const ShooterGameModeComponent* pGameMode = findGameMode();
                if ( writeOwnerState( next, pPlayer != nullptr ? pPlayer->getHitCount() : 0u, pGameMode != nullptr ? pGameMode->getKillCount() : 0u, pPlayer != nullptr ? pPlayer->getHealth() : 0.0f,
                                      _bytesScratch ) )
                    (void)_server.setOwnerState( _controllerID, *lastTick, _bytesScratch ); // 상태는 상한(작은 고정 크기) 안이다
            }
        }
        _interest.updateRelevancy();
        _server.sendSnapshots();
    }

    ShooterMoveState ShooterNetPawnComponent::stepMovement( ShooterBodyMovementComponent& movement, const vector<ShooterArenaBox>& listBox, const ControlIntent& intent ) const
    {
        const ShooterMoveState before = movement.getState();
        const ShooterMoveState next   = ShooterNetPawnComponentInternal::simulateTick( movement, listBox, before, intent, _tickInterval );
        movement.setState( next, float3{ next._position._x - before._position._x, 0.0f, next._position._z - before._position._z } * ( 1.0f / _tickInterval ) );
        return next;
    }

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
        ShooterMoveState     serverState;
        uint32               confirmedHitCount = 0;
        uint32               serverKillCount   = 0;
        float32              serverHealth      = 0.0f;
        const bool           bNewOwnerState    = pOwner != nullptr && pOwner->_bPresent == SW_TRUE && pOwner->_inputTick > _reconciledTick && readOwnerState( pOwner->_buffer, serverState, confirmedHitCount, serverKillCount, serverHealth );
        if ( bNewOwnerState )
        {
            _reconciledTick                 = pOwner->_inputTick;
            ShooterPlayerComponent* pPlayer = getOwner()->getComponent<ShooterPlayerComponent>();
            if ( pPlayer != nullptr )
                pPlayer->applyServerReport( confirmedHitCount, serverKillCount, serverHealth );
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

    RemoteControllerComponent* ShooterNetPawnComponent::findRemoteController() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const GameObject*        pObject  = pManager != nullptr ? pManager->resolveGameObject( _remoteController ) : nullptr;
        return pObject != nullptr ? pObject->getComponent<RemoteControllerComponent>() : nullptr;
    }

    void ShooterNetPawnComponent::possessRemote()
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        PawnComponent*     pPawn    = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pManager == nullptr || pPawn == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnNoPawn", "Shooter net pawn: the server body has no pawn - the client's shots and weapons do not reach it" );
            return;
        }
        // 언리얼 서버가 접속마다 PlayerController 를 세우는 자리다 — 씬 데이터가 아니라 네트 역할이 정한다(혼자 하는 게임에는 없다). 씬과 함께 지워진다.
        // 맡은 연결(`_controllerID`)로 세워 폰의 소유 연결(`PawnComponent::findInputConnection`)이 잡힌다.
        RemoteControllerComponent* pRemote = RemoteControllerComponent::spawnForConnection( *pManager, hashed_string( "ShooterRemoteController" ), _controllerID, *pPawn );
        if ( pRemote == nullptr )
        {
            SW_LOG_ERROR_ID( "ShooterNetPawn.ShooterNetPawnNoRemote", "Shooter net pawn: the remote controller could not be created - the client's shots and weapons do not reach the server body" );
            return;
        }
        pRemote->setNextInputTick( _nextInputTick );
        _remoteController = pRemote->getOwner()->getHandle();
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
            if ( movement.isSuspended() )
            {
                intent._controlYaw   = pPawn->getIntent()._controlYaw;
                intent._controlPitch = pPawn->getIntent()._controlPitch;
            }
            else
            {
                intent = pPawn->getIntent();
            }
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
        const ShooterPlayerComponent* pPlayer  = pOwner != nullptr ? pOwner->getComponent<ShooterPlayerComponent>() : nullptr;
        return pManager != nullptr && pPlayer != nullptr ? GameModeComponent::resolve<ShooterGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
    }

    const vector<ShooterArenaBox>* ShooterNetPawnComponent::findArenaBoxes() const
    {
        const ShooterGameModeComponent* pGameMode = findGameMode();
        return pGameMode != nullptr ? &pGameMode->getBoxes() : nullptr;
    }

    void ShooterNetPawnComponent::recordEnemies()
    {
        _listLagScratch.clear();
        _listEntityScratch.clear();
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

    void ShooterNetPawnComponent::reportMatchScore( uint64 accountID )
    {
        const MinigameFlow* const       pFlow   = game::findSubsystem<MinigameFlow>();
        const MinigameDefinition* const pActive = pFlow != nullptr ? pFlow->findActiveDefinition() : nullptr;
        const optional<int64>           score   = pFlow != nullptr ? pFlow->findMatchScore() : nullopt;
        if ( score.has_value() == false || accountID == 0 || _bScoreReported == SW_TRUE )
            return; // 매치 서버가 아니거나(개발 접속 포함) 이 사람의 판을 이미 냈다
        _bScoreReported                           = SW_TRUE;
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

    void ShooterNetPawnComponent::reportMatchScore()
    {
        if ( _role == Role::Server )
            reportMatchScore( _controllerAccountID );
    }

    void ShooterNetPawnComponent::reportSeatCapacity()
    {
        IMatchBackfillSender* const pSender = ShooterNetPawnComponentInternal::findBackfillSender();
        if ( pSender != nullptr )
            pSender->setSeatCapacity( 1 ); // 이 맵의 서버 폰은 하나다 — 관전 · 공동 조종이 없다
    }

    void ShooterNetPawnComponent::finishLeftPlayer()
    {
        _bLeaveScheduled                 = SW_FALSE;
        const uint64 accountID           = _leftAccountID;
        _leftAccountID                   = 0;
        const MinigameFlow* const pFlow  = game::findSubsystem<MinigameFlow>();
        const bool                bMatch = pFlow != nullptr && pFlow->findMatchScore().has_value();
        reportMatchScore( accountID );
        ShooterNetPawnComponentInternal::reportPlayerLeft( accountID );
        // 매치 서버의 판은 다음에 배정받은 사람이 처음부터 잇는다 — 앞사람의 처치 수 · 적을 물려받지 않는다.
        ShooterPlayerComponent* const pPlayer = getOwner()->getComponent<ShooterPlayerComponent>();
        if ( bMatch && pPlayer != nullptr )
            pPlayer->restartRound();
    }

    void ShooterNetPawnComponent::resolveClaims()
    {
        _bClaimsScheduled                         = SW_FALSE;
        GameObject*                     pOwner    = getOwner();
        GameObjectManager*              pManager  = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        ShooterPlayerComponent*         pPlayer   = pOwner != nullptr ? pOwner->getComponent<ShooterPlayerComponent>() : nullptr;
        const MeleeSweepHistory*        pLedger   = pManager != nullptr ? MeleeSweepHistory::find( *pManager ) : nullptr;
        const ShooterGameModeComponent* pGameMode = findGameMode();
        if ( pLedger == nullptr || pPlayer == nullptr || pGameMode == nullptr )
        {
            _listPendingClaim.clear();
            return;
        }
        size_t keptCount = 0;
        for ( const PendingClaim& pending : _listPendingClaim )
        {
            // 서버의 폰은 꺼낸 다음 프레임부터 그 의도로 휘두른다 — 맞춰 볼 프레임 안팎의 스윕이 모두 기록될 때까지 기다린다.
            if ( pLedger->getFrame() <= pending._claim._frame + MeleeSweepHistory::kClaimSlackFrames + 1u )
            {
                _listPendingClaim[keptCount++] = pending;
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
            GameObject* pTarget = pView != nullptr ? pManager->resolveGameObject( pView->_object ) : nullptr;
            if ( pTarget == nullptr || _lagHistory.sampleAt( pending._viewTick, entityID, past ) == false )
            {
                ++_rejectedClaimCount; // 표적이 그 사이 쓰러졌거나 지어낸 번호다
                continue;
            }
            // 장부는 표적을 구로 본다 — 몸 가운데에 몸 전체를 덮는 구다(근접은 뼈마다 가르지 않는다).
            const float3              center         = pManager->getWorldOrigin().toLocal( past._position + float3{ 0.0f, past._height * 0.5f, 0.0f } ); // 장부의 칼날은 씬 자리다
            const float32             radius         = MathUtil::max( past._radius, past._height * 0.5f );
            vector<GameObjectHandle>& listHit        = pPlayer->getMeleeHitTargets();
            const size_t              hitCountBefore = listHit.size();
            if ( pLedger->claimHit( *pOwner, *pTarget, pending._claim, center, radius, listHit ) != MeleeClaimResult::Confirmed )
            {
                ++_rejectedClaimCount;
                continue;
            }
            ++_confirmedClaimCount;
            if ( listHit.size() > hitCountBefore )
                pPlayer->recordMeleeHit(); // 서버 스윕이 먼저 맞히지 못한 것만 — 창 하나에 한 번이다
        }
        _listPendingClaim.resize( keptCount );
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

    float32 ShooterNetPawnComponent::findShotViewTick() const
    {
        const RemoteControllerComponent* pRemote   = findRemoteController();
        const uint32                     nextTick  = pRemote != nullptr ? pRemote->getNextInputTick() : 0u;
        const float32*                   pViewTick = nextTick > 0u ? _viewTickByInput.find( nextTick - 1u ) : nullptr;
        return pViewTick != nullptr ? *pViewTick : _server.getClientViewTick( _controllerID );
    }

    bool ShooterNetPawnComponent::raycastRewound( const GameObjectManager& manager, const Ray& ray, float32 maxDistance, uint32 layerMask, uint64 ignoreObjectID, CharacterRayHit& outHit )
    {
        ++_rewoundShotCount;
        const float32 viewTick   = findShotViewTick();
        const float32 newestTick = static_cast<float32>( _lagHistory.getNewestTick() );
        _lastRewindTicks         = newestTick - viewTick;
        float32      distance    = 0.0f;
        const uint32 entityID    = _lagHistory.raycastAt( viewTick, manager.getWorldOrigin().toAbsolute( ray._origin ), ray._direction, maxDistance, 0, distance );
        LagRecord    past;
        LagRecord    now;
        const bool   bTracked = entityID != 0 && _lagHistory.sampleAt( viewTick, entityID, past ) && _lagHistory.sampleAt( newestTick, entityID, now );
        if ( bTracked == false )
            return false;
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

    bool ShooterNetPawnComponent::writeState( const ShooterMoveState& state, vector<uint8>& outBytes )
    {
        // 칸 선언(`Replicated` · 범위)이 곧 바이트다 — 같은 칸을 손 코덱이 따로 알지 않는다. 스키마는 핸드셰이크가 확인했다(`PlaygroundGame::onRegisterReplicatedTypes`).
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        const bool bWritten = ReplicatedLayout::writeFull( state, writer );
        outBytes            = writer.releaseBytes();
        return bWritten;
    }

    bool ShooterNetPawnComponent::readState( const vector<uint8>& bytes, ShooterMoveState& outState )
    {
        BitReader reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        return ReplicatedLayout::readFull( reader, outState, nullptr ) == FixedReadResult::Ok;
    }

    bool ShooterNetPawnComponent::writeOwnerState( const ShooterMoveState& state, uint32 confirmedHitCount, uint32 killCount, float32 health, vector<uint8>& outBytes )
    {
        // 걸음 상태와 판정 결과를 한 선언(`ShooterOwnerNetState`)으로 쓴다 — 뒤에 값을 손으로 이어 쓰지 않는다.
        ShooterOwnerNetState owner;
        owner._move              = state;
        owner._confirmedHitCount = confirmedHitCount;
        owner._killCount         = killCount;
        owner._health            = health;
        BitWriter writer{ std::move( outBytes ) };
        writer.clear();
        const bool bWritten = ReplicatedLayout::writeFull( owner, writer );
        outBytes            = writer.releaseBytes();
        return bWritten;
    }

    bool ShooterNetPawnComponent::readOwnerState( const vector<uint8>& bytes, ShooterMoveState& outState, uint32& outConfirmedHitCount, uint32& outKillCount, float32& outHealth )
    {
        BitReader            reader( bytes.data(), static_cast<int32>( bytes.size() ) );
        ShooterOwnerNetState owner;
        owner._move = outState; // 복제 칸 밖의 걸음 상태 칸은 지금 값에서 이어진다
        if ( ReplicatedLayout::readFull( reader, owner, nullptr ) != FixedReadResult::Ok )
            return false;
        outState             = owner._move;
        outConfirmedHitCount = owner._confirmedHitCount;
        outKillCount         = owner._killCount;
        outHealth            = owner._health;
        return true;
    }

    bool ShooterNetPawnComponent::isClose( const ShooterMoveState& left, const ShooterMoveState& right ) const
    {
        const float3 delta = left._position - right._position;
        return delta.getLength() <= _reconcileTolerance && left._bOnGround == right._bOnGround;
    }

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
