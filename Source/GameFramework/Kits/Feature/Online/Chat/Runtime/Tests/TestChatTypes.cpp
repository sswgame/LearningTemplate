// ChatTypesTest — 채널 id 규칙: 접두어마다의 종류, 이름 없는 접두어 · 모르는 접두어 · 글자 · 길이 거절, 만든 id(길드 · 파티 · 귓속말)가 규칙 안이고 화면이 드는 채팅 기록(`ChatLog`)의 약한 참조도 본다.
// 귓속말 키는 두 계정의 순서와 무관하다. 서버는 받은 채널 id 를 이 함수로 거른다(ChatService).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatLog.h"
#include "GameFramework/Kits/Feature/Online/Chat/Runtime/ChatTypes.h"

using namespace sw;

/** @brief [ChatTypesTest] 접두어가 종류를 정하고, 접두어만 있는 id · 모르는 접두어 · 허락하지 않은 글자 · 상한 + 1 은 거절한다 */
SW_TEST_CASE( ChatTypesTest, ParseKindReadsThePrefixAndRejectsTheRest )
{
    // 틀리면: 접두어 대응이 어긋나면 종류 단언이, 이름 길이 · 글자 · 상한 검사 중 하나를 빼면 해당 거짓 단언이 진다.
    ChatChannelKind kind = ChatChannelKind::Count;
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( "world.kr_1", kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::World );
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( "custom.trade", kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Custom );
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( "whisper.x", kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Whisper );

    kind = ChatChannelKind::Count;
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "world.", kind ) );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "team.red", kind ) );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "World.kr", kind ) );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "world.kr-1", kind ) );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( "", kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Count ); // 거절은 종류를 쓰지 않는다

    string longest{ "world." };
    longest.append( static_cast<size_t>( ChatLimit::kMaxChannelIDSize ) - longest.size(), 'a' );
    SW_EXPECT_TRUE( ChatChannelID::parseKind( longest, kind ) );
    longest.push_back( 'a' );
    SW_EXPECT_FALSE( ChatChannelID::parseKind( longest, kind ) );
}

/** @brief [ChatTypesTest] 만든 id 는 그 종류로 다시 읽히고 상한 안이며, 귓속말 키는 두 계정의 순서와 무관하다 */
SW_TEST_CASE( ChatTypesTest, MadeIDsParseBackAsTheirKind )
{
    // 틀리면: 만들기가 상한을 넘는 id 를 내면(가장 큰 id) parseKind 가 거절해 REQUIRE 가, 귓속말 키가 순서를 따르면 같음 단언이 진다.
    ChatChannelKind kind = ChatChannelKind::Count;
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( ChatChannelID::makeGuild( ~0ull ), kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Guild );
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( ChatChannelID::makeParty( 1 ), kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Party );

    const string whisper = ChatChannelID::makeWhisper( ~0ull, 2 );
    SW_EXPECT_EQUAL( whisper, ChatChannelID::makeWhisper( 2, ~0ull ) );
    SW_REQUIRE_TRUE( ChatChannelID::parseKind( whisper, kind ) );
    SW_EXPECT_TRUE( kind == ChatChannelKind::Whisper );
    SW_EXPECT_TRUE( whisper != ChatChannelID::makeWhisper( 2, 3 ) );
}

/** @brief [ChatTypesTest] 화면이 든 약한 참조는 모델이 사라지면 빈다 — 화면(엔진 UI 소유)이 게임 쪽 모델보다 오래 살아도 사라진 모델을 부르지 않는다 */
SW_TEST_CASE( ChatTypesTest, ScreenReferenceEmptiesWhenTheModelIsDestroyed )
{
    WeakRef<ChatLog> reference;
    {
        ChatLog model;
        reference = WeakRef<ChatLog>{ model };
        SW_EXPECT_TRUE( reference.get() == &model );
    }
    SW_EXPECT_NULL( reference.get() );
}
