#include "pch.h"

#include "Core/Container/deque.h"
#include "Core/Container/map.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"

#include "GameFramework/Kits/Network/NetLockstep/LockstepSession.h"
#include "GameFramework/Kits/Network/NetLockstep/RollbackSession.h"

#include "TestFramework/TestFramework.h"

// 결정적 네트워크 키트 — 락스텝(셋이 같은 입력으로 같은 상태, 입력 위조 거절, 체크섬 비동기 감지)과
// 롤백(지연 · 손실 망에서 예측 → 되감기 → 양쪽 확정 상태 일치, 너무 앞서면 멈춤).

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
            settings._sendInterval = 1.0f / 60.0f;
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

        void setFrame( int32 frame ) { _frame = frame; }

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
} // namespace

SW_TEST_CASE( NetLockstepTest, LockstepPlayersAdvanceIdenticallyAndDetectDesyncs )
{
    LoopbackConditions conditions;
    conditions._latency  = 0.03f;
    conditions._jitter   = 0.01f;
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
                SW_EXPECT_TRUE( listSession[static_cast<size_t>( index )].handleMessage( connectionId, buffer ) );
        }
        if ( frame % 2 != 0 || frame > 60 * 5 )
            continue; // 마지막 1 초는 입력 없이 남은 것을 비운다
        for ( int32 index = 0; index < 3; ++index )
        {
            LockstepSession& session = listSession[static_cast<size_t>( index )];
            session.submitLocalInput( vector<uint8>{ static_cast<uint8>( session.getLocalPlayer() * 50 + frame / 2 ) } );
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
    conditions._latency  = 0.05f;
    conditions._jitter   = 0.015f;
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
                SW_EXPECT_TRUE( arrSession[index].handleMessage( connectionId, buffer ) );
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
