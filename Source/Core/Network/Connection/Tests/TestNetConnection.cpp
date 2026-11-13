#include "pch.h"

#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/SequenceBuffer.h"
#include "Core/Network/Tests/NetTestUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

// 연결 하나의 신뢰성 계층 — 시퀀스 감김, 재전송 · 순서 · 중복 · 옛것 버리기 · RTT, 전달 통지, 순서만 채널의 종류별 흐름, 받은 버퍼 다시 쓰기,
// 받은 큐 넘침, 깨진 몸은 확인하지 않음, 대역폭 통계, 빠른 재전송, 크기 상한과 신뢰 창, 큰 신뢰 메시지와 순서만 메시지 번갈아 싣기, 신뢰 순서 없음, 비신뢰 나이 상한.

using namespace sw;
using test::NetTestUtil;

SW_TEST_CASE( NetConnectionTest, SequencesWrapAndSequenceBufferForgetsStaleEntries )
{
    SW_EXPECT_TRUE( NetSequence::isGreater( 1, 65535 ) );
    SW_EXPECT_TRUE( NetSequence::isLess( 65530, 3 ) );
    SW_EXPECT_FALSE( NetSequence::isGreater( 100, 200 ) );
    SW_EXPECT_EQUAL( 4, NetSequence::computeDifference( 2, 65534 ) );
    SW_EXPECT_EQUAL( -4, NetSequence::computeDifference( 65534, 2 ) );

    SequenceBuffer<int32, 8> buffer;
    *buffer.insert( 65534 ) = 1;
    *buffer.insert( 3 )     = 2; // 감겨서 앞섰다 — 사이(65535 · 0 · 1 · 2)는 비운다
    SW_EXPECT_TRUE( buffer.find( 65534 ) != nullptr && *buffer.find( 65534 ) == 1 );
    SW_EXPECT_TRUE( buffer.exists( 0 ) == false );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( buffer.getNewest() ) );
    SW_EXPECT_NULL( buffer.insert( 65000 ) ); // 너무 낡았다
    *buffer.insert( 20 ) = 3;
    SW_EXPECT_FALSE( buffer.exists( 65534 ) ); // 크기만큼 지나 덮였다

    // 감김 직전 크기만큼의 시퀀스와 직후 시퀀스가 서로 다른 칸에 남는다(크기가 65536 의 약수라 자리가 이어진다).
    SequenceBuffer<int32, 8> wrapping;
    for ( uint32 step = 0; step < 8; ++step )
    {
        *wrapping.insert( static_cast<uint16>( 65532 + step ) ) = static_cast<int32>( step );
    }
    for ( uint32 step = 0; step < 8; ++step )
    {
        const int32* pValue = wrapping.find( static_cast<uint16>( 65532 + step ) );
        SW_REQUIRE_TRUE( pValue != nullptr );
        SW_EXPECT_EQUAL( static_cast<int32>( step ), *pValue );
    }
    SW_EXPECT_EQUAL( 8, ( SequenceBuffer<int32, 8>::getSize() ) );
}

SW_TEST_CASE( NetConnectionTest, ConnectionsResendReliableMessagesInOrderAndDropStaleSequenced )
{
    NetConnection sender;
    NetConnection receiver;
    for ( int32 index = 0; index < 40; ++index )
    {
        const vector<uint8> buffer = NetTestUtil::makeMessage( index, 40 );
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::ReliableOrdered, buffer.data(), static_cast<int32>( buffer.size() ) ) );
    }
    {
        SW_EXPECT_LOG( "Net.MessageTooLarge" ); // 신뢰 상한을 넘는 메시지는 오류와 함께 거절한다
        const vector<uint8> tooBig( static_cast<size_t>( NetConnection::kMaxReliableMessageSize + 1 ), 0 );
        SW_EXPECT_FALSE( sender.sendMessage( NetChannelType::ReliableOrdered, tooBig.data(), static_cast<int32>( tooBig.size() ) ) );
    }

    // 세 패킷 중 하나를 잃고, 받는 쪽은 매번 확인을 돌려준다.
    vector<int32> listReceived;
    float64       time = 0.0;
    for ( int32 round = 0; round < 200 && static_cast<int32>( listReceived.size() ) < 40; ++round )
    {
        time += 0.05;
        BitWriter writer;
        sender.writePacket( time, writer, 300 );
        if ( round % 3 != 1 )
        {
            BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            SW_EXPECT_TRUE( receiver.readPacket( time + 0.02, reader ) );
            // 같은 패킷이 두 번 오면 두 번째는 버린다.
            BitReader duplicate( writer.getBytes().data(), writer.getByteCount() );
            SW_EXPECT_FALSE( receiver.readPacket( time + 0.03, duplicate ) );
        }
        BitWriter ackWriter;
        receiver.writePacket( time + 0.02, ackWriter, 300 );
        BitReader ackReader( ackWriter.getBytes().data(), ackWriter.getByteCount() );
        (void)sender.readPacket( time + 0.04, ackReader ); // ack 만 받으면 된다 — 결과는 아래 수신 메시지로 확인한다
        vector<uint8> buffer;
        while ( receiver.receiveMessage( NetChannelType::ReliableOrdered, buffer ) )
        {
            listReceived.push_back( NetTestUtil::readMessageValue( buffer ) );
        }
    }
    SW_REQUIRE_TRUE( listReceived.size() == 40 );
    for ( int32 index = 0; index < 40; ++index )
    {
        SW_EXPECT_EQUAL( index, listReceived[static_cast<size_t>( index )] );
    }
    SW_EXPECT_TRUE( sender.getStats()._resentMessageCount > 0 );
    SW_EXPECT_EQUAL( 0, sender.getPendingReliableCount() );
    SW_EXPECT_NEAR( 0.04f, sender.getStats()._rtt, 0.01f );

    // 순서만 — 같은 종류(첫 바이트 0)는 한 패킷에 가장 새 것 하나, 늦게 온 옛 패킷은 버린다.
    NetConnection fresh;
    NetConnection late;
    for ( int32 index = 0; index < 3; ++index )
    {
        const vector<uint8> buffer = NetTestUtil::makeMessage( index << 8 );
        SW_REQUIRE_TRUE( fresh.sendMessage( NetChannelType::UnreliableSequenced, buffer.data(), 4 ) );
    }
    BitWriter firstPacket;
    fresh.writePacket( 0.0, firstPacket, 300 );
    const vector<uint8> newer = NetTestUtil::makeMessage( 9 << 8 );
    SW_REQUIRE_TRUE( fresh.sendMessage( NetChannelType::UnreliableSequenced, newer.data(), 4 ) );
    BitWriter secondPacket;
    fresh.writePacket( 0.1, secondPacket, 300 );
    BitReader secondReader( secondPacket.getBytes().data(), secondPacket.getByteCount() );
    BitReader firstReader( firstPacket.getBytes().data(), firstPacket.getByteCount() );
    SW_EXPECT_TRUE( late.readPacket( 0.2, secondReader ) );
    SW_EXPECT_TRUE( late.readPacket( 0.3, firstReader ) ); // 패킷은 받지만 그 안의 옛 순서 메시지는 버린다
    vector<uint8> buffer;
    SW_REQUIRE_TRUE( late.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) );
    SW_EXPECT_EQUAL( 9 << 8, NetTestUtil::readMessageValue( buffer ) );
    SW_EXPECT_FALSE( late.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) );
}

/**
 * @brief [NetConnectionTest] 전달 통지를 단 비신뢰 메시지는 실린 패킷의 판정을 메시지마다 한 번 돌려준다 — 확인되면 Delivered, 빠른 재전송 간격으로 잃으면 Lost,
 *        싣지 못하고 줄에서 나이 상한을 넘기면 Lost 다
 * @details 키트가 "받았다" 확인 메시지를 따로 주고받지 않게 하는 창구다(MMO 갱신 확인을 이것으로 바꿨다). 판정이 없는 메시지가 남으면 기다리는 쪽이 영영 모른다.
 */
SW_TEST_CASE( NetConnectionTest, NotifiedMessagesReportDeliveredOrLostExactlyOnce )
{
    NetConnection   sender;
    NetConnection   receiver;
    constexpr uint8 kKind = 0x42;
    float64         time  = 0.0;
    for ( uint32 round = 0; round < 8; ++round )
    {
        time += 0.05;
        const uint8 arrMessage[3] = { kKind, static_cast<uint8>( round ), 7 };
        SW_REQUIRE_TRUE( sender.sendNotifiedMessage( arrMessage, 3, 100 + round ) );
        BitWriter writer;
        sender.writePacket( time, writer, 300 );
        if ( round != 1 ) // 둘째 패킷(값 101)만 잃는다
        {
            BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            SW_EXPECT_TRUE( receiver.readPacket( time + 0.01, reader ) );
        }
        BitWriter ackWriter;
        receiver.writePacket( time + 0.01, ackWriter, 300 );
        BitReader ackReader( ackWriter.getBytes().data(), ackWriter.getByteCount() );
        (void)sender.readPacket( time + 0.02, ackReader ); // 확인만 받으면 된다 — 결과는 아래 통지로 본다
    }
    vector<NetDeliveryNotification> listNotice;
    sender.pollDeliveryNotifications( listNotice );
    int32 lostCount      = 0;
    int32 deliveredCount = 0;
    for ( const NetDeliveryNotification& notice : listNotice )
    {
        SW_EXPECT_EQUAL( int32( kKind ), int32( notice._kind ) );
        SW_EXPECT_EQUAL( -1, notice._connectionID ); // 연결 하나는 자기 ID 를 모른다 — 호스트가 채운다
        if ( notice._result == NetDeliveryResult::Lost )
        {
            SW_EXPECT_EQUAL( uint32( 101 ), notice._tag );
            ++lostCount;
        }
        else
        {
            SW_EXPECT_TRUE( notice._tag != 101 );
            ++deliveredCount;
        }
    }
    SW_EXPECT_EQUAL( 1, lostCount );
    SW_EXPECT_EQUAL( 7, deliveredCount );
    listNotice.clear();
    sender.pollDeliveryNotifications( listNotice );
    SW_EXPECT_TRUE( listNotice.empty() ); // 한 번만 낸다

    // 싣지 못한 채 나이 상한을 넘기면 잃은 것이다.
    NetConnection saturated;
    const uint8   arrBig[40] = { kKind };
    SW_REQUIRE_TRUE( saturated.sendNotifiedMessage( arrBig, 40, 9 ) );
    BitWriter tinyPacket;
    saturated.writePacket( 0.0, tinyPacket, 12 ); // 머리만 들어가는 크기라 메시지는 남는다
    BitWriter latePacket;
    saturated.writePacket( 1.0, latePacket, 300 );
    saturated.pollDeliveryNotifications( listNotice );
    SW_REQUIRE_EQUAL( size_t( 1 ), listNotice.size() );
    SW_EXPECT_EQUAL( uint32( 9 ), listNotice[0]._tag );
    SW_EXPECT_TRUE( listNotice[0]._result == NetDeliveryResult::Lost );

    SW_EXPECT_LOG( "Net.NotifiedMessageSize" ); // 빈 통지 메시지는 오류와 함께 거절한다
    SW_EXPECT_FALSE( saturated.sendNotifiedMessage( arrBig, 0, 1 ) );
}

/**
 * @brief [NetConnectionTest] 순서만 채널은 메시지 종류(첫 바이트)마다 흐름이 따로다 — 한 보내기 간격에 두 종류를 보내면 둘 다 가고, 같은 종류는 새 것만 가고,
 *        늦게 온 패킷의 다른 종류 메시지는 버리지 않는다
 * @details 예전에는 채널 전체가 흐름 하나라 마지막 메시지 하나만 실렸다 — 복제 스냅샷과 파괴 덩어리 자세가 같은 간격에 나가면 하나가 조용히 사라졌다.
 */
SW_TEST_CASE( NetConnectionTest, SequencedChannelKeepsOneStreamPerMessageKind )
{
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> snapshotOld{ 0x10, 1 };
    const vector<uint8> pose{ 0x15, 2 };
    const vector<uint8> snapshotNew{ 0x10, 3 };
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, snapshotOld.data(), 2 ) );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, pose.data(), 2 ) );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, snapshotNew.data(), 2 ) );
    BitWriter firstPacket;
    sender.writePacket( 0.0, firstPacket, 300 );
    // 한 패킷에 두 종류가 다 실린다(같은 종류의 옛 스냅샷만 빠졌다).
    {
        NetConnection direct;
        BitReader     directReader( firstPacket.getBytes().data(), firstPacket.getByteCount() );
        SW_REQUIRE_TRUE( direct.readPacket( 0.05, directReader ) );
        vector<uint8> directBuffer;
        int32         directCount = 0;
        while ( direct.receiveMessage( NetChannelType::UnreliableSequenced, directBuffer ) )
        {
            ++directCount;
        }
        SW_EXPECT_EQUAL( 2, directCount );
    }
    // 다음 간격 — 자세만 새로. 이 패킷이 먼저 도착해도 앞 패킷의 스냅샷(다른 종류)은 받는다.
    const vector<uint8> poseNew{ 0x15, 4 };
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, poseNew.data(), 2 ) );
    BitWriter secondPacket;
    sender.writePacket( 0.1, secondPacket, 300 );
    BitReader secondReader( secondPacket.getBytes().data(), secondPacket.getByteCount() );
    BitReader firstReader( firstPacket.getBytes().data(), firstPacket.getByteCount() );
    SW_EXPECT_TRUE( receiver.readPacket( 0.2, secondReader ) );
    SW_EXPECT_TRUE( receiver.readPacket( 0.3, firstReader ) );
    vector<vector<uint8>> listReceived;
    vector<uint8>         buffer;
    while ( receiver.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) )
    {
        listReceived.push_back( buffer );
    }
    SW_REQUIRE_EQUAL( size_t{ 2 }, listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0] == poseNew );     // 새 자세
    SW_EXPECT_TRUE( listReceived[1] == snapshotNew ); // 옛 패킷이지만 스냅샷 흐름에서는 처음이자 가장 새 것 — 옛 자세(2)는 버렸다
}

SW_TEST_CASE( NetConnectionTest, SequencedReplacementTakesNewSizeAndAppendReceiveConcatenates )
{
    // 같은 종류의 새 메시지는 옛 버퍼에 덮어 쓴다 — 길이가 달라도 새 바이트만 남아야 한다.
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> longOld{ 0x20, 1, 2, 3, 4, 5 };
    const vector<uint8> shortNew{ 0x20, 9 };
    const vector<uint8> other{ 0x21, 7, 7 };
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, longOld.data(), static_cast<int32>( longOld.size() ) ) );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, other.data(), static_cast<int32>( other.size() ) ) );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, shortNew.data(), static_cast<int32>( shortNew.size() ) ) );
    BitWriter packet;
    sender.writePacket( 0.0, packet, 300 );
    BitReader reader( packet.getBytes().data(), packet.getByteCount() );
    SW_REQUIRE_TRUE( receiver.readPacket( 0.05, reader ) );

    // 아레나에 덧붙이기 — 앞에 있던 바이트는 그대로 두고 메시지 바이트만 뒤에 붙는다.
    vector<uint8> arena{ 0xEE };
    SW_REQUIRE_TRUE( receiver.receiveMessageAppend( NetChannelType::UnreliableSequenced, arena ) );
    SW_REQUIRE_TRUE( receiver.receiveMessageAppend( NetChannelType::UnreliableSequenced, arena ) );
    SW_EXPECT_FALSE( receiver.receiveMessageAppend( NetChannelType::UnreliableSequenced, arena ) );
    const vector<uint8> expected{ 0xEE, 0x20, 9, 0x21, 7, 7 };
    SW_EXPECT_TRUE( arena == expected );
}

SW_TEST_CASE( NetConnectionTest, ReceivedMessageBuffersAreReusedThroughTheCallerBuffer )
{
    // 받는 쪽이 같은 버퍼로 계속 꺼내면 그 옛 용량이 다음 패킷의 메시지 자리로 돌아온다 — 메시지마다 할당하지 않는다.
    NetConnection sender;
    NetConnection receiver;
    vector<uint8> received;
    const uint8*  pFirstStorage = nullptr;
    for ( int32 round = 0; round < 3; ++round )
    {
        const vector<uint8> message( 100, static_cast<uint8>( round ) );
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, message.data(), static_cast<int32>( message.size() ) ) );
        BitWriter packet;
        sender.writePacket( 0.01 * round, packet, 300 );
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        SW_REQUIRE_TRUE( receiver.readPacket( 0.01 * round, reader ) );
        SW_REQUIRE_TRUE( receiver.receiveMessage( NetChannelType::Unreliable, received ) );
        SW_EXPECT_TRUE( received == message );
        if ( round == 0 )
            pFirstStorage = received.data();
    }
    // 1 회째 버퍼가 2 회째 꺼낼 때 연결로 돌아가 3 회째 메시지를 담았다.
    SW_EXPECT_TRUE( received.data() == pFirstStorage );
}

SW_TEST_CASE( NetConnectionTest, ReceiveQueueOverflowIsFlaggedAndDrainingFreesRoom )
{
    // 게임이 꺼내지 않는 동안 상대가 계속 넣으면 받은 큐가 상한에서 멈추고 표시가 선다(호스트는 이 표시로 끊는다). 꺼낸 만큼은 다시 들어간다.
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> message( 1000, 0x30 );
    const int32         perMessage    = 1000 + NetConnection::kIncomingMessageOverheadBytes;
    const int32         fitCount      = NetConnection::kMaxPendingReceiveBytes / perMessage;
    int32               deliveredSeen = 0;
    SW_EXPECT_LOG( "Net.ReceiveOverflow" ); // 받은 큐 넘침은 경고로 알린다
    for ( int32 round = 0; round <= fitCount; ++round )
    {
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, message.data(), static_cast<int32>( message.size() ) ) );
        BitWriter packet;
        sender.writePacket( 0.001 * round, packet, 1200 );
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        SW_REQUIRE_TRUE( receiver.readPacket( 0.001 * round, reader ) );
        if ( round < fitCount )
            SW_REQUIRE_FALSE( receiver.hasReceiveOverflowed() );
    }
    SW_EXPECT_TRUE( receiver.hasReceiveOverflowed() );
    vector<uint8> received;
    while ( receiver.receiveMessage( NetChannelType::Unreliable, received ) )
    {
        ++deliveredSeen;
    }
    SW_EXPECT_EQUAL( fitCount, deliveredSeen ); // 넘친 하나는 버렸다
    receiver.reset();
    SW_EXPECT_FALSE( receiver.hasReceiveOverflowed() );
}

/**
 * @brief [NetConnectionTest] 몸이 깨진 패킷은 확인하지 않는다 — 그 안의 신뢰 메시지는 확인되지 않았으니 다시 보내져 도착한다
 * @details 시퀀스를 받은 표에 먼저 넣고 몸을 읽으면, 읽기에 실패해도 다음 답의 확인에 그 시퀀스가 실린다. 보낸 쪽은 그 패킷의 신뢰 메시지를 전달된 것으로
 *          지우고 다시 보내지 않는다 — 체크섬은 맞고 몸이 틀린 패킷(다른 빌드 · 악의) 하나가 메시지를 영영 없앤다.
 */
SW_TEST_CASE( NetConnectionTest, MalformedPayloadIsNotAcknowledged )
{
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> message = NetTestUtil::makeMessage( 777, 40 );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::ReliableOrdered, message.data(), static_cast<int32>( message.size() ) ) );
    BitWriter packet;
    sender.writePacket( 0.0, packet, 300 );
    // 머리(시퀀스 · 확인)는 온전하고 메시지 몸이 잘렸다.
    SW_REQUIRE_TRUE( packet.getByteCount() > 40 );
    BitReader truncated( packet.getBytes().data(), 20 );
    SW_EXPECT_FALSE( receiver.readPacket( 0.05, truncated ) );

    vector<uint8> buffer;
    int32         receivedCount = 0;
    float64       time          = 0.1;
    for ( int32 round = 0; round < 40 && receivedCount == 0; ++round )
    {
        time += 0.05;
        BitWriter reply;
        receiver.writePacket( time, reply, 300 );
        BitReader replyReader( reply.getBytes().data(), reply.getByteCount() );
        (void)sender.readPacket( time + 0.01, replyReader ); // ack 만 받으면 된다 — 결과는 아래 수신 메시지로 확인한다
        BitWriter next;
        sender.writePacket( time + 0.02, next, 300 );
        BitReader nextReader( next.getBytes().data(), next.getByteCount() );
        (void)receiver.readPacket( time + 0.03, nextReader ); // 중복이면 false — 결과는 아래 수신 메시지로 확인한다
        while ( receiver.receiveMessage( NetChannelType::ReliableOrdered, buffer ) )
        {
            ++receivedCount;
        }
    }
    SW_REQUIRE_EQUAL( 1, receivedCount );
    SW_EXPECT_EQUAL( 777, NetTestUtil::readMessageValue( buffer ) );
    // 이번에는 온전한 패킷이 확인된다.
    BitWriter finalReply;
    receiver.writePacket( time + 0.05, finalReply, 300 );
    BitReader finalReader( finalReply.getBytes().data(), finalReply.getByteCount() );
    SW_REQUIRE_TRUE( sender.readPacket( time + 0.06, finalReader ) );
    SW_EXPECT_EQUAL( 0, sender.getPendingReliableCount() );
}

/**
 * @brief [NetConnectionTest] 대역폭 통계는 최근 1 초에 보내고 받은 패킷을 모두 센다 — 보내기 간격이 짧아도(120 Hz) 덜 측정하지 않는다
 * @details 최근 64 패킷만 보면 1/64 초보다 짧은 간격에서는 1 초 창의 일부만 센다(120 Hz 면 절반 남짓).
 */
SW_TEST_CASE( NetConnectionTest, BandwidthStatCountsEveryPacketInTheLastSecond )
{
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> payload     = NetTestUtil::makeMessage( 1, 100 );
    int32               packetBytes = 0;
    for ( int32 frame = 0; frame <= 240; ++frame )
    {
        const float64 time = static_cast<float64>( frame ) / 120.0;
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, payload.data(), static_cast<int32>( payload.size() ) ) );
        BitWriter packet;
        sender.writePacket( time, packet, 1200 );
        packetBytes = packet.getByteCount();
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        SW_REQUIRE_TRUE( receiver.readPacket( time + 0.01, reader ) );
    }
    // 120 패킷 × 바이트 × 8 비트 / 1000 = 초당 킬로비트.
    const float32 expectedKilobits = 120.0f * static_cast<float32>( packetBytes ) * 8.0f / 1000.0f;
    SW_EXPECT_NEAR( expectedKilobits, sender.getStats()._sentBandwidth, expectedKilobits * 0.05f );
    SW_EXPECT_NEAR( expectedKilobits, receiver.getStats()._receivedBandwidth, expectedKilobits * 0.05f );
}

/**
 * @brief [NetConnectionTest] 뒤에 보낸 패킷들이 확인됐는데 확인이 없는 패킷의 신뢰 메시지는 재전송 간격을 기다리지 않고 바로 다시 실린다(빠른 재전송)
 * @details 재전송을 시계(RTT 기반)로만 하면 손실 많은 회선에서 신뢰 순서 채널의 꼬리가 길다 — 앞 메시지를 기다리는 머리 막힘이 그 지연을 뒤 메시지 모두에
 *          옮긴다(250 ms · 손실 15 % 파괴 시나리오에서 사건 최대 지연 4.1 초). 여기서는 RTT 를 1 초로 익힌 뒤 회선이 빨라진 경우로, 시계라면 1 초 넘게
 *          기다릴 메시지가 0.1 초 안에 다시 간다.
 */
SW_TEST_CASE( NetConnectionTest, LostPacketIsResentBeforeTheResendDelay )
{
    NetConnection sender;
    NetConnection receiver;
    const auto    sendTo = []( NetConnection& from, NetConnection& to, float64 sendTime, float64 receiveTime, bool bDeliver )
    {
        BitWriter packet;
        from.writePacket( sendTime, packet, 300 );
        if ( bDeliver == false )
            return;
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        (void)to.readPacket( receiveTime, reader ); // 버린 · 중복 패킷이면 false — 결과는 테스트 단언이 본다
    };
    // RTT 1 초를 익힌다 — 한쪽 0.5 초.
    float64 time = 0.0;
    for ( int32 round = 0; round < 30; ++round )
    {
        time += 1.0;
        sendTo( sender, receiver, time, time + 0.5, true );
        sendTo( receiver, sender, time + 0.5, time + 1.0, true );
    }
    SW_REQUIRE_TRUE( sender.getStats()._rtt > 0.8f );

    // 신뢰 메시지를 실은 패킷을 잃고, 뒤의 패킷 넷(비신뢰)은 빨라진 회선으로 간다.
    time += 2.0;
    const vector<uint8> message = NetTestUtil::makeMessage( 4242, 8 );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::ReliableOrdered, message.data(), static_cast<int32>( message.size() ) ) );
    sendTo( sender, receiver, time, time, false );
    for ( int32 index = 1; index <= 4; ++index )
    {
        const vector<uint8> filler = NetTestUtil::makeMessage( index, 4 );
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, filler.data(), static_cast<int32>( filler.size() ) ) );
        sendTo( sender, receiver, time + 0.01 * index, time + 0.01 * index + 0.02, true );
    }
    sendTo( receiver, sender, time + 0.07, time + 0.09, true );
    sendTo( sender, receiver, time + 0.1, time + 0.12, true );
    vector<uint8> buffer;
    SW_REQUIRE_TRUE( receiver.receiveMessage( NetChannelType::ReliableOrdered, buffer ) );
    SW_EXPECT_EQUAL( 4242, NetTestUtil::readMessageValue( buffer ) );
}

/**
 * @brief [NetConnectionTest] 채널 상한을 넘는 메시지는 오류 로그와 함께 거절되고 아무것도 남기지 않는다. 신뢰 창이 메시지 전체를 받을 수 없으면 통째로 거절된다 — 막힘은 오류가 아니다(로그 없음)
 */
SW_TEST_CASE( NetConnectionTest, OversizedMessagesAreRefusedLoudlyAndAFullWindowQuietly )
{
    NetConnection       connection;
    const vector<uint8> tooLarge     = NetTestUtil::makePatternMessage( 6u, NetConnection::kMaxReliableMessageSize + 1 );
    const vector<uint8> unfragmented = NetTestUtil::makePatternMessage( 7u, NetConnection::kMaxSingleMessageSize + 1 );
    {
        SW_EXPECT_LOG( "Net.MessageTooLarge" ); // 채널 상한을 넘는 셋은 오류와 함께 거절한다
        SW_EXPECT_FALSE( connection.sendMessage( NetChannelType::ReliableOrdered, tooLarge.data(), static_cast<int32>( tooLarge.size() ) ) );
        SW_EXPECT_FALSE( connection.sendMessage( NetChannelType::UnreliableSequenced, unfragmented.data(), static_cast<int32>( unfragmented.size() ) ) );
        SW_EXPECT_FALSE( connection.sendMessage( NetChannelType::Unreliable, unfragmented.data(), static_cast<int32>( unfragmented.size() ) ) );
    }
    SW_EXPECT_EQUAL( 0, connection.getPendingReliableCount() ); // 거절한 것은 조각 하나도 남기지 않는다
    SW_EXPECT_FALSE( connection.hasDataToSend( 0.0 ) );

    // 창 — 64 KB 는 조각 64 개. 셋(192)은 들어가고, 넷째(256)는 창(미확인 255)을 넘어 통째로 거절된다. 63 개짜리는 꼭 맞고, 그 뒤 1 바이트는 거절된다.
    // 창이 차서 거절하는 것은 오류가 아니다 — 다른 id 의 로그가 나오면 케이스의 로그 게이트가 실패시킨다.
    const vector<uint8> largest    = NetTestUtil::makePatternMessage( 5u, NetConnection::kMaxReliableMessageSize );
    const int32         perMessage = NetConnection::kMaxReliableMessageSize / NetConnection::kMaxSingleMessageSize;
    for ( int32 index = 0; index < 3; ++index )
    {
        SW_EXPECT_TRUE( connection.sendMessage( NetChannelType::ReliableOrdered, largest.data(), static_cast<int32>( largest.size() ) ) );
    }
    SW_EXPECT_EQUAL( 3 * perMessage, connection.getPendingReliableCount() );
    SW_EXPECT_FALSE( connection.sendMessage( NetChannelType::ReliableOrdered, largest.data(), static_cast<int32>( largest.size() ) ) );
    SW_EXPECT_EQUAL( 3 * perMessage, connection.getPendingReliableCount() );
    SW_EXPECT_TRUE( connection.sendMessage( NetChannelType::ReliableOrdered, largest.data(), 63 * NetConnection::kMaxSingleMessageSize ) );
    SW_EXPECT_EQUAL( NetConnection::kReliableWindow - 1, connection.getPendingReliableCount() );
    SW_EXPECT_FALSE( connection.sendMessage( NetChannelType::ReliableOrdered, largest.data(), 1 ) );
}

/**
 * @brief [NetConnectionTest] 큰 신뢰 메시지의 조각이 패킷을 채워도 순서만 메시지(스냅샷)가 굶지 않는다 — 앞 패킷이 남겼으면 다음 패킷은 그쪽부터 싣는다(번갈아)
 * @details 조각(1 KB) 뒤에는 150 B 남짓만 남아 600 B 스냅샷이 못 들어간다. 늘 신뢰부터 실으면 32 KB 를 보내는 동안 스냅샷이 하나도 가지 않는다.
 */
SW_TEST_CASE( NetConnectionTest, LargeReliableMessageDoesNotStarveSequencedMessages )
{
    NetConnection       sender;
    NetConnection       receiver;
    const int32         fragmentCount = 32;
    const vector<uint8> large         = NetTestUtil::makePatternMessage( 10u, fragmentCount * NetConnection::kMaxSingleMessageSize );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::ReliableOrdered, large.data(), static_cast<int32>( large.size() ) ) );
    const auto exchange = [&]( float64 time )
    {
        BitWriter packet;
        sender.writePacket( time, packet, kNetMaxPacketSize - 8 );
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        SW_EXPECT_TRUE( receiver.readPacket( time + 0.01, reader ) );
        BitWriter reply;
        receiver.writePacket( time + 0.01, reply, kNetMaxPacketSize - 8 );
        BitReader replyReader( reply.getBytes().data(), reply.getByteCount() );
        (void)sender.readPacket( time + 0.02, replyReader ); // ack 만 받으면 된다 — 결과는 아래 스냅숏 수로 확인한다
    };
    int32         snapshotCount = 0;
    vector<uint8> buffer;
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        vector<uint8> snapshot( 600, static_cast<uint8>( frame ) );
        snapshot[0] = 0x10; // 종류 하나 — 흐름 하나
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, snapshot.data(), static_cast<int32>( snapshot.size() ) ) );
        exchange( 0.05 * frame );
        while ( receiver.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) )
        {
            ++snapshotCount;
        }
    }
    SW_EXPECT_TRUE_MSG( snapshotCount >= 9, "about every other packet carries the snapshot" );
    SW_EXPECT_TRUE_MSG( fragmentCount - sender.getPendingReliableCount() >= 9, "the reliable transfer keeps going too" );

    // 스냅샷이 그치면 조각만 — 메시지는 바이트 그대로 끝난다.
    vector<vector<uint8>> listReceived;
    for ( int32 frame = 20; frame < 80 && listReceived.empty(); ++frame )
    {
        exchange( 0.05 * frame );
        while ( receiver.receiveMessage( NetChannelType::ReliableOrdered, buffer ) )
        {
            listReceived.push_back( buffer );
        }
    }
    SW_REQUIRE_EQUAL( size_t{ 1 }, listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0] == large );
}

/**
 * @brief [NetConnectionTest] 신뢰 · 순서 없음 메시지는 앞 신뢰 메시지를 잃어도 기다리지 않고 받는 대로 건네지고, 다시 와도 두 번 건네지지 않는다. 신뢰 순서는 여전히 순서대로다
 * @details 두 채널은 신뢰 id · 창을 같이 쓴다 — 순서 없음 자리에는 "건넸다" 표만 남아 순서 커서가 지나간다. 순서 없음은 조각나지 않는다(1 KB 까지).
 */
SW_TEST_CASE( NetConnectionTest, ReliableUnorderedSkipsTheHeadOfLineButStaysReliable )
{
    NetConnection sender;
    NetConnection receiver;
    const auto    writeOne = [&sender]( NetChannelType channel, int32 value, float64 time ) -> vector<uint8>
    {
        const vector<uint8> message = NetTestUtil::makeMessage( value );
        SW_EXPECT_TRUE( sender.sendMessage( channel, message.data(), static_cast<int32>( message.size() ) ) );
        BitWriter packet;
        sender.writePacket( time, packet, 300 );
        return vector<uint8>( packet.getBytes().data(), packet.getBytes().data() + packet.getByteCount() );
    };
    const auto deliver = [&receiver]( const vector<uint8>& listPacket, float64 time ) -> bool
    {
        BitReader reader( listPacket.data(), static_cast<int32>( listPacket.size() ) );
        return receiver.readPacket( time, reader );
    };
    const auto pollAll = [&receiver]( NetChannelType channel ) -> vector<int32>
    {
        vector<int32> listValue;
        vector<uint8> buffer;
        while ( receiver.receiveMessage( channel, buffer ) )
        {
            listValue.push_back( NetTestUtil::readMessageValue( buffer ) );
        }
        return listValue;
    };

    // 패킷마다 메시지 하나 — 순서 1 · 없음 2 · 순서 3 · 없음 4. 첫 패킷(순서 1)을 잃고, 나머지는 뒤바뀌어 온다.
    (void)writeOne( NetChannelType::ReliableOrdered, 1, 0.00 );
    const vector<uint8> packetA = writeOne( NetChannelType::ReliableUnordered, 2, 0.01 );
    const vector<uint8> packetB = writeOne( NetChannelType::ReliableOrdered, 3, 0.02 );
    const vector<uint8> packetC = writeOne( NetChannelType::ReliableUnordered, 4, 0.03 );
    SW_REQUIRE_TRUE( deliver( packetC, 0.05 ) );
    SW_REQUIRE_TRUE( deliver( packetA, 0.06 ) );
    SW_REQUIRE_TRUE( deliver( packetB, 0.07 ) );
    SW_EXPECT_TRUE( pollAll( NetChannelType::ReliableUnordered ) == ( vector<int32>{ 4, 2 } ) ); // 받는 대로 — 순서 1 을 기다리지 않는다
    SW_EXPECT_TRUE( pollAll( NetChannelType::ReliableOrdered ).empty() );                        // 순서 3 은 잃은 1 을 기다린다

    // 확인이 보내는 쪽에 가지 않았다 — 재전송 간격(0.1 초)이 지나 넷이 모두 다시 간다. 순서는 1, 3 으로 이어지고 없음 2 · 4 는 다시 건네지지 않는다.
    BitWriter resend;
    sender.writePacket( 0.5, resend, 300 );
    BitReader resendReader( resend.getBytes().data(), resend.getByteCount() );
    SW_REQUIRE_TRUE( receiver.readPacket( 0.55, resendReader ) );
    SW_EXPECT_TRUE( pollAll( NetChannelType::ReliableOrdered ) == ( vector<int32>{ 1, 3 } ) );
    SW_EXPECT_TRUE( pollAll( NetChannelType::ReliableUnordered ).empty() );

    // 순서 없음은 조각나지 않는다.
    SW_EXPECT_LOG( "Net.MessageTooLarge" ); // 조각 하나를 넘는 순서 없음 메시지는 오류와 함께 거절한다
    const vector<uint8> tooLarge( static_cast<size_t>( NetConnection::kMaxSingleMessageSize + 1 ), 0 );
    SW_EXPECT_FALSE( sender.sendMessage( NetChannelType::ReliableUnordered, tooLarge.data(), static_cast<int32>( tooLarge.size() ) ) );
}

/**
 * @brief [NetConnectionTest] 비신뢰 메시지는 보낼 줄에서 나이 상한(기본 0.25 초)을 넘게 기다리면 싣지 않고 버린다 — 포화가 풀릴 때 옛 비신뢰가 몰려 나가지 않는다. 상한 0 이면 버리지 않는다
 */
SW_TEST_CASE( NetConnectionTest, UnreliableMessagesExpireAfterWaitingTooLong )
{
    NetConnection sender;
    NetConnection receiver;
    for ( int32 index = 0; index < 3; ++index )
    {
        const vector<uint8> buffer = NetTestUtil::makeMessage( index );
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, buffer.data(), 4 ) );
    }
    BitWriter starved;
    sender.writePacket( 0.0, starved, 0 ); // 대역폭 토큰을 다 쓴 전송 — 헤더만 나가고 메시지는 기다린다
    SW_EXPECT_EQUAL( 3, sender.getQueuedUnreliableCount() );

    BitWriter late;
    sender.writePacket( 0.5, late, 1200 ); // 0.5 초를 기다렸다 — 상한을 넘었다
    SW_EXPECT_EQUAL( 0, sender.getQueuedUnreliableCount() );
    SW_EXPECT_EQUAL( uint64( 3 ), sender.getStats()._expiredUnreliableCount );
    BitReader lateReader( late.getBytes().data(), late.getByteCount() );
    SW_EXPECT_TRUE( receiver.readPacket( 0.5, lateReader ) );
    vector<uint8> buffer;
    SW_EXPECT_FALSE( receiver.receiveMessage( NetChannelType::Unreliable, buffer ) );

    // 상한 안이면 그대로 간다.
    const vector<uint8> fresh = NetTestUtil::makeMessage( 7 );
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, fresh.data(), 4 ) );
    BitWriter onTime;
    sender.writePacket( 0.6, onTime, 1200 );
    BitReader onTimeReader( onTime.getBytes().data(), onTime.getByteCount() );
    SW_EXPECT_TRUE( receiver.readPacket( 0.6, onTimeReader ) );
    SW_REQUIRE_TRUE( receiver.receiveMessage( NetChannelType::Unreliable, buffer ) );
    SW_EXPECT_EQUAL( 7, NetTestUtil::readMessageValue( buffer ) );

    // 상한 0 — 오래 기다려도 버리지 않는다.
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::Unreliable, fresh.data(), 4 ) );
    BitWriter noRoom;
    sender.writePacket( 1.0, noRoom, 0, NetConnection::kDefaultAckRequestInterval, 0.0 );
    BitWriter muchLater;
    sender.writePacket( 5.0, muchLater, 1200, NetConnection::kDefaultAckRequestInterval, 0.0 );
    SW_EXPECT_EQUAL( 0, sender.getQueuedUnreliableCount() );
    SW_EXPECT_EQUAL( uint64( 3 ), sender.getStats()._expiredUnreliableCount );
}

/**
 * @brief [NetConnectionTest] 한 종류가 쉬는 동안 다른 종류가 번호 공간의 반(32768) 넘게 나가도, 다시 나온 그 종류는 버려지지 않는다 — 번호는 종류마다 따로 센다(감사 N-2)
 */
SW_TEST_CASE( NetConnectionTest, SequencedKindIdleForHalfTheSequenceSpaceStillDelivers )
{
    constexpr int32     kOtherKindPacketCount = 40000;
    NetConnection       sender;
    NetConnection       receiver;
    const vector<uint8> poseOld{ 0x15, 1 };
    const vector<uint8> snapshot{ 0x10, 2 };
    const vector<uint8> poseNew{ 0x15, 3 };

    vector<vector<uint8>> listReceived; // 자세 종류만 모은다
    vector<uint8>         buffer;
    const auto            deliver = [&]( BitWriter& packet )
    {
        BitReader reader( packet.getBytes().data(), packet.getByteCount() );
        SW_EXPECT_TRUE( receiver.readPacket( 0.1, reader ) );
        while ( receiver.receiveMessage( NetChannelType::UnreliableSequenced, buffer ) )
        {
            if ( buffer[0] == poseOld[0] )
                listReceived.push_back( buffer );
        }
    };
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, poseOld.data(), 2 ) );
    BitWriter firstPacket;
    sender.writePacket( 0.0, firstPacket, 300 );
    deliver( firstPacket );
    for ( int32 packet = 0; packet < kOtherKindPacketCount; ++packet )
    {
        SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, snapshot.data(), 2 ) );
        BitWriter other; // 패킷 번호가 이어지게 받는 쪽도 읽는다(멀리 건너뛴 패킷은 받는 쪽이 거절한다)
        sender.writePacket( 0.0, other, 300 );
        deliver( other );
    }
    SW_REQUIRE_TRUE( sender.sendMessage( NetChannelType::UnreliableSequenced, poseNew.data(), 2 ) );
    BitWriter lastPacket;
    sender.writePacket( 0.0, lastPacket, 300 );
    deliver( lastPacket );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listReceived.size() );
    SW_EXPECT_TRUE( listReceived[0] == poseOld );
    SW_EXPECT_TRUE( listReceived[1] == poseNew );
}
