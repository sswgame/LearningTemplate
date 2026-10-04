#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Container/map.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"

#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"
#include "GameFramework/Kits/Network/NetLockstep/RollbackSession.h"
#include "GameFramework/Kits/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestFramework.h"

// 결정적 네트워크 키트 — 락스텝(셋이 같은 입력으로 같은 상태, 입력 위조 거절, 체크섬 비동기 감지, 떠난 플레이어를 모두 같은 틱에 빼기, 입력 · 체크섬 창)과
// 롤백(지연 · 손실 망에서 예측 → 되감기 → 양쪽 확정 상태 일치, 너무 앞서면 멈춤, 확인 기반 다시 보내기, 시간 동기, 받는 창).

using namespace sw;

namespace
{
    struct NetTestCluster
    {
        LoopbackNetwork _network{ 31u };
        deque<NetHost>  _listHost; ///< deque — NetHost 는 옮길 수 없다(잠금을 품는다)
        float64         _time{ 0.0 };

        NetTestCluster( int32 clientCount, const LoopbackConditions& conditions )
        {
            for ( int32 index = 0; index <= clientCount; ++index )
                _listHost.emplace_back();
            NetHostSettings settings;
            settings._sendInterval = 1.0 / 60.0;
            for ( size_t index = 0; index < _listHost.size(); ++index )
                _listHost[index].initialize( _network.createEndpoint( static_cast<uint16>( 4000 + index ) ), settings );
            (void)_listHost[0].listen();
            for ( size_t index = 1; index < _listHost.size(); ++index )
                (void)_listHost[index].connect( NetAddress::makeLoopback( 4000 ) );
            for ( int32 frame = 0; frame < 30; ++frame )
                step();
            _network.setConditions( conditions );
        }

        void step()
        {
            _time += 1.0 / 60.0;
            for ( NetHost& host : _listHost )
                host.update( _time );
        }

        bool isAllConnected() const
        {
            for ( size_t index = 1; index < _listHost.size(); ++index )
            {
                if ( _listHost[index].getConnectionState( 0 ) != NetConnectionState::Connected )
                    return false;
            }
            return true;
        }
    };

    uint32 mixHash( uint32 hash, const vector<vector<uint8>>& listInput )
    {
        for ( const vector<uint8>& input : listInput )
        {
            for ( const uint8 byte : input )
                hash = ( hash ^ byte ) * 16777619u;
            hash = ( hash ^ 0xFFu ) * 16777619u;
        }
        return hash;
    }

    /** @brief 두 사람이 좌우로 움직이는 결정적 게임 — 상태는 두 자리입니다. */
    class DuelGame final : public IRollbackGame
    {
    public:
        void saveState( vector<uint8>& outStateBuffer ) override
        {
            outStateBuffer.resize( sizeof( _arrPosition ) );
            std::memcpy( outStateBuffer.data(), _arrPosition, sizeof( _arrPosition ) );
        }

        void loadState( const vector<uint8>& stateBuffer ) override { std::memcpy( _arrPosition, stateBuffer.data(), sizeof( _arrPosition ) ); }

        void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) override
        {
            (void)bResimulating;
            for ( size_t player = 0; player < 2; ++player )
            {
                if ( listInput[player] & 1u )
                    _arrPosition[player] += 3;
                if ( listInput[player] & 2u )
                    _arrPosition[player] -= 2;
                _arrPosition[player] = _arrPosition[player] * 31 % 100003; // 순서에 민감하게
            }
            _mapHistory[_frame++] = static_cast<int64>( _arrPosition[0] ) * 1000003 + _arrPosition[1];
        }

        int32             _arrPosition[2] = { 1, 2 };
        int32             _frame{ 0 };
        map<int32, int64> _mapHistory{}; ///< 프레임 → 그 프레임 뒤 상태(되감기가 덮어쓴다)
    };

    /** @brief 롤백이 프레임 번호를 알리도록 감쌉니다(되감기는 기록된 프레임부터 다시 센다). */
    class TrackedDuel final : public IRollbackGame
    {
    public:
        void saveState( vector<uint8>& outStateBuffer ) override
        {
            _game.saveState( outStateBuffer );
            outStateBuffer.push_back( static_cast<uint8>( _game._frame & 0xFF ) );
            outStateBuffer.push_back( static_cast<uint8>( ( _game._frame >> 8 ) & 0xFF ) );
        }

        void loadState( const vector<uint8>& stateBuffer ) override
        {
            _game.loadState( stateBuffer );
            _game._frame = stateBuffer[stateBuffer.size() - 2] | ( stateBuffer[stateBuffer.size() - 1] << 8 );
        }

        void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) override { _game.advanceFrame( listInput, bResimulating ); }

        DuelGame _game;
    };

    uint8 scriptInput( int32 player, int32 frame )
    {
        // 사람처럼 — 몇십 프레임마다 바꾼다.
        const int32 phase = ( frame / ( 17 + player * 6 ) + player ) % 4;
        return static_cast<uint8>( phase == 0 ? 1 : ( phase == 1 ? 0 : ( phase == 2 ? 2 : 3 ) ) );
    }

    /** @brief 하니스 위 롤백 — 서버가 플레이어 0, 클라이언트는 받은 번호 + 1. 연결되고 @p startDelayTicks 틱 뒤에 시작해 매 틱 한 프레임을 넣는다. */
    class RollbackNetSimSession final : public INetSimSession
    {
    public:
        RollbackNetSimSession( NetSimWorld& world, const RollbackSettings& settings, int32 startDelayTicks )
            : _game{}
            , _session{}
            , _settings{ settings }
            , _player{ -1 }
            , _startDelayTicks{ startDelayTicks }
            , _localFrame{ 0 }
        {
            world.getRouter().addHandler( &_session );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            NetHost& host = world.getHost();
            if ( _player < 0 )
            {
                const bool bConnected = world.isServer() ? host.getConnectedCount() > 0 : world.isConnected();
                if ( bConnected == false || _startDelayTicks-- > 0 )
                    return;
                _player = world.isServer() ? 0 : host.getClientIndex() + 1;
                _session.initialize( &host, &_game, 2, _player, _settings );
            }
            if ( _session.advanceFrame( scriptInput( _player, _localFrame ) ) )
                ++_localFrame;
        }

        TrackedDuel      _game;
        RollbackSession  _session;
        RollbackSettings _settings;
        int32            _player;
        int32            _startDelayTicks;
        int32            _localFrame;
    };

    class RollbackNetSimGame final : public INetSimGame
    {
    public:
        RollbackNetSimGame( const RollbackSettings& settings, int32 clientStartDelayTicks )
            : _settings{ settings }
            , _clientStartDelayTicks{ clientStartDelayTicks }
        {
        }

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            return make_unique<RollbackNetSimSession>( world, _settings, world.isServer() ? 0 : _clientStartDelayTicks );
        }

    private:
        RollbackSettings _settings;
        int32            _clientStartDelayTicks;
    };

    /** @brief 흉내 거르개 — 하니스 틱이 [_beginTick, _endTick) 안이면 모두 버린다(한 방향 연속 손실). */
    struct BurstDropper
    {
        const NetSimHarness* _pHarness{ nullptr };
        uint32               _beginTick{ 0 };
        uint32               _endTick{ 0 };
        int32                _droppedCount{ 0 };
    };

    bool dropDuringBurst( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
    {
        (void)to;
        (void)pData;
        (void)size;
        BurstDropper& dropper = *static_cast<BurstDropper*>( pContext );
        const uint32  tick    = dropper._pHarness->getTick();
        if ( tick < dropper._beginTick || dropper._endTick <= tick )
            return false;
        ++dropper._droppedCount;
        return true;
    }

    /** @brief 두 롤백 세션을 하니스에서 돌린 결과 — 양쪽이 확정한 프레임까지의 상태 차이와 되감기 수입니다. */
    struct RollbackRunResult
    {
        vector<int64> _listConfirmedState;
        int32         _arrFrame[2]{};
        int32         _arrRollbackCount[2]{};
        int32         _arrResimulatedFrameCount[2]{};
        int32         _confirmedFrame{ 0 };
        int32         _mismatchCount{ 0 };
        int32         _droppedCount{ 0 };
    };

    RollbackRunResult collectRollbackRun( const NetSimHarness& harness, int32 clientWorld )
    {
        RollbackRunResult            result;
        const RollbackNetSimSession* arrSession[2] = { static_cast<const RollbackNetSimSession*>( harness.getServer().getSession() ),
                                                       static_cast<const RollbackNetSimSession*>( harness.findClient( clientWorld )->getSession() ) };
        for ( int32 index = 0; index < 2; ++index )
        {
            result._arrFrame[index]                 = arrSession[index]->_session.getFrame();
            result._arrRollbackCount[index]         = arrSession[index]->_session.getRollbackCount();
            result._arrResimulatedFrameCount[index] = arrSession[index]->_session.getResimulatedFrameCount();
        }
        result._confirmedFrame                 = std::min( arrSession[0]->_session.getConfirmedFrame(), arrSession[1]->_session.getConfirmedFrame() );
        const map<int32, int64>& serverHistory = arrSession[0]->_game._game._mapHistory;
        const map<int32, int64>& clientHistory = arrSession[1]->_game._game._mapHistory;
        for ( int32 frame = 0; frame < result._confirmedFrame; ++frame )
        {
            const auto serverIter = serverHistory.find( frame );
            const auto clientIter = clientHistory.find( frame );
            const bool bSame      = serverIter != serverHistory.end() && clientIter != clientHistory.end() && serverIter->second == clientIter->second;
            result._mismatchCount += bSame ? 0 : 1;
            result._listConfirmedState.push_back( serverIter != serverHistory.end() ? serverIter->second : -1 );
        }
        return result;
    }

    /** @brief 클라이언트 → 서버가 틱 [120, 150) 동안 모두 사라지는 회선(그 밖은 지연 30 ms · 손실 2 %)에서 720 틱을 돌립니다. */
    RollbackRunResult runRollbackBurstLoss( uint32 seed )
    {
        RollbackNetSimGame game( RollbackSettings{}, 0 );
        NetSimHarness      harness;
        NetSimSettings     settings;
        settings._seed                       = seed;
        settings._hostSettings._sendInterval = 1.0 / 60.0;
        if ( harness.initialize( settings, &game ) == false )
            return RollbackRunResult{};
        BurstDropper dropper;
        dropper._pHarness  = &harness;
        dropper._beginTick = 120;
        dropper._endTick   = 150;
        NetSimLinkConditions link;
        link._downstream._latency          = 0.03;
        link._downstream._lossRate         = 0.02f;
        link._upstream                     = link._downstream;
        link._upstream._pDropFilter        = &dropDuringBurst;
        link._upstream._pDropFilterContext = &dropper;
        const int32 client                 = harness.addClient( link );
        harness.stepTicks( 720 );
        RollbackRunResult result = collectRollbackRun( harness, client );
        result._droppedCount     = dropper._droppedCount;
        return result;
    }
    /** @brief 프레임마다 쓴 입력을 남기는 게임입니다(되감기는 덮어쓴다). */
    class InputRecordingGame final : public IRollbackGame
    {
    public:
        void saveState( vector<uint8>& outStateBuffer ) override
        {
            outStateBuffer.assign( 1, static_cast<uint8>( _frame ) );
        }

        void loadState( const vector<uint8>& stateBuffer ) override { _frame = stateBuffer[0]; }

        void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) override
        {
            (void)bResimulating;
            _mapInput[_frame++] = listInput;
        }

        int32                     _frame{ 0 };
        map<int32, vector<uint8>> _mapInput{};
    };

    /** @brief 플레이어 1 의 롤백 입력 메시지 — 확인 · 이점은 비우고 [@p first, @p first + 입력 수) 프레임을 싣는다. */
    void sendRollbackInput( RollbackSession& session, int32 first, const vector<uint8>& listInput )
    {
        NetMessageWriter writer;
        BitWriter&       body = writer.begin( NetLockstepMessage::kRollbackInput );
        body.writeVarUint( 1 );
        body.writeVarUint( 0 );
        for ( int32 player = 0; player < 2; ++player )
        {
            body.writeVarUint( 0 );
            body.writeVarInt( 0 );
        }
        body.writeVarUint( static_cast<uint64>( first ) );
        body.writeVarUint( listInput.size() );
        for ( const uint8 input : listInput )
            body.writeBits( input, 8 );
        SW_EXPECT_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
    }

    /** @brief 하니스 락스텝의 시작 신호(게임 영역 첫 종류) — 서버가 모두 들어오면 보내고, 클라이언트는 이것을 받아 시작한다(시작 전 입력이 빠지지 않게). */
    constexpr uint8 kLockstepStartKind = NetMessageRange::kGame;

    /**
     * @brief 하니스 위 락스텝 — 서버 0, 클라이언트는 받은 번호 + 1. 틱마다 입력을 내고(받지 않으면 다음 틱에 다시), 진행한 틱마다 입력 해시를 남기고
     *        @p checksumInterval 틱마다 체크섬을 알린다(0 이면 알리지 않는다).
     */
    class LeaveLockstepSession final : public INetSimSession, public INetMessageHandler
    {
    public:
        LeaveLockstepSession( NetSimWorld& world, int32 playerCount, int32 checksumInterval )
            : _session{}
            , _listHashByTick{}
            , _pWorld{ &world }
            , _playerCount{ playerCount }
            , _checksumInterval{ checksumInterval }
            , _hash{ 2166136261u }
            , _bStarted{ false }
        {
            world.getRouter().addHandler( &_session );
            world.getRouter().addHandler( this );
        }

        uint8           getMessageRangeBase() const override { return NetMessageRange::kGame; }
        uint16          getMessageKindMask() const override { return 1u; }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
        {
            (void)context;
            (void)body;
            if ( _bStarted == false && _pWorld->isServer() == false )
                start();
            return NetHandleResult::Handled;
        }

        void onHostEvent( NetSimWorld& world, const NetHostEvent& event ) override
        {
            NetHost& host = world.getHost();
            if ( world.isServer() == false || _bStarted || event._kind != NetHostEvent::Kind::Connected || host.getConnectedCount() < _playerCount - 1 )
                return;
            start();
            const uint8 startKind = kLockstepStartKind;
            (void)host.broadcast( NetChannelType::ReliableOrdered, &startKind, 1 );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            if ( _bStarted == false )
                return;
            (void)_session.submitLocalInput( vector<uint8>{ static_cast<uint8>( static_cast<uint32>( _session.getLocalPlayer() * 40 ) + world.getLocalTick() ) } );
            vector<vector<uint8>> listInput;
            while ( _session.tryAdvance( listInput ) )
            {
                _hash = mixHash( _hash, listInput );
                _listHashByTick.push_back( _hash );
                const uint32 tick = _session.getCurrentTick() - 1;
                if ( _checksumInterval > 0 && tick % static_cast<uint32>( _checksumInterval ) == 0 )
                    _session.reportChecksum( tick, _hash );
            }
        }

        LockstepSession _session;
        vector<uint32>  _listHashByTick; ///< 틱 → 그 틱까지의 입력 해시

    private:
        void start()
        {
            NetHost& host = _pWorld->getHost();
            _session.initialize( &host, _playerCount, _pWorld->isServer() ? 0 : host.getClientIndex() + 1, 3 );
            _bStarted = true;
        }

        NetSimWorld* _pWorld;
        int32        _playerCount;
        int32        _checksumInterval;
        uint32       _hash;
        bool         _bStarted;
    };

    class LeaveLockstepGame final : public INetSimGame
    {
    public:
        LeaveLockstepGame( int32 playerCount, int32 serverChecksumInterval, int32 clientChecksumInterval )
            : _playerCount{ playerCount }
            , _serverChecksumInterval{ serverChecksumInterval }
            , _clientChecksumInterval{ clientChecksumInterval }
        {
        }

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            return make_unique<LeaveLockstepSession>( world, _playerCount, world.isServer() ? _serverChecksumInterval : _clientChecksumInterval );
        }

    private:
        int32 _playerCount;
        int32 _serverChecksumInterval;
        int32 _clientChecksumInterval;
    };

    const LeaveLockstepSession& getLeaveLockstepSession( const NetSimWorld& world ) { return *static_cast<const LeaveLockstepSession*>( world.getSession() ); }

    /** @brief 흉내 거르개 — 모두 버린다(회선이 끊긴 것처럼 — 양쪽이 연결 제한 시간으로 안다). */
    bool dropAllPackets( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
    {
        (void)to;
        (void)pData;
        (void)size;
        (void)pContext;
        return true;
    }

    /** @brief 락스텝 떠남 시험의 결과 — 남은 둘(서버 · 클라이언트 A)의 틱 해시와 떠남 틱입니다. */
    struct LockstepLeaveResult
    {
        vector<uint32> _arrHashByTick[2];
        uint32         _arrLeaveTick[2][2]{}; ///< 남은 쪽 → (깨끗이 떠난 B, 끊긴 C) 의 떠남 틱
        uint32         _arrTickAtCut[2]{};
        int32          _arrPendingChecksum[2]{};
        bool           _arrDesynced[2]{};
    };

    /**
     * @brief 넷이 하는 판(서버 + 클라이언트 A · B · C, 지연 30 ms · 손실 3 %) — 120 틱 뒤 B 가 떠나고(끊김 알림), 240 틱에 C 의 회선이 끊긴다(연결 제한 시간 1 초).
     *        480 틱까지 돌린다.
     */
    LockstepLeaveResult runLockstepLeave( uint32 seed )
    {
        LeaveLockstepGame game( 4, 10, 10 );
        NetSimHarness     harness;
        NetSimSettings    settings;
        settings._seed                       = seed;
        settings._hostSettings._timeout      = 1.0;
        settings._hostSettings._sendInterval = 1.0 / 60.0;
        LockstepLeaveResult result;
        if ( harness.initialize( settings, &game ) == false )
            return result;
        NetSimLinkConditions link;
        link._upstream._latency  = 0.03;
        link._upstream._lossRate = 0.03f;
        link._downstream         = link._upstream;
        const int32 clientA      = harness.addClient( link );
        const int32 clientB      = harness.addClient( link );
        const int32 clientC      = harness.addClient( link );
        harness.stepTicks( 120 );
        const int32 playerB = getLeaveLockstepSession( *harness.findClient( clientB ) )._session.getLocalPlayer();
        const int32 playerC = getLeaveLockstepSession( *harness.findClient( clientC ) )._session.getLocalPlayer();
        harness.removeClient( clientB );
        harness.stepTicks( 120 );
        NetSimLinkConditions cut;
        cut._upstream._pDropFilter   = &dropAllPackets;
        cut._downstream._pDropFilter = &dropAllPackets;
        harness.setLinkConditions( clientC, cut );
        const NetSimWorld* arrWorld[2] = { &harness.getServer(), harness.findClient( clientA ) };
        for ( int32 index = 0; index < 2; ++index )
            result._arrTickAtCut[index] = getLeaveLockstepSession( *arrWorld[index] )._session.getCurrentTick();
        harness.stepTicks( 240 );
        for ( int32 index = 0; index < 2; ++index )
        {
            const LeaveLockstepSession& session = getLeaveLockstepSession( *arrWorld[index] );
            result._arrHashByTick[index]        = session._listHashByTick;
            result._arrLeaveTick[index][0]      = session._session.getLeaveTick( playerB );
            result._arrLeaveTick[index][1]      = session._session.getLeaveTick( playerC );
            result._arrPendingChecksum[index]   = session._session.getPendingChecksumCount();
            result._arrDesynced[index]          = session._session.isDesynced();
        }
        return result;
    }
} // namespace

SW_TEST_CASE( NetLockstepTest, LockstepPlayersAdvanceIdenticallyAndDetectDesyncs )
{
    LoopbackConditions conditions;
    conditions._latency  = 0.03;
    conditions._jitter   = 0.01;
    conditions._lossRate = 0.05f;
    NetTestCluster cluster( 2, conditions );
    SW_ASSERT_TRUE( cluster.isAllConnected() );

    vector<LockstepSession>     listSession( 3 );
    vector<uint32>              listHash( 3, 2166136261u );
    vector<map<uint32, uint32>> listHashByTick( 3 );
    for ( int32 index = 0; index < 3; ++index )
    {
        const int32 player = index == 0 ? 0 : cluster._listHost[static_cast<size_t>( index )].getClientIndex() + 1;
        listSession[static_cast<size_t>( index )].initialize( &cluster._listHost[static_cast<size_t>( index )], 3, player, 4 );
    }
    vector<uint8> buffer;
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        cluster.step();
        for ( int32 index = 0; index < 3; ++index )
        {
            NetHost&       host         = cluster._listHost[static_cast<size_t>( index )];
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( host.receiveMessage( connectionId, channel, buffer ) )
                SW_EXPECT_TRUE( NetHandleResult::Handled == listSession[static_cast<size_t>( index )].handleMessage( connectionId, buffer ) );
        }
        if ( frame % 2 != 0 || frame > 60 * 5 )
            continue; // 마지막 1 초는 입력 없이 남은 것을 비운다
        for ( int32 index = 0; index < 3; ++index )
        {
            LockstepSession& session = listSession[static_cast<size_t>( index )];
            (void)session.submitLocalInput( vector<uint8>{ static_cast<uint8>( session.getLocalPlayer() * 50 + frame / 2 ) } );
            vector<vector<uint8>> listInput;
            while ( session.tryAdvance( listInput ) )
            {
                const uint32 tick                                  = session.getCurrentTick() - 1;
                listHash[static_cast<size_t>( index )]             = mixHash( listHash[static_cast<size_t>( index )], listInput );
                listHashByTick[static_cast<size_t>( index )][tick] = listHash[static_cast<size_t>( index )];
                if ( tick % 10 == 0 )
                    session.reportChecksum( tick, listHash[static_cast<size_t>( index )] );
            }
        }
    }
    // 셋 모두 많이 나갔고, 함께 지난 틱마다 상태가 같다.
    uint32 commonTick = 0xFFFFFFFFu;
    for ( const LockstepSession& session : listSession )
        commonTick = std::min( commonTick, session.getCurrentTick() );
    SW_EXPECT_TRUE( commonTick > 120 );
    for ( uint32 tick = 0; tick < commonTick; ++tick )
    {
        SW_EXPECT_TRUE( listHashByTick[0][tick] == listHashByTick[1][tick] );
        SW_EXPECT_TRUE( listHashByTick[0][tick] == listHashByTick[2][tick] );
    }
    for ( const LockstepSession& session : listSession )
        SW_EXPECT_FALSE( session.isDesynced() );

    // 비동기 — 한 사람이 다른 체크섬을 내면 모두 안다.
    const uint32 badTick = commonTick + 100;
    listSession[0].reportChecksum( badTick, 1 );
    listSession[1].reportChecksum( badTick, 1 );
    listSession[2].reportChecksum( badTick, 2 );
    for ( int32 frame = 0; frame < 60; ++frame )
    {
        cluster.step();
        for ( int32 index = 0; index < 3; ++index )
        {
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( cluster._listHost[static_cast<size_t>( index )].receiveMessage( connectionId, channel, buffer ) )
                (void)listSession[static_cast<size_t>( index )].handleMessage( connectionId, buffer );
        }
    }
    for ( const LockstepSession& session : listSession )
    {
        SW_EXPECT_TRUE( session.isDesynced() );
        SW_EXPECT_EQUAL( static_cast<int32>( badTick ), static_cast<int32>( session.getDesyncTick() ) );
    }
}

SW_TEST_CASE( NetLockstepTest, RollbackPredictsRewindsAndConvergesOnBothSides )
{
    LoopbackConditions conditions;
    conditions._latency  = 0.05;
    conditions._jitter   = 0.015;
    conditions._lossRate = 0.05f;
    NetTestCluster cluster( 1, conditions );
    SW_ASSERT_TRUE( cluster.isAllConnected() );

    TrackedDuel      arrGame[2];
    RollbackSession  arrSession[2];
    RollbackSettings settings;
    settings._inputDelay = 2;
    for ( int32 index = 0; index < 2; ++index )
        arrSession[index].initialize( &cluster._listHost[static_cast<size_t>( index )], &arrGame[index], 2, index, settings );

    vector<uint8> buffer;
    int32         arrLocalFrame[2] = { 0, 0 };
    for ( int32 frame = 0; frame < 60 * 8; ++frame )
    {
        cluster.step();
        for ( int32 index = 0; index < 2; ++index )
        {
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( cluster._listHost[static_cast<size_t>( index )].receiveMessage( connectionId, channel, buffer ) )
                SW_EXPECT_TRUE( NetHandleResult::Handled == arrSession[index].handleMessage( connectionId, buffer ) );
            if ( arrSession[index].advanceFrame( scriptInput( index, arrLocalFrame[index] ) ) )
                ++arrLocalFrame[index];
        }
    }
    // 예측이 틀린 적이 있고, 되감아 고쳤다.
    SW_EXPECT_TRUE( arrSession[0].getRollbackCount() > 0 && arrSession[1].getRollbackCount() > 0 );
    SW_EXPECT_TRUE( arrSession[0].getFrame() > 400 && arrSession[1].getFrame() > 400 );
    // 둘 다 확정한 프레임까지는 상태가 같다.
    const int32 confirmed = std::min( arrSession[0].getConfirmedFrame(), arrSession[1].getConfirmedFrame() );
    SW_EXPECT_TRUE( confirmed > 350 );
    int32 mismatchCount = 0;
    for ( int32 checkFrame = 0; checkFrame < confirmed; ++checkFrame )
        mismatchCount += arrGame[0]._game._mapHistory[checkFrame] == arrGame[1]._game._mapHistory[checkFrame] ? 0 : 1;
    SW_EXPECT_EQUAL( 0, mismatchCount );

    // 상대가 멈추면(끊긴 것처럼) 최대 예측만큼 가고 멈춘다.
    const int32 before = arrSession[0].getFrame();
    for ( int32 frame = 0; frame < 30; ++frame )
        (void)arrSession[0].advanceFrame( 0 );
    SW_EXPECT_TRUE( arrSession[0].getFrame() - before <= settings._maxPrediction + 2 );
    SW_EXPECT_TRUE( arrSession[0].getStallCount() > 0 );
}

/**
 * @brief [NetLockstepTest] 클라이언트 → 서버가 30 틱 동안 모두 사라져도 롤백이 멈추지 않고 양쪽 확정 상태가 같다 — 같은 씨앗이면 되감기 수까지 같은 실행
 * @details 보내는 쪽은 상대가 확인한 다음 프레임부터 싣는다. "최근 N 개" 만 겹쳐 보내면 연속 손실이 N 을 넘을 때 빈 프레임이 다시 오지 않아
 *          서버의 확인 프레임이 멈추고, 두 쪽 모두 최대 예측에서 영원히 멈춘다.
 */
SW_TEST_CASE( NetLockstepTest, RollbackSurvivesBurstLossLongerThanRedundancy )
{
    const RollbackRunResult first = runRollbackBurstLoss( 7u );
    SW_EXPECT_TRUE( first._droppedCount > 20 );
    SW_EXPECT_TRUE_MSG( first._arrFrame[0] > 600 && first._arrFrame[1] > 600, "both sides keep advancing after the burst" );
    SW_EXPECT_TRUE( first._confirmedFrame > 550 );
    SW_EXPECT_EQUAL( 0, first._mismatchCount );

    const RollbackRunResult second = runRollbackBurstLoss( 7u );
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_EXPECT_EQUAL( first._arrFrame[index], second._arrFrame[index] );
        SW_EXPECT_EQUAL( first._arrRollbackCount[index], second._arrRollbackCount[index] );
        SW_EXPECT_EQUAL( first._arrResimulatedFrameCount[index], second._arrResimulatedFrameCount[index] );
    }
    SW_EXPECT_TRUE( first._listConfirmedState == second._listConfirmedState );
}

/**
 * @brief [NetLockstepTest] 늦게 시작한 상대보다 앞선 쪽이 시간 동기로 쉬어 두 프레임이 `_maxFrameAdvantage` 안으로 모이고, 확정 상태는 같다
 * @details 앞선 쪽이 쉬지 않으면 최대 예측 가까이 앞선 채로 굳어, 상대 입력이 바뀔 때마다 그만큼 되감는다.
 */
SW_TEST_CASE( NetLockstepTest, RollbackLeadingPeerWaitsForTheOther )
{
    RollbackNetSimGame game( RollbackSettings{}, 12 );
    NetSimHarness      harness;
    NetSimSettings     settings;
    settings._hostSettings._sendInterval = 1.0 / 60.0;
    SW_ASSERT_TRUE( harness.initialize( settings, &game ) );
    NetSimLinkConditions link;
    link._upstream._latency = 0.05;
    link._downstream        = link._upstream;
    const int32 client      = harness.addClient( link );
    harness.stepTicks( 600 );

    const RollbackSession& server = static_cast<const RollbackNetSimSession*>( harness.getServer().getSession() )->_session;
    const RollbackSession& other  = static_cast<const RollbackNetSimSession*>( harness.findClient( client )->getSession() )->_session;
    const int32            lead   = server.getFrame() - other.getFrame();
    SW_EXPECT_TRUE( server.getTimeSyncWaitCount() > 0 );
    SW_EXPECT_TRUE_MSG( -3 <= lead && lead <= RollbackSettings{}._maxFrameAdvantage + 1, "the leading side waited until the two frames met" );
    SW_EXPECT_TRUE( other.getFrame() > 500 );
    const RollbackRunResult result = collectRollbackRun( harness, client );
    SW_EXPECT_TRUE( result._confirmedFrame > 480 );
    SW_EXPECT_EQUAL( 0, result._mismatchCount );
}

/**
 * @brief [NetLockstepTest] 받는 창 [지금 − 64, 지금 + 64) 밖의 롤백 입력은 버린다 — 고리 칸(프레임 % 128)이 같은 먼 프레임이 받아 둔 입력을 덮지 않는다
 */
SW_TEST_CASE( NetLockstepTest, RollbackIgnoresInputsOutsideTheFrameWindow )
{
    InputRecordingGame game;
    RollbackSession    session;
    session.initialize( nullptr, &game, 2, 0, RollbackSettings{} );
    sendRollbackInput( session, 2, vector<uint8>{ 2, 3, 4, 5 } );
    sendRollbackInput( session, 3 + RollbackSession::kHistorySize, vector<uint8>{ 9 } );
    for ( int32 frame = 0; frame < 6; ++frame )
        SW_EXPECT_TRUE( session.advanceFrame( 0 ) );
    SW_EXPECT_EQUAL( 5, session.getConfirmedFrame() );
    for ( int32 frame = 2; frame < 6; ++frame )
        SW_EXPECT_EQUAL( frame, static_cast<int32>( game._mapInput[frame][1] ) );
}

/**
 * @brief [NetLockstepTest] 한 명이 떠나도(끊김 알림 · 회선 끊김 둘 다) 남은 사람들이 멈추지 않고, 모두 같은 틱부터 그 사람을 빈 입력으로 둬 상태가 같다
 * @details 서버가 정한 떠남 틱은 그 사람의 마지막 입력 다음 틱이다 — 그 앞 입력과 같은 신뢰 순서로 나가 모두가 같은 틱에 적용한다. 같은 씨앗이면 같은 실행.
 */
SW_TEST_CASE( NetLockstepTest, LeavingPlayerDoesNotStallOthers )
{
    const LockstepLeaveResult first = runLockstepLeave( 5u );
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_EXPECT_TRUE( first._arrTickAtCut[index] > 150 );
        SW_EXPECT_TRUE_MSG( first._arrHashByTick[index].size() > first._arrTickAtCut[index] + 150, "the remaining players keep advancing after both leave" );
        SW_EXPECT_TRUE( first._arrLeaveTick[index][0] != LockstepSession::kNoLeaveTick );
        SW_EXPECT_TRUE( first._arrLeaveTick[index][1] != LockstepSession::kNoLeaveTick );
        SW_EXPECT_FALSE( first._arrDesynced[index] );
        SW_EXPECT_TRUE( first._arrPendingChecksum[index] <= 3 );
    }
    SW_EXPECT_EQUAL( first._arrLeaveTick[0][0], first._arrLeaveTick[1][0] );
    SW_EXPECT_EQUAL( first._arrLeaveTick[0][1], first._arrLeaveTick[1][1] );
    const size_t commonCount = std::min( first._arrHashByTick[0].size(), first._arrHashByTick[1].size() );
    int32        mismatch    = 0;
    for ( size_t tick = 0; tick < commonCount; ++tick )
        mismatch += first._arrHashByTick[0][tick] == first._arrHashByTick[1][tick] ? 0 : 1;
    SW_EXPECT_EQUAL( 0, mismatch );

    const LockstepLeaveResult second = runLockstepLeave( 5u );
    SW_EXPECT_TRUE( first._arrHashByTick[0] == second._arrHashByTick[0] );
    SW_EXPECT_EQUAL( first._arrLeaveTick[0][1], second._arrLeaveTick[0][1] );
}

/**
 * @brief [NetLockstepTest] 한쪽만 체크섬을 알려도 기다리는 체크섬은 창(`kChecksumWindow`) 안에서 멈춘다
 */
SW_TEST_CASE( NetLockstepTest, ChecksumHistoryStaysBounded )
{
    LeaveLockstepGame game( 2, 1, 0 );
    NetSimHarness     harness;
    SW_ASSERT_TRUE( harness.initialize( NetSimSettings{}, &game ) );
    (void)harness.addClient( NetSimLinkConditions{} );
    harness.stepTicks( 800 );
    const LockstepSession& server = getLeaveLockstepSession( harness.getServer() )._session;
    SW_EXPECT_TRUE( server.getCurrentTick() > 700 );
    SW_EXPECT_TRUE( server.getPendingChecksumCount() <= static_cast<int32>( LockstepSession::kChecksumWindow ) + 1 );
    SW_EXPECT_FALSE( server.isDesynced() );
}

/**
 * @brief [NetLockstepTest] 플레이어마다 다음 틱이 아닌 입력(먼 틱 · 겹친 틱)과 창 밖 체크섬은 버린다 — 받아 둔 틱이 늘지 않고 정상 흐름은 그대로 간다
 */
SW_TEST_CASE( NetLockstepTest, FarFutureInputIsIgnored )
{
    LockstepSession session;
    session.initialize( nullptr, 2, 0, 2 );
    const int32      queuedBefore = session.getQueuedTickCount();
    NetMessageWriter writer;
    for ( uint32 tick = 100; tick < 100000; tick += 997 )
    {
        BitWriter& body = writer.begin( NetLockstepMessage::kInput );
        body.writeVarUint( 1 );
        body.writeVarUint( tick );
        body.writeVarUint( 1 );
        body.writeBits( 0xAB, 8 );
        SW_EXPECT_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
    }
    SW_EXPECT_EQUAL( queuedBefore, session.getQueuedTickCount() );
    BitWriter& checksum = writer.begin( NetLockstepMessage::kChecksum );
    checksum.writeVarUint( 1 );
    checksum.writeVarUint( 1000000 );
    checksum.writeUint32( 7 );
    SW_EXPECT_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
    SW_EXPECT_EQUAL( 0, session.getPendingChecksumCount() );

    // 다음 틱(2)은 받는다 — 내 입력과 함께 세 틱을 진행한다.
    BitWriter& next = writer.begin( NetLockstepMessage::kInput );
    next.writeVarUint( 1 );
    next.writeVarUint( 2 );
    next.writeVarUint( 1 );
    next.writeBits( 0x11, 8 );
    SW_EXPECT_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
    SW_EXPECT_TRUE( session.submitLocalInput( vector<uint8>{ 0x22 } ) );
    // 내 입력은 지금(0) + kMaxInputLead 까지만 예약된다.
    int32 acceptedCount = 0;
    for ( int32 index = 0; index < 1000; ++index )
        acceptedCount += session.submitLocalInput( vector<uint8>{ 0x33 } ) ? 1 : 0;
    SW_EXPECT_EQUAL( static_cast<int32>( LockstepSession::kMaxInputLead ) - 2, acceptedCount );
    vector<vector<uint8>> listInput;
    for ( int32 tick = 0; tick < 3; ++tick )
        SW_EXPECT_TRUE( session.tryAdvance( listInput ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, listInput.size() );
    SW_EXPECT_TRUE( listInput[0] == vector<uint8>{ 0x22 } && listInput[1] == vector<uint8>{ 0x11 } );
    SW_EXPECT_FALSE( session.tryAdvance( listInput ) );
}
