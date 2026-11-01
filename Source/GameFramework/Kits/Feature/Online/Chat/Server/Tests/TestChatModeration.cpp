// ChatModerationTest — 채팅 거르기: 도배 방지(토큰 버킷 — 한꺼번에 쓰기와 간격마다 하나, 같은 글 반복은 창 안 상한까지, 정규화한 글이 같으면 같은 글),
// 금칙어(가리기 · 거절, 대소문자 · 전각 정규화, 건너뛸 글자 · 0 폭 글자를 끼워도 걸린다, 빈 낱말은 버림, 잘못된 UTF-8 은 InvalidText),
// 건너뛸 글자 파일(`Config/Server/chat_ignored_characters.txt`)과 깨진 줄 거절. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatSpamGuard.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatTextNormalizer.h"
#include "GameFramework/Kits/Feature/Online/Chat/Server/ChatWordFilter.h"

using namespace sw;

namespace
{
    /** @brief 운영 서버가 쓰는 건너뛸 글자 파일로 만든 정규화입니다. */
    const ChatTextNormalizer& getNormalizer()
    {
        static ChatTextNormalizer s_normalizer;
        static const bool         s_bLoaded = s_normalizer.loadFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Server/chat_ignored_characters.txt" ) );
        SW_EXPECT_TRUE( s_bLoaded );
        return s_normalizer;
    }

    ChatSpamGuard makeGuard( int32 burstCount )
    {
        ChatSpamConfig config;
        config._burstCount       = burstCount;
        config._refillIntervalMs = 1000;
        config._repeatWindowMs   = 10000;
        config._maxRepeatCount   = 2;
        ChatSpamGuard guard;
        guard.initialize( config, getNormalizer() );
        return guard;
    }
} // namespace

/** @brief [ChatModerationTest] 한꺼번에 쓰기 수까지 받고 그다음은 기다릴 시간과 함께 거절하며, 간격이 지나면 하나를 다시 받는다 */
SW_TEST_CASE( ChatModerationTest, SpamGuardLimitsBurstsAndRefillsPerInterval )
{
    ChatSpamGuard guard   = makeGuard( 3 );
    int64         retryMs = 0;
    SW_EXPECT_TRUE( guard.check( 7, "one", 0, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "two", 0, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "three", 0, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "four", 0, retryMs ) == ChatSpamVerdict::RateLimited );
    SW_EXPECT_TRUE( retryMs > 0 && retryMs <= 1000 );
    SW_EXPECT_TRUE( guard.check( 8, "other account", 0, retryMs ) == ChatSpamVerdict::Allowed ); // 버킷은 계정마다다
    SW_EXPECT_TRUE( guard.check( 7, "four", 1000, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "five", 1000, retryMs ) == ChatSpamVerdict::RateLimited );
}

/** @brief [ChatModerationTest] 같은 글(정규화가 같으면 같다)은 창 안에서 상한까지만 받고, 창이 지나거나 계정을 잊으면 다시 받는다 */
SW_TEST_CASE( ChatModerationTest, SpamGuardStopsRepeatsInsideTheWindow )
{
    ChatSpamGuard guard   = makeGuard( 100 );
    int64         retryMs = 0;
    SW_EXPECT_TRUE( guard.check( 7, "hello", 0, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "Hello", 100, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "H.E.L.L.O", 200, retryMs ) == ChatSpamVerdict::Repeated );
    SW_EXPECT_TRUE( guard.check( 7, "hello there", 300, retryMs ) == ChatSpamVerdict::Allowed ); // 다른 글
    SW_EXPECT_TRUE( guard.check( 7, "hello", 10200, retryMs ) == ChatSpamVerdict::Allowed );     // 창이 지났다
    guard.forget( 7 );
    SW_EXPECT_TRUE( guard.check( 7, "hello", 10300, retryMs ) == ChatSpamVerdict::Allowed );
    SW_EXPECT_TRUE( guard.check( 7, "hello", 10400, retryMs ) == ChatSpamVerdict::Allowed );
}

/** @brief [ChatModerationTest] 반복 해시는 대소문자 · 전각 · 건너뛸 글자(구두점 · 0 폭 글자)를 정규화한다 */
SW_TEST_CASE( ChatModerationTest, TextHashNormalizesCaseWidthAndIgnoredCharacters )
{
    const ChatTextNormalizer& normalizer = getNormalizer();
    const uint64              plain      = normalizer.computeTextHash( "hello" );
    SW_EXPECT_EQUAL( plain, normalizer.computeTextHash( "HeLLo" ) );
    SW_EXPECT_EQUAL( plain, normalizer.computeTextHash( "\xEF\xBD\x88\xEF\xBD\x85\xEF\xBD\x8C\xEF\xBD\x8C\xEF\xBD\x8F" ) ); // 전각 ｈｅｌｌｏ
    SW_EXPECT_EQUAL( plain, normalizer.computeTextHash( "h-e l_l.o" ) );
    SW_EXPECT_EQUAL( plain, normalizer.computeTextHash( "hel\xE2\x80\x8Blo" ) ); // U+200B 0 폭 공백
    SW_EXPECT_TRUE( plain != normalizer.computeTextHash( "help" ) );
}

/** @brief [ChatModerationTest] 금칙어는 가리기면 걸린 글자만 '*' 로, 거절이면 글 전체를 거절하고, 끼운 글자 · 0 폭 글자로 비켜 가지 못한다 */
SW_TEST_CASE( ChatModerationTest, WordFilterMasksOrRejectsNormalizedMatches )
{
    ChatWordFilter mask;
    SW_EXPECT_EQUAL( 2, mask.initialize( { "bad", "", "\xEC\x9A\x95" }, ChatFilterMode::Mask, getNormalizer() ) ); // 빈 낱말은 버린다, 욕
    string out;
    SW_EXPECT_TRUE( mask.apply( "this is BAD!", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_EQUAL( string( "this is ***!" ), out );
    SW_EXPECT_TRUE( mask.apply( "\xEF\xBD\x82\xEF\xBD\x81\xEF\xBD\x84", out ) == ChatFilterVerdict::Masked ); // 전각 ｂａｄ
    SW_EXPECT_TRUE( mask.apply( "b.a.d", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_EQUAL( string( "*****" ), out );
    SW_EXPECT_TRUE( mask.apply( "b\xE2\x80\x8B"
                                "a\xE2\x80\x8D"
                                "d",
                                out ) == ChatFilterVerdict::Masked ); // ZWSP · ZWJ 를 끼웠다
    SW_EXPECT_TRUE( mask.apply( "a \xEC\x9A\x95 b", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_EQUAL( string( "a * b" ), out );
    SW_EXPECT_TRUE( mask.apply( "good game", out ) == ChatFilterVerdict::Clean );
    SW_EXPECT_EQUAL( string( "good game" ), out );
    SW_EXPECT_TRUE( mask.apply( "bro\xFFken", out ) == ChatFilterVerdict::InvalidText );

    ChatWordFilter reject;
    SW_EXPECT_EQUAL( 1, reject.initialize( { "bad" }, ChatFilterMode::Reject, getNormalizer() ) );
    SW_EXPECT_TRUE( reject.apply( "so Bad", out ) == ChatFilterVerdict::Rejected );
    SW_EXPECT_TRUE( reject.apply( "fine", out ) == ChatFilterVerdict::Clean );
}

/** @brief [ChatModerationTest] 건너뛸 글자 파일은 `U+XXXX` 줄만 받는다 — 틀린 줄 · 없는 파일은 읽지 않는다(목록은 빈다) */
SW_TEST_CASE( ChatModerationTest, IgnoredCharacterFileRejectsBadLines )
{
    const string folder = test::makeScratchPath( "ChatIgnored" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );
    const string path = FileUtil::joinPath( folder, "ignored.txt" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "# comment\nU+002E\n.\n" ) );
    ChatTextNormalizer normalizer;
    SW_EXPECT_LOG( "ChatTextNormalizer.BadLine" );
    SW_EXPECT_FALSE( normalizer.loadFile( path ) );
    SW_EXPECT_EQUAL( uint32( '.' ), normalizer.normalize( '.' ) ); // 실패한 읽기는 아무것도 건너뛰지 않는다
    SW_EXPECT_LOG( "File.NotFound" );
    SW_EXPECT_LOG( "ChatTextNormalizer.CannotRead" );
    SW_EXPECT_FALSE( normalizer.loadFile( FileUtil::joinPath( folder, "missing.txt" ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( path, "U+002E\n" ) );
    SW_REQUIRE_TRUE( normalizer.loadFile( path ) );
    SW_EXPECT_EQUAL( uint32( 0 ), normalizer.normalize( '.' ) );
    SW_EXPECT_EQUAL( uint32( 'a' ), normalizer.normalize( 'A' ) );
}
