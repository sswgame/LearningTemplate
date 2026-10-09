#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "Engine/Common/EngineServices.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "TestFramework/TestFramework.h"

// 권위 서버 네트워크 키트 — 스냅샷 델타(바뀐 것 · 사라진 것 · 예산 넘침), 지연 · 손실 망 위의 복제와 보간, 확인 기반 입력 전송(연속 손실 뒤 빈틈 없음),
// 관련 정책, 클라이언트 예측 되맞추기, 랙 보정 되감기.

using namespace sw;

namespace
{
    vector<uint8> makeFloatBytes( float32 value )
    {
        BitWriter writer;
        writer.writeFloat( value );
        return writer.getBytes();
    }

    float32 readFloatBytes( const vector<uint8>& buffer )
    {
        BitReader reader( buffer.data(), static_cast<int32>( buffer.size() ) );
        return reader.readFloat();
    }

    /** @brief 권위 서버 키트 영역에서 종류 하나만 받아 세는 처리기입니다(같은 영역을 나눠 쓰는 다른 처리기 — 게임이 키트 영역 끝을 빌리는 자리). */
    class KindCounter final : public INetMessageHandler
    {
    public:
        explicit KindCounter( uint8 kind )
            : _kind{ kind }
            , _count{ 0 }
        {
        }

        uint8           getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        uint16          getMessageKindMask() const override { return static_cast<uint16>( 1u << ( _kind - NetKitMessageRange::kClientServer ) ); }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override
        {
            (void)context;
            (void)body;
            ++_count;
            return NetHandleResult::Handled;
        }

        uint8 _kind;
        int32 _count;
    };

    /** @brief 엔티티 셋을 보이지 않게 하는 정책입니다. */
    class HideThreePolicy final : public IReplicationPolicy
    {
    public:
        bool isRelevant( int32 connectionId, const NetEntityState& entity ) const override
        {
            (void)connectionId;
            return entity._entityId != 3;
        }
    };

    /** @brief 손으로 지은 입력 메시지 — [@p firstTick, @p firstTick + 개수) 틱의 입력을 오래된 것부터 싣는다(보는 틱 0). */
    vector<uint8> makeInputMessage( uint32 firstTick, const vector<vector<uint8>>& listPayload )
    {
        BitWriter writer;
        writer.writeBits( NetClientServerMessage::kInput, 8 );
        writer.writeVarUint( 0 ); // 보는 틱 × 256
        writer.writeVarUint( firstTick );
        writer.writeVarUint( listPayload.size() );
        for ( const vector<uint8>& payload : listPayload )
        {
            writer.writeBlob( payload.data(), static_cast<int32>( payload.size() ) );
        }
        return writer.getBytes();
    }

    /** @brief 흉내 거르개 — `_bDropping` 인 동안 보내는 패킷을 모두 버립니다(한 방향 연속 손실). */
    struct InputBurstDropper
    {
        int32 _droppedCount{ 0 };
        bool  _bDropping{ false };
    };

    bool dropInputBurst( const NetAddress& to, const uint8* pData, int32 size, void* pContext )
    {
        (void)to;
        (void)pData;
        (void)size;
        InputBurstDropper& dropper = *static_cast<InputBurstDropper*>( pContext );
        if ( dropper._bDropping == false )
            return false;
        ++dropper._droppedCount;
        return true;
    }
} // namespace

SW_TEST_CASE( NetClientServerTest, SnapshotDeltasCarryChangesRemovalsAndRespectBudgets )
{
    NetSnapshot baseline;
    baseline._tick = 10;
    for ( uint32 entityId = 1; entityId <= 4; ++entityId )
    {
        baseline._listEntity.push_back( NetEntityState{ makeFloatBytes( static_cast<float32>( entityId ) ), entityId, 7 } );
    }
    NetSnapshot current;
    current._tick = 12;
    current._listEntity.push_back( NetEntityState{ makeFloatBytes( 1.0f ), 1, 7 } );  // 그대로
    current._listEntity.push_back( NetEntityState{ makeFloatBytes( 20.0f ), 2, 7 } ); // 바뀜
    current._listEntity.push_back( NetEntityState{ makeFloatBytes( 50.0f ), 5, 8 } ); // 새로
    current.sortEntities();                                                           // 3 · 4 사라짐

    BitWriter   fullWriter;
    NetSnapshot unused;
    current.writeDelta( fullWriter, nullptr, 1000, unused );
    BitWriter   deltaWriter;
    NetSnapshot written;
    current.writeDelta( deltaWriter, &baseline, 1000, written );
    SW_EXPECT_TRUE( deltaWriter.getByteCount() < fullWriter.getByteCount() + 4 );

    BitReader   reader( deltaWriter.getBytes().data(), deltaWriter.getByteCount() );
    NetSnapshot decoded;
    SW_ASSERT_TRUE( NetSnapshot::readDelta( reader, &baseline, decoded ) );
    SW_EXPECT_EQUAL( 12, static_cast<int32>( decoded._tick ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( decoded._listEntity.size() ) );
    SW_EXPECT_NEAR_EQUAL( 20.0f, readFloatBytes( decoded.findEntity( 2 )->_buffer ), 1.0e-6f );
    SW_EXPECT_EQUAL( 8, static_cast<int32>( decoded.findEntity( 5 )->_typeId ) );
    SW_EXPECT_NULL( decoded.findEntity( 3 ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( written._listEntity.size() ) );

    // 기준이 없으면 풀지 않는다.
    BitReader   noBaseline( deltaWriter.getBytes().data(), deltaWriter.getByteCount() );
    NetSnapshot failed;
    SW_EXPECT_FALSE( NetSnapshot::readDelta( noBaseline, nullptr, failed ) );

    // 예산 — 다 싣지 못하면 실은 것만 재구성에 반영되고, 받는 쪽 결과가 그것과 같다.
    NetSnapshot big;
    big._tick = 13;
    for ( uint32 entityId = 1; entityId <= 60; ++entityId )
    {
        big._listEntity.push_back( NetEntityState{ vector<uint8>( 20, static_cast<uint8>( entityId ) ), entityId, 1 } );
    }
    BitWriter   smallWriter;
    NetSnapshot partial;
    big.writeDelta( smallWriter, nullptr, 300, partial );
    SW_EXPECT_TRUE( smallWriter.getByteCount() <= 300 );
    SW_EXPECT_TRUE( partial._listEntity.size() > 5 && partial._listEntity.size() < 60 );
    BitReader   partialReader( smallWriter.getBytes().data(), smallWriter.getByteCount() );
    NetSnapshot partialDecoded;
    SW_ASSERT_TRUE( NetSnapshot::readDelta( partialReader, nullptr, partialDecoded ) );
    SW_EXPECT_EQUAL( static_cast<int32>( partial._listEntity.size() ), static_cast<int32>( partialDecoded._listEntity.size() ) );
}

/**
 * @brief [NetClientServerTest] 한 델타에 사라짐 · 새 엔티티 · 바뀐 엔티티가 섞여도, 그리고 재구성 · 해독 자리를 다시 써도 서버의 재구성과 클라이언트의 해독이 같다
 */
SW_TEST_CASE( NetClientServerTest, DeltaReconstructionMatchesAfterSlotReuse )
{
    NetSnapshot baseline;
    baseline._tick = 10;
    for ( uint32 entityId = 1; entityId <= 50; ++entityId )
    {
        baseline._listEntity.push_back( NetEntityState{ vector<uint8>( 8, static_cast<uint8>( entityId ) ), entityId, 1 } );
    }
    NetSnapshot current;
    current._tick = 11;
    for ( uint32 entityId = 5; entityId <= 60; ++entityId ) // 1..4 사라짐, 51..60 새것, 7 의 배수는 바뀜
    {
        current._listEntity.push_back( NetEntityState{ vector<uint8>( 8, static_cast<uint8>( entityId % 7 == 0 ? 0xEE : entityId ) ), entityId, 1 } );
    }
    // 다시 쓰는 자리 — 크기 · 내용이 다른 옛 재구성이 들어 있다.
    NetSnapshot written;
    for ( uint32 entityId = 100; entityId < 180; ++entityId )
    {
        written._listEntity.push_back( NetEntityState{ vector<uint8>( 32, 0x11 ), entityId, 9 } );
    }
    NetSnapshot decoded = written;
    BitWriter   writer;
    current.writeDelta( writer, &baseline, 1000, written );
    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_ASSERT_TRUE( NetSnapshot::readDelta( reader, &baseline, decoded ) );
    SW_ASSERT_EQUAL( size_t{ 56 }, written._listEntity.size() );
    SW_ASSERT_EQUAL( written._listEntity.size(), decoded._listEntity.size() );
    for ( size_t index = 0; index < written._listEntity.size(); ++index )
    {
        SW_EXPECT_EQUAL( current._listEntity[index]._entityId, written._listEntity[index]._entityId );
        SW_EXPECT_TRUE( current._listEntity[index]._buffer == written._listEntity[index]._buffer );
        SW_EXPECT_EQUAL( written._listEntity[index]._entityId, decoded._listEntity[index]._entityId );
        SW_EXPECT_EQUAL( written._listEntity[index]._typeId, decoded._listEntity[index]._typeId );
        SW_EXPECT_TRUE( written._listEntity[index]._buffer == decoded._listEntity[index]._buffer );
    }
    SW_EXPECT_NULL( written.findEntity( 3 ) );
    SW_EXPECT_NOT_NULL( written.findEntity( 60 ) );
}

/**
 * @brief [NetClientServerTest] 겹쳐 실려 온 입력 가운데 이미 가진 틱은 넘기고, 그 뒤의 새 틱은 제 페이로드로 받는다
 * @details 클라이언트는 서버가 확인하지 않은 입력을 오래된 것부터 메시지마다 다시 싣는다. 서버는 이미 가진 틱의 페이로드를 버퍼 없이 넘기는데,
 *          넘기는 길이가 틀리면 뒤따르는 새 틱이 남의 바이트를 입력으로 받는다.
 */
SW_TEST_CASE( NetClientServerTest, RedundantInputsSkipKnownTicksAndKeepLaterPayloads )
{
    ReplicationServer server;
    server.initialize( nullptr, ReplicationServerSettings{}, nullptr );

    // 틱 5 를 먼저 받는다. 다음 메시지는 4(새) · 5(이미 가짐) · 6(새) — 오래된 것부터.
    SW_ASSERT_TRUE( NetHandleResult::Handled == server.handleMessage( 0, makeInputMessage( 5, {
                                                                                                  { 5, 5, 5 }
    } ) ) );
    SW_ASSERT_TRUE( NetHandleResult::Handled == server.handleMessage( 0, makeInputMessage( 4, {
                                                                                                  { 4 },
                                                                                                  { 5, 5, 5 },
                                                                                                  { 6, 6 }
    } ) ) );

    vector<uint8> input;
    bool          bExact = false;
    SW_ASSERT_TRUE( server.popInput( 0, 4, input, bExact ) );
    SW_EXPECT_TRUE( bExact );
    SW_ASSERT_EQUAL( size_t{ 1 }, input.size() );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( input[0] ) );
    SW_ASSERT_TRUE( server.popInput( 0, 5, input, bExact ) );
    SW_EXPECT_EQUAL( size_t{ 3 }, input.size() );
    SW_ASSERT_TRUE( server.popInput( 0, 6, input, bExact ) );
    SW_ASSERT_EQUAL( size_t{ 2 }, input.size() );
    SW_EXPECT_EQUAL( 6, static_cast<int32>( input[1] ) );
}

SW_TEST_CASE( NetClientServerTest, ReplicationInterpolatesOverLossyLatencyAndCarriesRedundantInputs )
{
    LoopbackNetwork        network;
    NetEmulationTransport  serverTransport( network.createEndpoint( 4000 ), 11u );
    NetEmulationTransport  clientTransport( network.createEndpoint( 5000 ), 12u );
    NetEmulationConditions conditions;
    conditions._latency  = 0.05;
    conditions._jitter   = 0.01;
    conditions._lossRate = 0.1f;
    serverTransport.setDefaultConditions( conditions );
    clientTransport.setDefaultConditions( conditions );
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( &serverTransport, hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( 4000 ) ) );

    HideThreePolicy           policy;
    ReplicationServer         server;
    ReplicationClient         client;
    ReplicationClientSettings clientSettings;
    clientSettings._tickInterval = 1.0f / 30.0f;
    server.initialize( &serverHost, ReplicationServerSettings{}, &policy );
    client.initialize( &clientHost, clientSettings );

    // 30 Hz 서버 틱, 60 Hz 프레임. 엔티티 1 은 x = 틱 × 0.1 로 움직인다.
    float64       time          = 0.0;
    uint32        serverTick    = 0;
    uint32        clientTick    = 0;
    int32         exactInputs   = 0;
    int32         poppedInputs  = 0;
    float32       lastSampledX  = -1.0f;
    bool          bMonotonic    = true;
    float32       maxLagSeconds = 0.0f;
    vector<uint8> buffer;
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        time += 1.0 / 60.0;
        // 흉내 줄을 먼저 모두 비운다 — 지연이 방향과 상관없이 같다.
        serverTransport.update( time );
        clientTransport.update( time );
        serverHost.update( time );
        clientHost.update( time );
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionId, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == server.handleMessage( connectionId, buffer ) );
        }
        while ( clientHost.receiveMessage( connectionId, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, buffer ) );
        }
        if ( clientHost.getConnectionState( 0 ) != NetConnectionState::Connected )
            continue;

        if ( frame % 2 == 0 )
        {
            // 클라이언트 입력 — 틱 번호를 넣는다.
            client.sendInput( clientTick, vector<uint8>{ static_cast<uint8>( clientTick & 0xFF ) } );
            ++clientTick;
            // 서버 틱 — 입력은 지연을 두고(4 틱 뒤) 꺼낸다.
            if ( serverTick >= 4 )
            {
                vector<uint8> input;
                bool          bExact = false;
                if ( server.popInput( 0, serverTick - 4, input, bExact ) )
                {
                    ++poppedInputs;
                    exactInputs += bExact ? 1 : 0;
                    server.setLastProcessedInputTick( 0, serverTick - 4 );
                }
            }
            server.beginTick( serverTick );
            server.setEntity( 1, 1, makeFloatBytes( static_cast<float32>( serverTick ) * 0.1f ) );
            server.setEntity( 2, 1, makeFloatBytes( 5.0f ) );
            server.setEntity( 3, 2, makeFloatBytes( 9.0f ) );
            server.endTick();
            server.sendSnapshots();
            ++serverTick;
        }
        client.update( 1.0f / 60.0f );
        const NetEntityState* pFrom = nullptr;
        const NetEntityState* pTo   = nullptr;
        float32               alpha = 0.0f;
        if ( frame > 60 && client.sampleEntity( 1, pFrom, pTo, alpha ) )
        {
            const float32 fromX   = readFloatBytes( pFrom->_buffer );
            const float32 toX     = readFloatBytes( pTo->_buffer );
            const float32 x       = fromX + ( toX - fromX ) * alpha;
            bMonotonic            = bMonotonic && x >= lastSampledX - 1.0e-4f;
            lastSampledX          = x;
            const float32 serverX = static_cast<float32>( serverTick - 1 ) * 0.1f;
            maxLagSeconds         = MathUtil::max( maxLagSeconds, ( serverX - x ) / 0.1f / 30.0f );
        }
    }
    SW_EXPECT_TRUE( client.hasSnapshot() );
    SW_EXPECT_TRUE( bMonotonic );           // 손실이 있어도 되돌아가지 않는다
    SW_EXPECT_TRUE( maxLagSeconds < 0.3f ); // 지연 50 ms + 보간 100 ms + 틱 몇 개
    SW_EXPECT_TRUE( lastSampledX > 15.0f );
    SW_EXPECT_TRUE( server.getAckedTick( 0 ) + 10 > serverTick ); // 확인이 따라온다 — 델타의 기준이 최근이다
    SW_EXPECT_EQUAL( 0, static_cast<int32>( client.getDecodeFailureCount() ) );
    vector<uint32> listVisible;
    client.collectVisibleEntities( listVisible );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listVisible.size() ) ); // 3 은 정책이 숨겼다
    SW_EXPECT_TRUE( poppedInputs > 150 );
    SW_EXPECT_TRUE( static_cast<float32>( exactInputs ) / static_cast<float32>( poppedInputs ) > 0.97f ); // 손실 10 % 를 겹쳐 보내기가 메운다
    SW_EXPECT_TRUE( server.getClientViewTick( 0 ) > static_cast<float32>( serverTick ) - 15.0f );
}

SW_TEST_CASE( NetClientServerTest, PredictionReconcilesAndLagCompensationRewinds )
{
    // 예측 — 상태는 x, 입력은 이동량. 서버는 x 가 5 를 넘지 못하게 막는다(벽) — 클라이언트는 몰랐다.
    ClientPrediction<float32, float32> prediction;
    float32                            predicted = 0.0f;
    const auto                         simulate  = []( float32 x, float32 move )
    { return x + move; };
    for ( uint32 tick = 1; tick <= 10; ++tick )
    {
        predicted = simulate( predicted, 1.0f );
        prediction.record( tick, 1.0f, predicted );
    }
    SW_EXPECT_NEAR_EQUAL( 10.0f, predicted, 1.0e-5f );
    const auto isClose = []( float32 lhs, float32 rhs )
    { return MathUtil::abs( lhs - rhs ) < 1.0e-3f; };
    SW_EXPECT_FALSE( prediction.reconcile( 4, 4.0f, predicted, simulate, isClose ) ); // 맞았다
    SW_EXPECT_TRUE( prediction.reconcile( 6, 5.0f, predicted, simulate, isClose ) );  // 서버는 5 에서 멈췄다 → 남은 4 틱을 다시
    SW_EXPECT_NEAR_EQUAL( 9.0f, predicted, 1.0e-5f );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( prediction.getCorrectionCount() ) );
    SW_EXPECT_FALSE( prediction.reconcile( 7, 6.0f, predicted, simulate, isClose ) ); // 고친 기록과 맞는다

    // 랙 보정 — 목표는 틱마다 x 로 1 m 씩 간다. 10 틱 전을 보던 사수는 그 자리를 맞힌다.
    LagCompensationHistory history( 32 );
    for ( uint32 tick = 0; tick <= 30; ++tick )
    {
        history.record( tick, vector<LagRecord>{
                                  LagRecord{float3{ static_cast<float32>( tick ), 0.0f, 10.0f }, 0.5f, 7},
                                  LagRecord{                         float3{ 0.0f, 0.0f, 0.0f }, 0.5f, 1}
        } );
    }
    LagRecord sample;
    SW_ASSERT_TRUE( history.sampleAt( 20.5f, 7, sample ) );
    SW_EXPECT_NEAR_EQUAL( 20.5f, sample._position._x, 1.0e-4f );
    const float3 origin{ 20.0f, 0.0f, 0.0f };
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    float32      distance = 0.0f;
    SW_EXPECT_EQUAL( 7, static_cast<int32>( history.raycastAt( 20.0f, origin, forward, 100.0f, 1, distance ) ) );
    SW_EXPECT_NEAR_EQUAL( 9.5f, distance, 1.0e-3f );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( history.raycastAt( 30.0f, origin, forward, 100.0f, 1, distance ) ) ); // 지금은 이미 지나갔다
    SW_ASSERT_TRUE( history.sampleAt( -5.0f, 7, sample ) );                                                       // 기억 밖 — 가장 오래된 끝
    SW_EXPECT_NEAR_EQUAL( 0.0f, sample._position._x, 1.0e-4f );
}

/**
 * @brief [NetClientServerTest] 실제 전송 위에 씌운 네트워크 흉내(지연 100 ms · 흔들림 · 손실 5 % · 순서 뒤바뀜 10 % · 회선 64 KB/s)에서도 복제가 되돌아가지 않고 따라온다
 * @details 루프백 망은 완벽하게 두고 양쪽 끝점에 `NetEmulationTransport` 를 씌운다 — UDP 전송에 씌우는 것과 같은 길이다(`-gv_netEmu*`).
 */
SW_TEST_CASE( NetClientServerTest, ReplicationSurvivesEmulatedBadNetwork )
{
    LoopbackNetwork        network;
    NetEmulationTransport  serverTransport( network.createEndpoint( 4100 ), 5u );
    NetEmulationTransport  clientTransport( network.createEndpoint( 5100 ), 9u );
    NetEmulationConditions conditions;
    conditions._latency                 = 0.1;
    conditions._jitter                  = 0.02;
    conditions._lossRate                = 0.05f;
    conditions._reorderRate             = 0.1f;
    conditions._reorderDelay            = 0.03;
    conditions._bandwidthBytesPerSecond = 64 * 1024;
    serverTransport.setDefaultConditions( conditions );
    clientTransport.setDefaultConditions( conditions );
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( &serverTransport, hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( 4100 ) ) );

    ReplicationServer         server;
    ReplicationClient         client;
    ReplicationClientSettings clientSettings;
    clientSettings._tickInterval = 1.0f / 30.0f;
    server.initialize( &serverHost, ReplicationServerSettings{} );
    client.initialize( &clientHost, clientSettings );

    float64       time         = 0.0;
    uint32        serverTick   = 0;
    float32       lastSampledX = -1.0f;
    bool          bMonotonic   = true;
    vector<uint8> buffer;
    for ( int32 frame = 0; frame < 60 * 6; ++frame )
    {
        time += 1.0 / 60.0;
        serverHost.update( time );
        clientHost.update( time );
        network.deliverInFlight();
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionId, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == server.handleMessage( connectionId, buffer ) );
        }
        while ( clientHost.receiveMessage( connectionId, channel, buffer ) )
        {
            SW_EXPECT_TRUE( NetHandleResult::Handled == client.handleMessage( 0, buffer ) );
        }
        if ( frame % 2 == 0 )
        {
            server.beginTick( serverTick );
            server.setEntity( 1, 1, makeFloatBytes( static_cast<float32>( serverTick ) * 0.1f ) );
            server.endTick();
            server.sendSnapshots();
            ++serverTick;
        }
        client.update( 1.0f / 60.0f );
        const NetEntityState* pFrom = nullptr;
        const NetEntityState* pTo   = nullptr;
        float32               alpha = 0.0f;
        if ( frame > 90 && client.sampleEntity( 1, pFrom, pTo, alpha ) )
        {
            const float32 fromX = readFloatBytes( pFrom->_buffer );
            const float32 toX   = readFloatBytes( pTo->_buffer );
            const float32 x     = fromX + ( toX - fromX ) * alpha;
            bMonotonic          = bMonotonic && x >= lastSampledX - 1.0e-4f;
            lastSampledX        = x;
        }
    }
    SW_EXPECT_TRUE( clientHost.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_TRUE( client.hasSnapshot() );
    SW_EXPECT_TRUE( bMonotonic );           // 순서가 뒤바뀐 낡은 스냅샷에 되돌아가지 않는다
    SW_EXPECT_TRUE( lastSampledX > 14.0f ); // 6 초 · 30 Hz 의 끝 근처까지 따라왔다
    SW_EXPECT_EQUAL( 0, static_cast<int32>( client.getDecodeFailureCount() ) );
    const NetEmulationStats serverStats = serverTransport.getStats();
    SW_EXPECT_TRUE( serverStats._droppedCount > 0u );
    SW_EXPECT_TRUE( serverStats._reorderedCount > 0u );
}

/**
 * @brief [NetClientServerTest] `-gv_netEmu*` 전역 변수가 네트워크 흉내 조건이 된다(언리얼 PktLag · PktLoss 의 자리) — 모두 0 이면 꺼짐
 */
SW_TEST_CASE( NetClientServerTest, EmulationConditionsComeFromGlobalVariables )
{
#if defined( SW_SHIPPING )
    SW_TEST_SKIP( "Test global variables (-gv_netEmu*) are not registered in Shipping" );
#endif
    GlobalVariableManager& variables = engine::getGlobalVariableManager();
    GlobalVariableInfo*    pLatency  = variables.findVariable( "gv_netEmuLatencyMs" );
    GlobalVariableInfo*    pLoss     = variables.findVariable( "gv_netEmuLossPercent" );
    GlobalVariableInfo*    pReorder  = variables.findVariable( "gv_netEmuReorderPercent" );
    GlobalVariableInfo*    pBand     = variables.findVariable( "gv_netEmuBandwidthKilobytesPerSecond" );
    SW_ASSERT_NOT_NULL( pLatency );
    SW_ASSERT_NOT_NULL( pLoss );
    SW_ASSERT_NOT_NULL( pReorder );
    SW_ASSERT_NOT_NULL( pBand );
    SW_EXPECT_FALSE( NetEmulationConditions::makeFromGlobalVariables().isActive() );

    SW_ASSERT_TRUE( pLatency->setValueAsInt( 120 ) );
    SW_ASSERT_TRUE( pLoss->setValueAsInt( 5 ) );
    SW_ASSERT_TRUE( pReorder->setValueAsInt( 250 ) ); // 100 % 로 묶인다
    SW_ASSERT_TRUE( pBand->setValueAsInt( 64 ) );
    const NetEmulationConditions conditions = NetEmulationConditions::makeFromGlobalVariables();
    (void)pLatency->setValueAsInt( 0 );
    (void)pLoss->setValueAsInt( 0 );
    (void)pReorder->setValueAsInt( 0 );
    (void)pBand->setValueAsInt( 0 );
    SW_EXPECT_NEAR_EQUAL( 0.12, conditions._latency, 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( 0.05f, conditions._lossRate, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, conditions._reorderRate, 1.0e-6f );
    SW_EXPECT_EQUAL( 64 * 1024, conditions._bandwidthBytesPerSecond );
    SW_EXPECT_TRUE( conditions.isActive() );
}

/**
 * @brief [NetClientServerTest] 같은 자리에 새로 온 클라이언트는 옛 연결의 상태를 이어 쓰지 않는다 — 끊김 · 연결이 게임 프레임 사이에 일어나도
 *        라우터가 닫힘 → 열림을 그 연결의 메시지보다 먼저 키트에 알린다
 * @details 연결 id 는 자리 번호라 다시 쓰인다. 사건과 메시지를 따로 꺼내고 게임이 손으로 `onDisconnected` 를 넘기던 때는, 새 클라이언트의 틱 0 입력이
 *          옛 클라이언트가 쓴 틱(100)보다 작아 "이미 씀" 으로 버려졌다.
 */
SW_TEST_CASE( NetClientServerTest, ReconnectOnSameSlotResetsHandlerState )
{
    NetHostSettings settings;
    settings._maxConnections = 1;
    LoopbackNetwork network;
    NetHost         serverHost;
    NetHost         first;
    NetHost         second;
    serverHost.initialize( network.createEndpoint( 4000 ), settings );
    first.initialize( network.createEndpoint( 5001 ), settings );
    second.initialize( network.createEndpoint( 5002 ), settings );
    SW_ASSERT_TRUE( serverHost.listen() );
    ReplicationServer server;
    server.initialize( &serverHost, ReplicationServerSettings{}, nullptr );
    NetMessageRouter router;
    router.addHandler( &server );
    float64    time   = 0.0;
    const auto runAll = [&]( float64 seconds )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 60.0 )
        {
            time += 1.0 / 60.0;
            serverHost.update( time );
            first.update( time );
            second.update( time );
        }
    };
    const auto sendInput = []( NetHost& host, uint32 tick )
    {
        const vector<uint8> message = makeInputMessage( tick, { vector<uint8>{ static_cast<uint8>( tick + 1 ) } } );
        return host.sendMessage( 0, NetChannelType::ReliableOrdered, message );
    };

    // 첫 클라이언트가 자리 0 에서 틱 100 까지 썼다.
    SW_ASSERT_TRUE( first.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.3 );
    SW_ASSERT_TRUE( sendInput( first, 100 ) );
    runAll( 0.2 );
    (void)router.pump( serverHost );
    vector<uint8> input;
    bool          bExact = false;
    SW_ASSERT_TRUE( server.popInput( 0, 100, input, bExact ) && bExact );
    server.setLastProcessedInputTick( 0, 100 );

    // 게임 프레임 사이 — 첫째가 떠나고 둘째가 같은 자리에 들어와 틱 0 입력을 보낸다. 서버 게임은 그동안 라우터를 돌리지 않았다.
    first.disconnect( 0 );
    runAll( 0.1 );
    SW_ASSERT_TRUE( second.connect( NetAddress::makeLoopback( 4000 ) ) );
    runAll( 0.3 );
    SW_ASSERT_TRUE( second.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_ASSERT_EQUAL( 0, second.getClientIndex() );
    SW_ASSERT_TRUE( sendInput( second, 0 ) );
    runAll( 0.2 );

    vector<NetHostEvent> listEvent;
    (void)router.pump( serverHost, nullptr, &listEvent );
    SW_ASSERT_EQUAL( size_t{ 2 }, listEvent.size() ); // 닫힘 → 열림, 그 뒤에 둘째의 입력
    SW_EXPECT_TRUE( listEvent[0]._kind == NetHostEvent::Kind::Disconnected );
    SW_EXPECT_TRUE( listEvent[1]._kind == NetHostEvent::Kind::Connected );
    SW_ASSERT_TRUE( server.popInput( 0, 0, input, bExact ) );
    SW_EXPECT_TRUE( bExact );
    SW_ASSERT_EQUAL( size_t{ 1 }, input.size() );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( input[0] ) );
}

/**
 * @brief [NetClientServerTest] 복제 서버 · 클라이언트는 종류 마스크로 자기 종류만 맡는다 — 같은 영역의 다른 종류는 라우터가 그 종류를 맡은 처리기에 준다
 * @details 영역 전체를 받아 버리면 라우터 등록 순서에 따라 같은 영역을 나눠 쓰는 처리기가 메시지를 하나도 받지 못한다.
 */
SW_TEST_CASE( NetClientServerTest, KindMaskRoutesSharedRange )
{
    ReplicationServer server;
    server.initialize( nullptr, ReplicationServerSettings{}, nullptr );
    ReplicationClient client;
    client.initialize( nullptr, ReplicationClientSettings{} );
    const uint8         otherKind = NetKitMessageRange::kClientServer + 7;
    const vector<uint8> message{ otherKind, 1 };
    SW_EXPECT_TRUE( NetHandleResult::NotMine == server.handleMessage( 0, message ) );
    SW_EXPECT_TRUE( NetHandleResult::NotMine == client.handleMessage( 0, message ) );

    KindCounter      serverSide{ otherKind };
    KindCounter      clientSide{ otherKind };
    NetMessageRouter serverRouter;
    NetMessageRouter clientRouter;
    SW_ASSERT_TRUE( serverRouter.addHandler( &server ) ); // 키트 처리기와 게임 처리기는 서로 다른 종류를 맡는다(겹치면 라우터가 받지 않는다)
    SW_ASSERT_TRUE( serverRouter.addHandler( &serverSide ) );
    SW_ASSERT_TRUE( clientRouter.addHandler( &client ) );
    SW_ASSERT_TRUE( clientRouter.addHandler( &clientSide ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == serverRouter.dispatch( NetMessageContext{}, message.data(), static_cast<int32>( message.size() ) ) );
    SW_EXPECT_TRUE( NetHandleResult::Handled == clientRouter.dispatch( NetMessageContext{}, message.data(), static_cast<int32>( message.size() ) ) );
    SW_EXPECT_EQUAL( 1, serverSide._count );
    SW_EXPECT_EQUAL( 1, clientSide._count );
}

/**
 * @brief [NetClientServerTest] 사라진 엔티티가 많아도 스냅숏은 메시지 상한 안이고, 못 실은 사라짐은 재구성에 남아 다음 델타가 마저 싣는다.
 *        상한(255 B)을 넘는 엔티티는 싣지 않아 두 쪽 기준이 같다
 * @details 예산이 사라진 목록 · 종류 바이트를 세지 않고 id 를 3 바이트로 어림하던 때는 3 바이트 id 400 개가 사라지면 1200 B 를 넘었다(보내기에서 버려진다).
 *          255 B 를 넘는 엔티티는 255 로 잘려 실리는데 서버는 자르지 않은 원본을 기준으로 기억해, 다음 델타부터 두 쪽 기준이 어긋났다.
 */
SW_TEST_CASE( NetClientServerTest, SnapshotStaysUnderMessageLimitWithManyRemovals )
{
    NetSnapshot baseline;
    baseline._tick = 5;
    for ( uint32 index = 0; index < 400; ++index )
    {
        baseline._listEntity.push_back( NetEntityState{ vector<uint8>( 4, 1 ), 1000000 + index, 1 } );
    }
    NetSnapshot current;
    current._tick = 6;
    current._listEntity.push_back( NetEntityState{ vector<uint8>( 300, 7 ), 3, 1 } ); // 상한을 넘는다
    current._listEntity.push_back( NetEntityState{ vector<uint8>( 8, 9 ), 4, 1 } );
    current.sortEntities();

    // 예산을 상한보다 크게 줘도 메시지(종류 바이트 포함)는 상한 안이다.
    NetSnapshot clientState = baseline;
    NetSnapshot serverState = baseline;
    int32       rounds      = 0;
    for ( ; rounds < 10 && clientState._listEntity.size() != 1; ++rounds )
    {
        NetMessageWriter messageWriter;
        BitWriter&       writer = messageWriter.begin( NetClientServerMessage::kSnapshot );
        NetSnapshot      written;
        current.writeDelta( writer, &serverState, 5000, written );
        SW_ASSERT_TRUE( messageWriter.getByteCount() <= NetConnection::kMaxSingleMessageSize );
        BitReader reader( messageWriter.getBytes().data(), messageWriter.getByteCount() );
        (void)reader.readBits( 8 ); // 앞의 종류 바이트를 건너뛴다 — 값은 쓰지 않는다
        NetSnapshot decoded;
        SW_ASSERT_TRUE( NetSnapshot::readDelta( reader, &clientState, decoded ) );
        // 서버가 기억하는 재구성과 클라이언트가 푼 것이 엔티티 하나까지 같다.
        SW_ASSERT_EQUAL( written._listEntity.size(), decoded._listEntity.size() );
        for ( size_t index = 0; index < written._listEntity.size(); ++index )
        {
            SW_EXPECT_EQUAL( written._listEntity[index]._entityId, decoded._listEntity[index]._entityId );
            SW_EXPECT_TRUE( written._listEntity[index]._buffer == decoded._listEntity[index]._buffer );
        }
        clientState = decoded;
        serverState = written;
        ++current._tick;
    }
    SW_EXPECT_TRUE( rounds >= 2 ); // 한 메시지에 다 들어가지 않는다
    SW_ASSERT_EQUAL( size_t{ 1 }, clientState._listEntity.size() );
    SW_EXPECT_EQUAL( 4u, clientState._listEntity[0]._entityId );
    SW_EXPECT_NULL( clientState.findEntity( 3 ) ); // 넘는 엔티티는 싣지 않았다
}

/**
 * @brief [NetClientServerTest] 상한을 넘는 입력은 보내는 쪽이 거절하고, 서버는 상한을 넘는 길이를 잘라 읽지 않고 깨짐으로 보며 그 메시지의 입력은 하나도 넣지 않는다.
 *        큰 입력은 메시지 상한 안에서 오래된 것부터 실리고 서버 확인이 오르면 다음 메시지가 잇는다
 * @details 클라이언트는 전체 길이를 쓰는데 서버가 길이를 255 로 잘라 읽어, 남은 바이트가 다음 항목의 길이로 읽혀 옛 틱에 쓰레기 입력이 들어갔다.
 *          항목마다 읽으며 넣던 때는 깨진 항목 앞의 입력이 이미 들어가 있었다.
 */
SW_TEST_CASE( NetClientServerTest, OversizedInputIsRejectedAtTheSender )
{
    ReplicationClient offline;
    offline.initialize( nullptr, ReplicationClientSettings{} );
    SW_EXPECT_FALSE( offline.sendInput( 0, vector<uint8>( NetClientServerMessage::kMaxInputBytes + 1, 1 ) ) );
    SW_EXPECT_TRUE( offline.sendInput( 0, vector<uint8>( NetClientServerMessage::kMaxInputBytes, 1 ) ) );

    // 손으로 지은 메시지 — 틱 4 는 1 B, 틱 5 는 300 B(상한 넘음).
    ReplicationServer server;
    server.initialize( nullptr, ReplicationServerSettings{}, nullptr );
    SW_EXPECT_TRUE( NetHandleResult::Malformed == server.handleMessage( 0, makeInputMessage( 4, { vector<uint8>{ 9 }, vector<uint8>( 300, 7 ) } ) ) );
    vector<uint8> input;
    bool          bExact = false;
    SW_EXPECT_FALSE( server.popInput( 0, 4, input, bExact ) ); // 깨진 항목 앞의 성한 입력도 들어가지 않았다

    // 입력 200 B — 메시지 하나에 다섯 개쯤. 서버가 스냅숏에 실어 준 확인을 따라 오래된 것부터 이어 실어 40 틱이 모두 간다.
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    LoopbackNetwork network;
    NetHost         serverHost;
    NetHost         clientHost;
    serverHost.initialize( network.createEndpoint( 4000 ), hostSettings );
    clientHost.initialize( network.createEndpoint( 5000 ), hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( 4000 ) ) );
    ReplicationServer liveServer;
    ReplicationClient liveClient;
    liveServer.initialize( &serverHost, ReplicationServerSettings{}, nullptr );
    liveClient.initialize( &clientHost, ReplicationClientSettings{} );
    NetMessageRouter serverRouter;
    NetMessageRouter clientRouter;
    serverRouter.addHandler( &liveServer );
    clientRouter.addHandler( &liveClient );
    float64 time = 0.0;
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        time += 1.0 / 60.0;
        serverHost.update( time );
        clientHost.update( time );
    }
    SW_ASSERT_TRUE( clientHost.getConnectionState( 0 ) == NetConnectionState::Connected );
    for ( uint32 tick = 0; tick < 40; ++tick )
    {
        SW_EXPECT_TRUE( liveClient.sendInput( tick, vector<uint8>( 200, static_cast<uint8>( tick ) ) ) );
        liveServer.beginTick( tick );
        liveServer.endTick();
        liveServer.sendSnapshots(); // 스냅숏이 입력 확인을 나른다
        time += 1.0 / 60.0;
        clientHost.update( time );
        serverHost.update( time );
        (void)serverRouter.pump( serverHost );
        (void)clientRouter.pump( clientHost );
    }
    for ( int32 frame = 0; frame < 30; ++frame ) // 보내기 간격이 틱과 어긋나 밀린 패킷까지
    {
        time += 1.0 / 60.0;
        clientHost.update( time );
        serverHost.update( time );
        (void)serverRouter.pump( serverHost );
        (void)clientRouter.pump( clientHost );
    }
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( serverRouter.getMalformedCount() ) );
    int32 exactCount = 0;
    for ( uint32 tick = 0; tick < 39; ++tick )
    {
        if ( liveServer.popInput( 0, tick, input, bExact ) && bExact && input.size() == 200 && input[0] == static_cast<uint8>( tick ) )
            ++exactCount;
    }
    SW_EXPECT_EQUAL( 39, exactCount );
}

/**
 * @brief [NetClientServerTest] 클라이언트 → 서버가 12 틱 동안 모두 사라져도 서버가 꺼내는 입력에 빈틈이 없다 — 보내는 쪽은 서버가 확인한 다음 틱부터 싣는다
 * @details "최근 N 개(4)" 만 겹쳐 보내던 때는 끊김이 끝난 첫 메시지가 마지막 네 틱만 실어, 그 앞 여덟 틱을 서버가 지난 입력 되풀이로 처리했다(꺼내기는
 *          24 틱 뒤라 다시 왔으면 늦지 않았다). 확인이 따라오면 다시 싣는 입력은 몇 개뿐이다(확인이 없으면 32 개까지 자란다).
 */
SW_TEST_CASE( NetClientServerTest, InputBurstLossLeavesNoGap )
{
    LoopbackNetwork        network;
    NetEmulationTransport  clientTransport( network.createEndpoint( 5200 ), 3u );
    InputBurstDropper      dropper;
    NetEmulationConditions conditions;
    conditions._pDropFilter        = &dropInputBurst;
    conditions._pDropFilterContext = &dropper;
    clientTransport.setDefaultConditions( conditions );
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( network.createEndpoint( 4200 ), hostSettings );
    clientHost.initialize( &clientTransport, hostSettings );
    SW_ASSERT_TRUE( serverHost.listen() );
    SW_ASSERT_TRUE( clientHost.connect( NetAddress::makeLoopback( 4200 ) ) );

    ReplicationServer server;
    ReplicationClient client;
    server.initialize( &serverHost, ReplicationServerSettings{} );
    client.initialize( &clientHost, ReplicationClientSettings{} );
    NetMessageRouter serverRouter;
    NetMessageRouter clientRouter;
    serverRouter.addHandler( &server );
    clientRouter.addHandler( &client );

    // 30 Hz 틱 · 60 Hz 프레임. 서버는 입력을 24 틱 늦게 꺼낸다 — 끊김이 끝난 뒤 다시 온 입력이 꺼내기 전에 닿는다.
    const uint32  popDelay     = 24;
    const uint32  burstBegin   = 40;
    const uint32  burstEnd     = 52;
    float64       time         = 0.0;
    uint32        tick         = 0;
    int32         checkedCount = 0;
    int32         exactCount   = 0;
    vector<uint8> input;
    for ( int32 frame = 0; frame < 60 * 4; ++frame )
    {
        time += 1.0 / 60.0;
        // 틱 t 의 입력은 다음 프레임 update 에서 나간다(그때 tick = t + 1) — 틱 [40, 52) 의 입력을 실은 패킷이 모두 사라진다.
        dropper._bDropping = burstBegin < tick && tick <= burstEnd;
        serverHost.update( time );
        clientHost.update( time );
        network.deliverInFlight();
        (void)serverRouter.pump( serverHost );
        (void)clientRouter.pump( clientHost );
        const bool bTickFrame = frame % 2 == 0 && clientHost.getConnectionState( 0 ) == NetConnectionState::Connected;
        if ( bTickFrame == false )
            continue;
        SW_EXPECT_TRUE( client.sendInput( tick, vector<uint8>{ static_cast<uint8>( tick & 0xFF ), 7 } ) );
        if ( tick >= popDelay )
        {
            const uint32 popTick = tick - popDelay;
            bool         bExact  = false;
            if ( server.popInput( 0, popTick, input, bExact ) )
            {
                ++checkedCount;
                exactCount += bExact && input.size() == 2 && input[0] == static_cast<uint8>( popTick & 0xFF ) ? 1 : 0;
            }
            server.setLastProcessedInputTick( 0, popTick );
        }
        server.beginTick( tick );
        server.setEntity( 1, 1, makeFloatBytes( static_cast<float32>( tick ) ) );
        server.endTick();
        server.sendSnapshots(); // 스냅숏이 입력 확인을 나른다
        ++tick;
    }
    SW_EXPECT_TRUE( clientHost.getConnectionState( 0 ) == NetConnectionState::Connected );
    SW_EXPECT_TRUE( dropper._droppedCount >= 10 );
    SW_EXPECT_TRUE( checkedCount > 60 );
    SW_EXPECT_EQUAL( checkedCount, exactCount );          // 끊김 앞뒤 모두 그 틱의 입력이다
    SW_EXPECT_TRUE( client.getPendingInputCount() <= 6 ); // 확인이 따라와 다시 싣는 것은 몇 개뿐
}

/**
 * @brief [NetClientServerTest] 스냅숏이 1 초 끊겼다가 다시 와도 클라이언트 렌더 틱은 되돌아가지 않고 받은 가장 새 틱을 넘지 않는다
 * @details 손으로 만든 스냅숏을 손 배달한다(호스트 없음). 끊긴 동안 렌더 틱은 마지막으로 받은 틱에서 멈추고, 다시 오면 앞으로만 간다.
 */
SW_TEST_CASE( NetClientServerTest, RenderTickNeverGoesBackAcrossASnapshotGap )
{
    ReplicationClient         client;
    ReplicationClientSettings clientSettings;
    clientSettings._tickInterval = 1.0f / 30.0f;
    client.initialize( nullptr, clientSettings );
    float32    lastRenderTick = -1.0e9f; // 첫 스냅숏의 렌더 틱(0 − 지연 = −3)은 받기 전 값보다 작다 — 받은 뒤부터 본다
    bool       bMonotonic     = true;
    bool       bBehindNewest  = true;
    const auto deliver        = [&client]( uint32 tick )
    {
        BitWriter writer;
        writer.writeBits( NetClientServerMessage::kSnapshot, 8 );
        NetSnapshot snapshot;
        snapshot._tick = tick;
        NetSnapshot written;
        snapshot.writeDelta( writer, nullptr, NetConnection::kMaxSingleMessageSize, written );
        return client.handleMessage( 0, writer.getBytes() );
    };
    for ( uint32 frame = 0; frame < 240; ++frame )
    {
        const bool bGap = frame >= 60 && frame < 120; // 1 초 동안 아무것도 오지 않는다
        if ( frame % 2 == 0 && bGap == false )
            SW_EXPECT_TRUE( NetHandleResult::Handled == deliver( frame / 2 ) );
        client.update( 1.0f / 60.0f );
        const float32 renderTick = client.getRenderTick();
        bMonotonic               = bMonotonic && renderTick >= lastRenderTick - 1.0e-4f;
        bBehindNewest            = bBehindNewest && ( client.hasSnapshot() == false || renderTick <= static_cast<float32>( client.getLatest()->_tick ) + 1.0e-4f );
        lastRenderTick           = renderTick;
    }
    SW_EXPECT_TRUE( bMonotonic );
    SW_EXPECT_TRUE( bBehindNewest );
    SW_EXPECT_TRUE( lastRenderTick > 110.0f ); // 끝(받은 틱 119) 근처까지 따라왔다 — 지연 3 틱
}
