#include "pch.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/Replication/NetInputWindow.h"

#include "TestFramework/TestFramework.h"

// 비신뢰 입력 묶음 — 확인 기반 보내는 창(확인된 다음 틱부터 · 예산이 모자라면 오래된 것부터 · 틱이 건너뛰거나 줄 때), 받는 버퍼(창 · 빈틈없이 받은 다음 틱 ·
// 깨진 묶음은 하나도 넣지 않는다 · 가장 새 틱을 따르는 창), 고정 길이(롤백 1 바이트 — 길이 칸 없음, 옛 배치와 같은 바이트) · 항목 스탬프.

using namespace sw;

namespace
{
    struct NetInputWindowTestInternal
    {
        static constexpr NetInputFormat kBlobFormat{ 0, 255, 32, SW_FALSE };
        static constexpr NetInputFormat kButtonFormat{ 1, 1, 64, SW_FALSE };
        static constexpr NetInputFormat kStampedFormat{ 0, 255, 32, SW_TRUE };

        /** @brief 보내는 창이 쓴 묶음을 받는 버퍼가 읽습니다(종류 바이트 하나를 앞에 둔 셈으로 예산을 센다). 깨졌으면 false 입니다. */
        static bool deliver( const NetInputSendWindow& window, NetInputReceiveBuffer& buffer, int32 budgetBytes = NetConnection::kMaxSingleMessageSize )
        {
            BitWriter     writer;
            NetSendBudget budget( budgetBytes );
            budget.reserveBits( 8 );
            (void)window.write( writer, budget ); // 실은 개수는 보지 않는다 — 판정은 받는 쪽 read 가 한다
            BitReader reader( writer.getBytes().data(), writer.getByteCount() );
            return buffer.read( reader );
        }
    };
} // namespace

/**
 * @brief [NetInputWindowTest] 보내는 창은 상대가 확인한 다음 틱부터 싣고, 확인은 늘기만 하며 아직 넣지 않은 틱은 확인할 수 없다. 같은 틱은 처음 값이 남는다
 */
SW_TEST_CASE( NetInputWindowTest, SendWindowStartsAfterTheAcknowledgedTick )
{
    NetInputSendWindow window;
    window.initialize( 32, NetInputWindowTestInternal::kBlobFormat );
    for ( uint32 tick = 0; tick < 10; ++tick )
    {
        const uint8 value = static_cast<uint8>( tick );
        SW_ASSERT_TRUE( window.push( tick, &value, 1 ) );
    }
    window.acknowledge( 6 );
    SW_EXPECT_EQUAL( 6u, window.getFirstPendingTick() );
    SW_EXPECT_EQUAL( 4, window.getPendingCount() );

    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kBlobFormat, NetInputWindowMode::Manual );
    SW_ASSERT_TRUE( NetInputWindowTestInternal::deliver( window, buffer ) );
    SW_EXPECT_NULL( buffer.find( 5 ) ); // 확인된 것은 다시 싣지 않는다
    for ( uint32 tick = 6; tick < 10; ++tick )
    {
        const NetInputEntry* pEntry = buffer.find( tick );
        SW_ASSERT_NOT_NULL( pEntry );
        SW_EXPECT_EQUAL( static_cast<uint8>( tick ), pEntry->_bytes[0] );
    }

    window.acknowledge( 3 ); // 늦게 온 옛 확인
    SW_EXPECT_EQUAL( 4, window.getPendingCount() );
    window.acknowledge( 100 ); // 아직 넣지 않은 틱
    SW_EXPECT_EQUAL( 10u, window.getFirstPendingTick() );
    SW_EXPECT_EQUAL( 0, window.getPendingCount() );

    const uint8 other = 99;
    SW_EXPECT_FALSE( window.push( 9, &other, 1 ) ); // 이미 있다
    const uint8        tooLong[2] = { 1, 2 };
    NetInputSendWindow buttons;
    buttons.initialize( 8, NetInputWindowTestInternal::kButtonFormat );
    SW_EXPECT_FALSE( buttons.push( 0, tooLong, 2 ) ); // 고정 길이와 다르다
}

/**
 * @brief [NetInputWindowTest] 틱이 건너뛰면 그 틱부터 다시 쌓고, 틱이 줄면(새 판) 확인까지 비운다
 */
SW_TEST_CASE( NetInputWindowTest, TickGapRestartsAndTickDropResets )
{
    NetInputSendWindow window;
    window.initialize( 32, NetInputWindowTestInternal::kBlobFormat );
    const uint8 value = 1;
    for ( uint32 tick = 0; tick < 5; ++tick )
    {
        SW_ASSERT_TRUE( window.push( tick, &value, 1 ) );
    }
    window.acknowledge( 3 );
    SW_ASSERT_TRUE( window.push( 10, &value, 1 ) );
    SW_EXPECT_EQUAL( 10u, window.getFirstPendingTick() );
    SW_EXPECT_EQUAL( 1, window.getPendingCount() );
    SW_ASSERT_TRUE( window.push( 2, &value, 1 ) );
    SW_EXPECT_EQUAL( 2u, window.getFirstPendingTick() );
    SW_EXPECT_EQUAL( 1, window.getPendingCount() );
}

/**
 * @brief [NetInputWindowTest] 한 방향이 25 틱 동안 모두 사라져도 받는 쪽에 빈틈이 남지 않는다 — 확인이 오를 때까지 다음 묶음이 다시 싣는다
 * @details "최근 N 개" 만 겹쳐 보내면 끊김이 끝난 첫 묶음이 마지막 N 틱만 실어, 받는 쪽의 "빈틈없이 받은 다음 틱" 이 끊김 첫 틱(20)에서 영원히 멈춘다.
 */
SW_TEST_CASE( NetInputWindowTest, BurstLossLongerThanRecentRedundancyLeavesNoGap )
{
    NetInputSendWindow window;
    window.initialize( 32, NetInputWindowTestInternal::kBlobFormat );
    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kBlobFormat, NetInputWindowMode::FollowNewest );
    uint32 ackInFlight = 0; // 확인은 한 틱 늦게 닿는다
    for ( uint32 tick = 0; tick < 100; ++tick )
    {
        const uint8 arrValue[2] = { static_cast<uint8>( tick ), static_cast<uint8>( tick >> 8 ) };
        SW_ASSERT_TRUE( window.push( tick, arrValue, 2 ) );
        window.acknowledge( ackInFlight );
        const bool bLost = 20u <= tick && tick < 45u;
        if ( bLost == false )
            SW_ASSERT_TRUE( NetInputWindowTestInternal::deliver( window, buffer ) );
        ackInFlight = buffer.getFirstMissingTick();
    }
    SW_EXPECT_EQUAL( 100u, buffer.getFirstMissingTick() );
    for ( uint32 tick = 36; tick < 100; ++tick ) // 고리(64)에 남은 것
    {
        const NetInputEntry* pEntry = buffer.find( tick );
        SW_ASSERT_NOT_NULL( pEntry );
        SW_EXPECT_EQUAL( static_cast<uint8>( tick ), pEntry->_bytes[0] );
    }
    SW_EXPECT_TRUE( window.getPendingCount() <= 1 ); // 확인이 따라와 다시 싣는 것은 거의 없다
}

/**
 * @brief [NetInputWindowTest] 예산이 모자라면 오래된 것부터 싣고 새 것은 다음 묶음이 이어 싣는다 — 메시지 상한을 넘지 않는다
 */
SW_TEST_CASE( NetInputWindowTest, ByteBudgetSendsTheOldestFirst )
{
    NetInputSendWindow window;
    window.initialize( 32, NetInputWindowTestInternal::kBlobFormat );
    const vector<uint8> big( 200, 3 );
    for ( uint32 tick = 0; tick < 10; ++tick )
    {
        SW_ASSERT_TRUE( window.push( tick, big.data(), static_cast<int32>( big.size() ) ) );
    }

    BitWriter     writer;
    NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
    budget.reserveBits( 8 );
    const int32 written = window.write( writer, budget );
    SW_EXPECT_TRUE( 3 <= written && written < 10 );
    SW_EXPECT_TRUE( writer.getByteCount() + 1 <= NetConnection::kMaxSingleMessageSize );

    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kBlobFormat, NetInputWindowMode::Manual );
    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_ASSERT_TRUE( buffer.read( reader ) );
    SW_EXPECT_NOT_NULL( buffer.find( 0 ) );
    SW_EXPECT_NULL( buffer.find( 9 ) );
    SW_EXPECT_EQUAL( static_cast<uint32>( written ), buffer.getFirstMissingTick() );

    int32 rounds = 0;
    for ( ; rounds < 5 && buffer.getFirstMissingTick() < 10u; ++rounds )
    {
        window.acknowledge( buffer.getFirstMissingTick() );
        SW_ASSERT_TRUE( NetInputWindowTestInternal::deliver( window, buffer ) );
    }
    SW_EXPECT_EQUAL( 10u, buffer.getFirstMissingTick() );
}

/**
 * @brief [NetInputWindowTest] 받는 창 밖(아래 · 고리 한 바퀴 뒤)은 버리고, 끝은 첫 틱 + 크기로 잘리며 첫 틱은 줄지 않는다. 창 아래는 받은 것으로 친다
 */
SW_TEST_CASE( NetInputWindowTest, ReceiveWindowRejectsTicksOutsideIt )
{
    NetInputReceiveBuffer buffer;
    buffer.initialize( 16, NetInputWindowTestInternal::kButtonFormat, NetInputWindowMode::Manual );
    buffer.setWindow( 10, 1000 );
    SW_EXPECT_EQUAL( 26u, buffer.getWindowEnd() );
    SW_EXPECT_EQUAL( 10u, buffer.getFirstMissingTick() );
    const uint8 value = 1;
    SW_EXPECT_FALSE( buffer.store( 9, &value, 1 ) );
    SW_EXPECT_FALSE( buffer.store( 26, &value, 1 ) ); // 받아 두면 10 의 칸을 덮는다
    SW_EXPECT_TRUE( buffer.store( 10, &value, 1 ) );
    SW_EXPECT_FALSE( buffer.store( 10, &value, 1 ) ); // 이미 있다
    SW_EXPECT_EQUAL( 11u, buffer.getFirstMissingTick() );
    SW_EXPECT_TRUE( buffer.store( 12, &value, 1 ) );
    SW_EXPECT_EQUAL( 11u, buffer.getFirstMissingTick() ); // 11 이 빈다
    buffer.setWindow( 5, 30 );
    SW_EXPECT_EQUAL( 10u, buffer.getWindowFirst() );
    buffer.setWindow( 12, NetInputReceiveBuffer::kNoWindowEnd ); // 11 을 놓는다
    SW_EXPECT_EQUAL( 13u, buffer.getFirstMissingTick() );
    const NetInputEntry* pLatest = buffer.findLatestAtOrBefore( 20, buffer.getWindowFirst() );
    SW_ASSERT_NOT_NULL( pLatest );
    SW_EXPECT_EQUAL( 12u, pLatest->_tick );
}

/**
 * @brief [NetInputWindowTest] 가장 새 틱을 따르는 창은 클라이언트 틱이 어디서 시작하든 받고, 크기보다 오래된 것은 놓는다(권위 서버)
 */
SW_TEST_CASE( NetInputWindowTest, FollowNewestWindowStartsAtTheSendersTick )
{
    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kBlobFormat, NetInputWindowMode::FollowNewest );
    const uint8 value = 1;
    SW_EXPECT_TRUE( buffer.store( 100, &value, 1 ) );
    SW_EXPECT_EQUAL( 37u, buffer.getWindowFirst() );
    SW_EXPECT_EQUAL( 37u, buffer.getFirstMissingTick() );
    SW_EXPECT_TRUE( buffer.store( 37, &value, 1 ) );
    SW_EXPECT_EQUAL( 38u, buffer.getFirstMissingTick() );
    SW_EXPECT_TRUE( buffer.store( 200, &value, 1 ) ); // 앞의 것은 모두 창 아래로
    SW_EXPECT_EQUAL( 137u, buffer.getFirstMissingTick() );
    SW_EXPECT_FALSE( buffer.store( 100, &value, 1 ) );
}

/**
 * @brief [NetInputWindowTest] 깨진 묶음(길이 상한 넘음 · 개수 상한 넘음 · 모자란 바이트)은 앞 항목도 넣지 않는다
 */
SW_TEST_CASE( NetInputWindowTest, MalformedBatchStoresNothing )
{
    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kBlobFormat, NetInputWindowMode::Manual );
    BitWriter writer;
    writer.writeVarUint( 4 );
    writer.writeVarUint( 2 );
    const uint8 tiny = 9;
    writer.writeBlob( &tiny, 1 );
    const vector<uint8> oversize( 300, 7 );
    writer.writeBlob( oversize.data(), static_cast<int32>( oversize.size() ) );
    BitReader reader( writer.getBytes().data(), writer.getByteCount() );
    SW_EXPECT_FALSE( buffer.read( reader ) );
    SW_EXPECT_NULL( buffer.find( 4 ) );
    SW_EXPECT_EQUAL( 0u, buffer.getFirstMissingTick() );

    BitWriter tooMany;
    tooMany.writeVarUint( 0 );
    tooMany.writeVarUint( 33 );
    BitReader tooManyReader( tooMany.getBytes().data(), tooMany.getByteCount() );
    SW_EXPECT_FALSE( buffer.read( tooManyReader ) );

    BitWriter truncated;
    truncated.writeVarUint( 0 );
    truncated.writeVarUint( 3 );
    truncated.writeBlob( &tiny, 1 );
    BitReader truncatedReader( truncated.getBytes().data(), truncated.getByteCount() );
    SW_EXPECT_FALSE( buffer.read( truncatedReader ) );
    SW_EXPECT_NULL( buffer.find( 0 ) );
}

/**
 * @brief [NetInputWindowTest] 고정 길이 항목은 길이 칸 없이 8 비트씩 — 롤백이 손으로 쓰던 배치와 바이트까지 같다(롤백 와이어 판이 그대로인 근거)
 */
SW_TEST_CASE( NetInputWindowTest, FixedEntriesMatchTheHandWrittenRollbackLayout )
{
    NetInputSendWindow window;
    window.initialize( 64, NetInputWindowTestInternal::kButtonFormat );
    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        const uint8 input = static_cast<uint8>( frame * 3 + 1 );
        SW_ASSERT_TRUE( window.push( frame, &input, 1 ) );
    }
    // 롤백 메시지는 앞에 홀수 비트가 오지 않지만, 비트 경계가 어긋난 자리에서도 같아야 한다.
    for ( int32 leadingBits = 0; leadingBits < 8; leadingBits += 3 )
    {
        BitWriter written;
        written.writeBits( 0, leadingBits );
        NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
        SW_EXPECT_EQUAL( 5, window.write( written, budget ) );

        BitWriter manual;
        manual.writeBits( 0, leadingBits );
        manual.writeVarUint( 0 );
        manual.writeVarUint( 5 );
        for ( uint32 frame = 0; frame < 5; ++frame )
        {
            manual.writeBits( frame * 3 + 1, 8 );
        }
        SW_EXPECT_EQUAL( manual.getBitCount(), written.getBitCount() );
        SW_EXPECT_TRUE( manual.getBytes() == written.getBytes() );
    }
}

/**
 * @brief [NetInputWindowTest] 스탬프 형식이면 항목마다 스탬프가 함께 간다(입력이 만들어진 순간 — 서브틱 입력이 쓴다)
 */
SW_TEST_CASE( NetInputWindowTest, StampsTravelWithEntries )
{
    NetInputSendWindow window;
    window.initialize( 32, NetInputWindowTestInternal::kStampedFormat );
    const uint8 value = 5;
    SW_ASSERT_TRUE( window.push( 7, &value, 1, 1234u ) );
    SW_ASSERT_TRUE( window.push( 8, &value, 1, 99u ) );
    NetInputReceiveBuffer buffer;
    buffer.initialize( 64, NetInputWindowTestInternal::kStampedFormat, NetInputWindowMode::FollowNewest );
    SW_ASSERT_TRUE( NetInputWindowTestInternal::deliver( window, buffer ) );
    SW_ASSERT_NOT_NULL( buffer.find( 7 ) );
    SW_ASSERT_NOT_NULL( buffer.find( 8 ) );
    SW_EXPECT_EQUAL( 1234u, buffer.find( 7 )->_stamp );
    SW_EXPECT_EQUAL( 99u, buffer.find( 8 )->_stamp );
}
