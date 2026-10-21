#include "pch.h"

#include "Core/Network/BitStream.h"

#include "Engine/Net/NetMessageChannel.h"

#include "EngineTest/GameFramework/Online/ServerBusTestTypes.h"

#include "TestFramework/TestFramework.h"

// 넷 채널의 스키마 해시 핸드셰이크(계획 NetChannelSchemaHashHandshake). 배치가 다른 두 빌드는 필드가 하나 다른 두 타입으로 흉내 낸다.

using namespace sw;

namespace
{
    constexpr uint8 kNoticeKind = 0x81;
} // namespace

/**
 * @brief [NetMessageChannelTest] 배치가 다른 타입을 같은 종류에 등록한 두 채널은 봉인한 와이어 버전이 다르다 — 핸드셰이크가 `VersionMismatch` 로 먼저 거절한다
 */
SW_TEST_CASE( NetMessageChannelTest, DifferentLayoutsSealDifferentWireVersions )
{
    NetHostConfig baseSettings;
    baseSettings._wireVersion = 7;

    NetMessageChannel oldChannel;
    SW_ASSERT_TRUE( oldChannel.registerMessage<BusTestNoticeOld>( kNoticeKind ) );
    NetHostConfig oldSettings;
    SW_ASSERT_TRUE( oldChannel.seal( baseSettings, oldSettings ) );

    NetMessageChannel newChannel;
    SW_ASSERT_TRUE( newChannel.registerMessage<BusTestNoticeNew>( kNoticeKind ) );
    NetHostConfig newSettings;
    SW_ASSERT_TRUE( newChannel.seal( baseSettings, newSettings ) );

    NetMessageChannel sameChannel;
    SW_ASSERT_TRUE( sameChannel.registerMessage<BusTestNoticeOld>( kNoticeKind ) );
    NetHostConfig sameSettings;
    SW_ASSERT_TRUE( sameChannel.seal( baseSettings, sameSettings ) );

    SW_EXPECT_TRUE( oldSettings._wireVersion != baseSettings._wireVersion );
    SW_EXPECT_TRUE( oldSettings._wireVersion != newSettings._wireVersion );
    SW_EXPECT_EQUAL( oldSettings._wireVersion, sameSettings._wireVersion );
}

/**
 * @brief [NetMessageChannelTest] 봉인한 채널은 본문만(스키마 해시 8 바이트 없이) 쓰고 같은 값으로 읽는다. 봉인 전 쓰기 · 봉인 뒤 등록은 거절한다
 */
SW_TEST_CASE( NetMessageChannelTest, ASealedChannelSendsTheBodyOnly )
{
    NetMessageChannel channel;
    SW_ASSERT_TRUE( channel.registerMessage<BusTestNoticeOld>( kNoticeKind ) );
    BusTestNoticeOld notice;
    notice._label     = "kick";
    notice._accountID = 77;
    NetMessageWriter writer;
    {
        test::ScopedLogSuppressor suppressor; // 봉인 전 사용 오류
        SW_EXPECT_FALSE( channel.writeMessage( kNoticeKind, notice, writer ) );
    }

    NetHostConfig settings;
    SW_ASSERT_TRUE( channel.seal( NetHostConfig{}, settings ) );
    {
        test::ScopedLogSuppressor suppressor; // 봉인 뒤 등록 오류
        SW_EXPECT_FALSE( channel.registerMessage<BusTestNoticeNew>( 0x82 ) );
    }
    SW_ASSERT_TRUE( channel.writeMessage( kNoticeKind, notice, writer ) );
    BitWriter withHeader;
    SW_ASSERT_TRUE( FixedLayout::write( notice, withHeader ) );
    SW_EXPECT_EQUAL( withHeader.getByteCount() - 8 + 1, writer.getByteCount() ); // 종류 1 바이트 + 머리 없는 본문

    const vector<uint8>& bytes = writer.getBytes();
    SW_ASSERT_EQUAL( kNoticeKind, bytes[0] );
    BitReader        body( bytes.data() + 1, writer.getByteCount() - 1 );
    BusTestNoticeOld received;
    SW_ASSERT_TRUE( channel.readMessage( kNoticeKind, body, received ) == FixedReadResult::Ok );
    SW_EXPECT_EQUAL( uint64( 77 ), received._accountID );
    SW_EXPECT_TRUE( received._label == "kick" );
}
