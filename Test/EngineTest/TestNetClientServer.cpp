#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetEmulation.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetMessage.h"
#include "Core/Network/NetTransport.h"

#include "Engine/Common/EngineServices.h"

#include "GameFramework/Kits/Network/NetClientServer/ClientPrediction.h"
#include "GameFramework/Kits/Network/NetClientServer/LagCompensation.h"
#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"
#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"

#include "TestFramework/TestFramework.h"

// 권위 서버 네트워크 키트 — 스냅샷 델타(바뀐 것 · 사라진 것 · 예산 넘침), 지연 · 손실 망 위의 복제와 보간, 입력 중복 전송,
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

        uint8 getMessageRangeBase() const override { return NetKitMessageRange::kClientServer; }
        bool  handleNetMessage( int32 connectionId, const uint8* pData, int32 size ) override
        {
            (void)connectionId;
            if ( size <= 0 || pData[0] != _kind )
                return false;
            ++_count;
            return true;
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
} // namespace

SW_TEST_CASE( NetClientServerTest, SnapshotDeltasCarryChangesRemovalsAndRespectBudgets )
{
    NetSnapshot baseline;
    baseline._tick = 10;
    for ( uint32 entityId = 1; entityId <= 4; ++entityId )
        baseline._listEntity.push_back( NetEntityState{ makeFloatBytes( static_cast<float32>( entityId ) ), entityId, 7 } );
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
        big._listEntity.push_back( NetEntityState{ vector<uint8>( 20, static_cast<uint8>( entityId ) ), entityId, 1 } );
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
 * @brief [NetClientServerTest] 겹쳐 실려 온 입력 가운데 이미 가진 틱은 넘기고, 그 뒤의 새 틱은 제 페이로드로 받는다
 * @details 클라이언트는 최근 입력을 새 것부터 여러 개 겹쳐 보낸다. 서버는 이미 가진 틱의 페이로드를 버퍼 없이 넘기는데, 넘기는 길이가 틀리면
 *          뒤따르는 새 틱이 남의 바이트를 입력으로 받는다.
 */
SW_TEST_CASE( NetClientServerTest, RedundantInputsSkipKnownTicksAndKeepLaterPayloads )
{
    ReplicationServer server;
    server.initialize( nullptr, ReplicationServerSettings{}, nullptr );
    const auto makeInputMessage = []( uint32 latestTick, const vector<vector<uint8>>& listPayload )
    {
        BitWriter writer;
        writer.writeBits( NetClientServerMessage::kInput, 8 );
        writer.writeVarUint( latestTick );
        writer.writeVarUint( 0 );
        writer.writeVarUint( listPayload.size() );
        for ( const vector<uint8>& payload : listPayload )
        {
            writer.writeVarUint( payload.size() );
            writer.writeBytes( payload.data(), static_cast<int32>( payload.size() ) );
        }
        return writer.getBytes();
    };

    // 틱 5 를 먼저 받는다. 다음 패킷은 6(새) · 5(이미 가짐) · 4(새) — 새 것부터.
    SW_ASSERT_TRUE( server.handleMessage( 0, makeInputMessage( 5, {
                                                                      { 5, 5, 5 }
    } ) ) );
    SW_ASSERT_TRUE( server.handleMessage( 0, makeInputMessage( 6, {
                                                                      { 6, 6 },
                                                                      { 5, 5, 5 },
                                                                      { 4 }
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
    LoopbackNetwork    network( 11u );
    LoopbackConditions conditions;
    conditions._latency  = 0.05;
    conditions._jitter   = 0.01;
    conditions._lossRate = 0.1f;
    network.setConditions( conditions );
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0 / 60.0;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( network.createEndpoint( 4000 ), hostSettings );
    clientHost.initialize( network.createEndpoint( 5000 ), hostSettings );
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
        serverHost.update( time );
        clientHost.update( time );
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionId, channel, buffer ) )
            SW_EXPECT_TRUE( server.handleMessage( connectionId, buffer ) );
        while ( clientHost.receiveMessage( connectionId, channel, buffer ) )
            SW_EXPECT_TRUE( client.handleMessage( buffer ) );
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
        history.record( tick, vector<LagRecord>{
                                  LagRecord{float3{ static_cast<float32>( tick ), 0.0f, 10.0f }, 0.5f, 7},
                                  LagRecord{                         float3{ 0.0f, 0.0f, 0.0f }, 0.5f, 1}
        } );
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
    LoopbackNetwork        network( 21u );
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
        network.advance( time );
        int32          connectionId = -1;
        NetChannelType channel      = NetChannelType::Unreliable;
        while ( serverHost.receiveMessage( connectionId, channel, buffer ) )
            SW_EXPECT_TRUE( server.handleMessage( connectionId, buffer ) );
        while ( clientHost.receiveMessage( connectionId, channel, buffer ) )
            SW_EXPECT_TRUE( client.handleMessage( buffer ) );
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
 * @brief [NetClientServerTest] 복제 서버 · 클라이언트는 키트 영역 안에서도 자기 종류만 먹는다 — 같은 영역의 다른 종류는 라우터가 다음 처리기에 준다
 * @details 영역 전체를 받아 버리면 라우터 등록 순서에 따라 같은 영역을 나눠 쓰는 처리기가 메시지를 하나도 받지 못한다.
 */
SW_TEST_CASE( NetClientServerTest, KitHandlersPassOnKindsOfTheRangeTheyDoNotOwn )
{
    ReplicationServer server;
    server.initialize( nullptr, ReplicationServerSettings{}, nullptr );
    ReplicationClient client;
    client.initialize( nullptr, ReplicationClientSettings{} );
    const uint8         otherKind = NetKitMessageRange::kClientServer + 7;
    const vector<uint8> message{ otherKind, 1 };
    SW_EXPECT_FALSE( server.handleMessage( 0, message ) );
    SW_EXPECT_FALSE( client.handleMessage( message ) );

    KindCounter      serverSide{ otherKind };
    KindCounter      clientSide{ otherKind };
    NetMessageRouter serverRouter;
    NetMessageRouter clientRouter;
    serverRouter.addHandler( &server ); // 먼저 등록된 쪽이 넘겨야 뒤가 받는다
    serverRouter.addHandler( &serverSide );
    clientRouter.addHandler( &client );
    clientRouter.addHandler( &clientSide );
    SW_EXPECT_TRUE( serverRouter.dispatch( 0, message.data(), static_cast<int32>( message.size() ) ) );
    SW_EXPECT_TRUE( clientRouter.dispatch( -1, message.data(), static_cast<int32>( message.size() ) ) );
    SW_EXPECT_EQUAL( 1, serverSide._count );
    SW_EXPECT_EQUAL( 1, clientSide._count );
}
