#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatTypes.h"

#include "TestFramework/TestFramework.h"

// 채널 id 규칙 — 접두마다 종류, 규칙 밖 글자 · 빈 이름 · 길이 상한 거절, 길드 · 파티 · 귓속말 id 만들기(귓속말은 두 계정의 순서와 무관).

using namespace sw;

SW_TEST_CASE( ChatChannelIDTest, ParsesKindsAndRejectsOutsideTheRule )
{
    ChatChannelKind kind = ChatChannelKind::Count;
    SW_EXPECT_TRUE( ChatChannelID::parseKind( "world.global", kind ) && kind == ChatChannelKind::World );
    SW_EXPECT_TRUE( ChatChannelID::parseKind( "custom.trade_1", kind ) && kind == ChatChannelKind::Custom );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "world.", kind ) );       // 이름이 비었다
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "World.global", kind ) ); // 대문자
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "lobby.one", kind ) );    // 모르는 접두
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "", kind ) );
    string tooLong( "custom." );
    while ( static_cast<int32>( tooLong.size() ) <= ChatLimit::kMaxChannelIDSize )
    {
        tooLong += 'a';
    }
    SW_EXPECT_FALSE( ChatChannelID::parseKind( tooLong, kind ) );
}

SW_TEST_CASE( ChatChannelIDTest, MakesGuildPartyAndOrderFreeWhisperIDs )
{
    ChatChannelKind kind  = ChatChannelKind::Count;
    const string    guild = ChatChannelID::makeGuild( 0x2A );
    SW_EXPECT_STREQ( guild.c_str(), "guild.000000000000002a" );
    SW_EXPECT_TRUE( ChatChannelID::parseKind( guild, kind ) && kind == ChatChannelKind::Guild );
    SW_EXPECT_TRUE( ChatChannelID::parseKind( ChatChannelID::makeParty( 7 ), kind ) && kind == ChatChannelKind::Party );

    const string whisper = ChatChannelID::makeWhisper( 9, 3 );
    SW_EXPECT_STREQ( whisper.c_str(), ChatChannelID::makeWhisper( 3, 9 ).c_str() );
    SW_EXPECT_STREQ( whisper.c_str(), "whisper.0000000000000003.0000000000000009" );
    SW_EXPECT_TRUE( ChatChannelID::parseKind( whisper, kind ) && kind == ChatChannelKind::Whisper );
    SW_EXPECT_STREQ( toString( ChatResult::RateLimited ), "RateLimited" );
}
