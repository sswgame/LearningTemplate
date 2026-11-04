#include "pch.h"

#include "Core/Container/map.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Tests/LoopbackCluster.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetLockstep/LockstepSession.h"
#include "GameFramework/Kits/Feature/Network/NetLockstep/RollbackSession.h"

#include <cstring>

// 결정적 네트워크 키트 — 락스텝(셋이 같은 입력으로 같은 상태, 체크섬 비동기 감지, 입력 · 체크섬 창, 범위 밖 설정 거절)과
// 롤백(지연 · 손실 망에서 예측 → 되감기 → 양쪽 확정 상태 일치, 너무 앞서면 멈춤, 받는 창, 범위 밖 설정 거절). 루프백 클러스터 위에서 손 시각으로 돈다.

using namespace sw;

namespace
{
    struct NetLockstepTestInternal
    {
        /** @brief 서버(포트 4000) + 클라이언트 @p clientCount(4001 부터)를 30 프레임 동안 연결하고, 그 뒤 모든 엔드포인트에 @p conditions 를 겁니다. */
        static void connectLockstepCluster( test::LoopbackCluster& cluster, int32 clientCount, const NetEmulationConditions& conditions )
        {
            NetEndpointConfig settings;
            settings._sendInterval = 1.0 / 60.0;
            for ( int32 index = 0; index <= clientCount; ++index )
            {
                (void)cluster.addEndpoint( static_cast<uint16>( 4000 + index ), settings ); // 빈 클러스터라 포트가 겹치지 않는다
            }
            (void)cluster.connectClients( 30 ); // 연결됐는지는 케이스가 단언한다
            cluster.setConditions( conditions );
        }

        static uint32 mixHash( uint32 hash, const vector<vector<uint8>>& listInput )
        {
            for ( const vector<uint8>& input : listInput )
            {
                for ( const uint8 byte : input )
                {
                    hash = HashUtil::fnvStep32( hash, byte );
                }
                hash = HashUtil::fnvStep32( hash, 0xFFu );
            }
            return hash;
        }

        /** @brief 두 사람이 좌우로 움직이는 결정적 게임 — 상태는 두 자리입니다. */
        class DuelGame final : public IRollbackGame
        {
        public:
            int32             _arrPosition[2] = { 1, 2 };
            int32             _frame{ 0 };
            map<int32, int64> _mapHistory{}; ///< 프레임 → 그 프레임 뒤 상태(되감기가 덮어쓴다)
            void              saveState( vector<uint8>& outStateBuffer ) override
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
        };

        /** @brief 롤백이 프레임 번호를 알리도록 감쌉니다(되감기는 기록된 프레임부터 다시 센다). */
        class TrackedDuel final : public IRollbackGame
        {
        public:
            DuelGame _game;
            void     saveState( vector<uint8>& outStateBuffer ) override
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
        };

        static uint8 scriptInput( int32 player, int32 frame )
        {
            // 사람처럼 — 몇십 프레임마다 바꾼다.
            const int32 phase = ( frame / ( 17 + player * 6 ) + player ) % 4;
            return static_cast<uint8>( phase == 0 ? 1 : ( phase == 1 ? 0 : ( phase == 2 ? 2 : 3 ) ) );
        }

        /** @brief 프레임마다 쓴 입력을 남기는 게임입니다(되감기는 덮어쓴다). */
        class InputRecordingGame final : public IRollbackGame
        {
        public:
            int32                     _frame{ 0 };
            map<int32, vector<uint8>> _mapInput{};
            void                      saveState( vector<uint8>& outStateBuffer ) override { outStateBuffer.assign( 1, static_cast<uint8>( _frame ) ); }

            void loadState( const vector<uint8>& stateBuffer ) override { _frame = stateBuffer[0]; }

            void advanceFrame( const vector<uint8>& listInput, bool bResimulating ) override
            {
                (void)bResimulating;
                _mapInput[_frame++] = listInput;
            }
        };

        /** @brief 플레이어 1 의 롤백 입력 메시지 — 확인 · 이점은 비우고 [@p first, @p first + 입력 수) 프레임을 싣는다. */
        static void sendRollbackInput( RollbackSession& session, int32 first, const vector<uint8>& listInput )
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
            {
                body.writeBits( input, 8 );
            }
            SW_EXPECT_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
        }
    };
} // namespace

SW_TEST_CASE( NetLockstepTest, LockstepPlayersAdvanceIdenticallyAndDetectDesyncs )
{
    NetEmulationConditions conditions;
    conditions._latency  = 0.03;
    conditions._jitter   = 0.01;
    conditions._lossRate = 0.05f;
    test::LoopbackCluster cluster( 31u );
    NetLockstepTestInternal::connectLockstepCluster( cluster, 2, conditions );
    SW_REQUIRE_TRUE( cluster.areClientsConnected() );

    vector<LockstepSession>     listSession( 3 );
    vector<uint32>              listHash( 3, HashUtil::kFNVOffset32 );
    vector<map<uint32, uint32>> listHashByTick( 3 );
    for ( int32 index = 0; index < 3; ++index )
    {
        const int32 player = index == 0 ? 0 : cluster.getEndpoint( index ).getClientIndex() + 1;
        SW_REQUIRE_TRUE( listSession[static_cast<size_t>( index )].initialize( &cluster.getEndpoint( index ), 3, player, LockstepConfig{ 4 } ) );
    }
    vector<uint8> buffer;
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        cluster.step();
        for ( int32 index = 0; index < 3; ++index )
        {
            NetEndpoint&   host         = cluster.getEndpoint( index );
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( host.receiveMessage( connectionID, channel, buffer ) )
            {
                SW_EXPECT_TRUE( NetHandleResult::Handled == listSession[static_cast<size_t>( index )].handleMessage( connectionID, buffer ) );
            }
        }
        if ( frame % 2 != 0 || frame > 60 * 5 )
            continue; // 마지막 1 초는 입력 없이 남은 것을 비운다
        for ( int32 index = 0; index < 3; ++index )
        {
            LockstepSession& session = listSession[static_cast<size_t>( index )];
            (void)session.submitLocalInput( vector<uint8>{ static_cast<uint8>( session.getLocalPlayer() * 50 + frame / 2 ) } ); // 예약이 찼으면 이 틱의 입력만 빠진다 — 테스트는 양쪽이 같은 입력을 받는지만 본다
            vector<vector<uint8>> listInput;
            while ( session.tryAdvance( listInput ) )
            {
                const uint32 tick                                  = session.getCurrentTick() - 1;
                listHash[static_cast<size_t>( index )]             = NetLockstepTestInternal::mixHash( listHash[static_cast<size_t>( index )], listInput );
                listHashByTick[static_cast<size_t>( index )][tick] = listHash[static_cast<size_t>( index )];
                if ( tick % 10 == 0 )
                    session.reportChecksum( tick, listHash[static_cast<size_t>( index )] );
            }
        }
    }
    // 셋 모두 많이 나갔고, 함께 지난 틱마다 상태가 같다.
    uint32 commonTick = 0xFFFFFFFFu;
    for ( const LockstepSession& session : listSession )
    {
        commonTick = std::min( commonTick, session.getCurrentTick() );
    }
    SW_EXPECT_TRUE( commonTick > 120 );
    for ( uint32 tick = 0; tick < commonTick; ++tick )
    {
        SW_EXPECT_TRUE( listHashByTick[0][tick] == listHashByTick[1][tick] );
        SW_EXPECT_TRUE( listHashByTick[0][tick] == listHashByTick[2][tick] );
    }
    for ( const LockstepSession& session : listSession )
    {
        SW_EXPECT_FALSE( session.isDesynced() );
    }

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
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( cluster.getEndpoint( index ).receiveMessage( connectionID, channel, buffer ) )
            {
                (void)listSession[static_cast<size_t>( index )].handleMessage( connectionID, buffer );
            }
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
    NetEmulationConditions conditions;
    conditions._latency  = 0.05;
    conditions._jitter   = 0.015;
    conditions._lossRate = 0.05f;
    test::LoopbackCluster cluster( 31u );
    NetLockstepTestInternal::connectLockstepCluster( cluster, 1, conditions );
    SW_REQUIRE_TRUE( cluster.areClientsConnected() );

    NetLockstepTestInternal::TrackedDuel arrGame[2];
    RollbackSession                      arrSession[2];
    RollbackConfig                       settings;
    settings._inputDelay = 2;
    for ( int32 index = 0; index < 2; ++index )
    {
        SW_REQUIRE_TRUE( arrSession[index].initialize( &cluster.getEndpoint( index ), &arrGame[index], 2, index, settings ) );
    }

    vector<uint8> buffer;
    int32         arrLocalFrame[2] = { 0, 0 };
    for ( int32 frame = 0; frame < 60 * 8; ++frame )
    {
        cluster.step();
        for ( int32 index = 0; index < 2; ++index )
        {
            int32          connectionID = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( cluster.getEndpoint( index ).receiveMessage( connectionID, channel, buffer ) )
            {
                SW_EXPECT_TRUE( NetHandleResult::Handled == arrSession[index].handleMessage( connectionID, buffer ) );
            }
            if ( arrSession[index].advanceFrame( NetLockstepTestInternal::scriptInput( index, arrLocalFrame[index] ) ) )
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
    {
        mismatchCount += arrGame[0]._game._mapHistory[checkFrame] == arrGame[1]._game._mapHistory[checkFrame] ? 0 : 1;
    }
    SW_EXPECT_EQUAL( 0, mismatchCount );

    // 해시 이력의 `rollback` 줄(서버 디버깅 S7): 틱은 확정 프레임 번호이고, 시작하지 않은 세션은 남기지 않는다.
    for ( int32 index = 0; index < 2; ++index )
    {
        StateHashSample sample;
        SW_REQUIRE_TRUE( arrSession[index].sampleStateHash( 0, sample ) );
        SW_EXPECT_EQUAL( static_cast<uint64>( arrSession[index].getConfirmedFrame() ), sample._tick );
    }
    RollbackSession idleSession;
    StateHashSample idleSample;
    SW_EXPECT_FALSE( idleSession.sampleStateHash( 0, idleSample ) );

    // 상대가 멈추면(끊긴 것처럼) 최대 예측만큼 가고 멈춘다.
    const int32 before = arrSession[0].getFrame();
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        (void)arrSession[0].advanceFrame( 0 );
    }
    SW_EXPECT_TRUE( arrSession[0].getFrame() - before <= settings._maxPrediction + 2 );
    SW_EXPECT_TRUE( arrSession[0].getStallCount() > 0 );
}

/**
 * @brief [NetLockstepTest] 받는 창 [지금 − 64, 지금 + 64) 밖의 롤백 입력은 버린다 — 고리 칸(프레임 % 128)이 같은 먼 프레임이 받아 둔 입력을 덮지 않는다
 */
SW_TEST_CASE( NetLockstepTest, RollbackIgnoresInputsOutsideTheFrameWindow )
{
    NetLockstepTestInternal::InputRecordingGame game;
    RollbackSession                             session;
    SW_REQUIRE_TRUE( session.initialize( nullptr, &game, 2, 0, RollbackConfig{} ) );
    NetLockstepTestInternal::sendRollbackInput( session, 2, vector<uint8>{ 2, 3, 4, 5 } );
    NetLockstepTestInternal::sendRollbackInput( session, 3 + RollbackSession::kHistorySize, vector<uint8>{ 9 } );
    for ( int32 frame = 0; frame < 6; ++frame )
    {
        SW_EXPECT_TRUE( session.advanceFrame( 0 ) );
    }
    SW_EXPECT_EQUAL( 5, session.getConfirmedFrame() );
    for ( int32 frame = 2; frame < 6; ++frame )
    {
        SW_EXPECT_EQUAL( frame, static_cast<int32>( game._mapInput[frame][1] ) );
    }
}

/**
 * @brief [NetLockstepTest] 범위 밖 롤백 설정은 조용히 범위 안으로 맞추지 않고 오류로 거절하며, 거절된 세션은 진행하지 않는다
 * @details 예전에는 지연 20 을 8 로, 예측 0 을 1 로 바꿔 돌았다. 내 번호가 범위 밖이어도 받았다.
 */
SW_TEST_CASE( NetLockstepTest, RollbackRefusesOutOfRangeSettings )
{
    SW_EXPECT_LOG( "Rollback.RefusedToStart" ); // 범위 밖 설정은 오류와 함께 거절한다
    NetLockstepTestInternal::InputRecordingGame game;
    RollbackSession                             session;
    RollbackConfig                              tooLongDelay;
    tooLongDelay._inputDelay = RollbackSession::kMaxInputDelay + 1;
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 2, 0, tooLongDelay ) );
    SW_EXPECT_FALSE( session.advanceFrame( 0 ) );
    RollbackConfig noPrediction;
    noPrediction._maxPrediction = 0;
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 2, 0, noPrediction ) );
    RollbackConfig negativeAdvantage;
    negativeAdvantage._maxFrameAdvantage = -1;
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 2, 0, negativeAdvantage ) );
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 2, 2, RollbackConfig{} ) ); // 내 번호가 플레이어 수 밖
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 0, 0, RollbackConfig{} ) );
    SW_EXPECT_FALSE( session.initialize( nullptr, nullptr, 2, 0, RollbackConfig{} ) );
    RollbackConfig tooLongPrediction;
    tooLongPrediction._maxPrediction = RollbackSession::kMaxPrediction + 1;
    SW_EXPECT_FALSE( session.initialize( nullptr, &game, 2, 0, tooLongPrediction ) );
    SW_EXPECT_EQUAL( 0, session.getFrame() );
    // 경계 값은 받는다. `PROPERTY( Max )` 리터럴과 세션 상수가 어긋나면 여기서 드러난다.
    RollbackConfig atLimit;
    atLimit._inputDelay    = RollbackSession::kMaxInputDelay;
    atLimit._maxPrediction = RollbackSession::kMaxPrediction;
    SW_EXPECT_TRUE( session.initialize( nullptr, &game, 2, 0, atLimit ) );
    // 맞는 설정이면 시작한다.
    SW_REQUIRE_TRUE( session.initialize( nullptr, &game, 2, 0, RollbackConfig{} ) );
    SW_EXPECT_TRUE( session.advanceFrame( 0 ) );
}

/**
 * @brief [NetLockstepTest] 범위 밖 락스텝 설정과 맞지 않는 플레이어 번호는 오류로 거절하며, 거절된 세션은 입력을 예약하지 않고 틱도 넘기지 않는다
 * @details 예전에는 지연 65 를 64 로, 플레이어 수 0 을 1 로 바꿔 돌았다. 경계 값(`kMaxInputDelay`)은 받는다 — `PROPERTY( Max )` 리터럴과 상수가 어긋나면 여기서 드러난다.
 */
SW_TEST_CASE( NetLockstepTest, LockstepRefusesOutOfRangeSettings )
{
    SW_EXPECT_LOG( "Lockstep.RefusedToStart" ); // 범위 밖 설정은 오류와 함께 거절한다
    LockstepSession session;
    SW_EXPECT_FALSE( session.initialize( nullptr, 2, 0, LockstepConfig{ LockstepSession::kMaxInputDelay + 1 } ) );
    SW_EXPECT_FALSE( session.isStarted() );
    SW_EXPECT_FALSE( session.submitLocalInput( vector<uint8>{} ) );
    vector<vector<uint8>> listInput;
    SW_EXPECT_FALSE( session.tryAdvance( listInput ) );
    SW_EXPECT_FALSE( session.initialize( nullptr, 2, 0, LockstepConfig{ -1 } ) );
    SW_EXPECT_FALSE( session.initialize( nullptr, 0, 0, LockstepConfig{} ) );
    SW_EXPECT_FALSE( session.initialize( nullptr, 2, 2, LockstepConfig{} ) ); // 내 번호가 플레이어 수 밖
    SW_EXPECT_EQUAL( 0u, session.getCurrentTick() );
    SW_EXPECT_TRUE( session.initialize( nullptr, 2, 0, LockstepConfig{ LockstepSession::kMaxInputDelay } ) );
    SW_EXPECT_TRUE( session.isStarted() );
}

/**
 * @brief [NetLockstepTest] 플레이어마다 다음 틱이 아닌 입력(먼 틱 · 겹친 틱)과 창 밖 체크섬은 버린다 — 받아 둔 틱이 늘지 않고 정상 흐름은 그대로 간다
 */
SW_TEST_CASE( NetLockstepTest, FarFutureInputIsIgnored )
{
    LockstepSession session;
    SW_REQUIRE_TRUE( session.initialize( nullptr, 2, 0, LockstepConfig{ 2 } ) );
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
    {
        acceptedCount += session.submitLocalInput( vector<uint8>{ 0x33 } ) ? 1 : 0;
    }
    SW_EXPECT_EQUAL( static_cast<int32>( LockstepSession::kMaxInputLead ) - 2, acceptedCount );
    vector<vector<uint8>> listInput;
    for ( int32 tick = 0; tick < 3; ++tick )
    {
        SW_EXPECT_TRUE( session.tryAdvance( listInput ) );
    }
    SW_REQUIRE_EQUAL( size_t{ 2 }, listInput.size() );
    SW_EXPECT_TRUE( listInput[0] == vector<uint8>{ 0x22 } && listInput[1] == vector<uint8>{ 0x11 } );
    SW_EXPECT_FALSE( session.tryAdvance( listInput ) );
}

/**
 * @brief [NetLockstepTest] 상한을 넘는 입력 — 내 쪽은 예약하지 않고(나만 진행하는 비동기를 막는다), 받은 쪽은 길이를 자르지 않고 깨진 메시지로 버린다
 */
SW_TEST_CASE( NetLockstepTest, OversizeInputIsRefusedOnBothEnds )
{
    LockstepSession session;
    SW_REQUIRE_TRUE( session.initialize( nullptr, 2, 0, LockstepConfig{ 2 } ) );
    {
        SW_EXPECT_LOG( "Lockstep.InputTooLarge" ); // 상한을 넘는 입력은 오류와 함께 거절한다
        SW_EXPECT_FALSE( session.submitLocalInput( vector<uint8>( static_cast<size_t>( NetLockstepMessage::kMaxInputBytes ) + 1, 0x5A ) ) );
    }
    SW_EXPECT_TRUE( session.submitLocalInput( vector<uint8>( static_cast<size_t>( NetLockstepMessage::kMaxInputBytes ), 0x5A ) ) );

    NetMessageWriter writer;
    BitWriter&       body = writer.begin( NetLockstepMessage::kInput );
    body.writeVarUint( 1 );
    body.writeVarUint( 0 );
    body.writeVarUint( static_cast<uint64>( NetLockstepMessage::kMaxInputBytes ) + 1 );
    for ( int32 index = 0; index <= NetLockstepMessage::kMaxInputBytes; ++index )
    {
        body.writeBits( 0x5A, 8 );
    }
    SW_EXPECT_TRUE( NetHandleResult::Malformed == session.handleMessage( 0, writer.getBytes() ) );
}

/**
 * @brief [NetLockstepTest] 기다리는 체크섬은 창(`kChecksumWindow`) 아래로 빠지는 틱마다 하나씩 지우고, 창 안의 것은 남긴다
 */
SW_TEST_CASE( NetLockstepTest, PendingChecksumsExpireOneTickAtATime )
{
    LockstepSession session;
    SW_REQUIRE_TRUE( session.initialize( nullptr, 2, 0, LockstepConfig{ 0 } ) );
    for ( uint32 tick = 0; tick < 10; ++tick )
    {
        session.reportChecksum( tick, 7u ); // 상대(플레이어 1)는 알리지 않는다 — 열 틱이 기다린다
    }
    SW_EXPECT_EQUAL( 10, session.getPendingChecksumCount() );
    NetMessageWriter      writer;
    vector<vector<uint8>> listInput;
    const vector<uint8>   listEmpty;
    for ( uint32 tick = 0; tick < LockstepSession::kChecksumWindow + 10u; ++tick )
    {
        SW_REQUIRE_TRUE( session.submitLocalInput( listEmpty ) );
        BitWriter& body = writer.begin( NetLockstepMessage::kInput );
        body.writeVarUint( 1 );
        body.writeVarUint( tick );
        body.writeVarUint( 0 ); // 빈 입력
        SW_REQUIRE_TRUE( NetHandleResult::Handled == session.handleMessage( 0, writer.getBytes() ) );
        SW_REQUIRE_TRUE( session.tryAdvance( listInput ) );
        const uint32 currentTick = session.getCurrentTick();
        const int32  expired     = currentTick > LockstepSession::kChecksumWindow ? static_cast<int32>( currentTick - LockstepSession::kChecksumWindow ) : 0;
        SW_EXPECT_EQUAL( 10 - expired, session.getPendingChecksumCount() );
    }
    SW_EXPECT_EQUAL( 0, session.getQueuedTickCount() );
}
