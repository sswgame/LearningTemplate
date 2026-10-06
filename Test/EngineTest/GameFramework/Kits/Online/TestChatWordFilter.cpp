#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"

#include "GameFramework/Kits/Online/Server/Chat/ChatWordFilter.h"

#include "TestFramework/TestFramework.h"

// 금칙어 거르개 — 가리기 · 대소문자 · 끼움 글자 · 전각 · 겹치는 낱말 · 실패 고리로만 보이는 짧은 낱말 · 한글 낱말 · 거절 방식 · 잘못된 UTF-8 ·
// 걸리지 않는 글은 그대로 · 저장소의 시험 낱말 파일.

using namespace sw;

namespace
{
    vector<string> makeWordList()
    {
        return vector<string>{ "bad", "badword", "word", "\xEB\x82\x98\xEC\x81\x9C" /* 나쁜 */ };
    }
} // namespace

SW_TEST_CASE( ChatWordFilterTest, MasksMatchesIgnoringCaseSeparatorsAndFullWidth )
{
    ChatWordFilter filter;
    SW_ASSERT_EQUAL( filter.initialize( makeWordList(), ChatFilterMode::Mask ), 4 );
    string out;
    SW_EXPECT_TRUE( filter.apply( "this is BAD", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_STREQ( out.c_str(), "this is ***" );
    SW_EXPECT_TRUE( filter.apply( "b.a.d!", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_STREQ( out.c_str(), "*****!" );                                                                   // 구간 안의 끼움 글자도 가린다, 뒤의 '!' 는 구간 밖
    SW_EXPECT_TRUE( filter.apply( "\xEF\xBC\xA2\xEF\xBC\xA1\xEF\xBC\xA4", out ) == ChatFilterVerdict::Masked ); // 전각 "ＢＡＤ"
    SW_EXPECT_STREQ( out.c_str(), "***" );
}

SW_TEST_CASE( ChatWordFilterTest, OverlappingAndHangulWords )
{
    ChatWordFilter filter;
    SW_ASSERT_EQUAL( filter.initialize( makeWordList(), ChatFilterMode::Mask ), 4 );
    string out;
    SW_EXPECT_TRUE( filter.apply( "badwordy", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_STREQ( out.c_str(), "*******y" );                                                                   // bad · badword · word 셋이 겹친다
    SW_EXPECT_TRUE( filter.apply( "\xEB\x82\x98 \xEC\x81\x9C \xEB\xA7\x90", out ) == ChatFilterVerdict::Masked ); // "나 쁜 말"
    SW_EXPECT_STREQ( out.c_str(), "*** \xEB\xA7\x90" );                                                           // 나 · 공백 · 쁜 → 셋, 뒤의 " 말" 은 그대로

    ChatWordFilter suffixFilter; // 긴 낱말의 길 위에 있는 짧은 낱말 — 실패 고리에서 물려받아야 잡힌다
    SW_ASSERT_EQUAL( suffixFilter.initialize( vector<string>{ "abcd", "bc" }, ChatFilterMode::Mask ), 2 );
    SW_EXPECT_TRUE( suffixFilter.apply( "abcx", out ) == ChatFilterVerdict::Masked );
    SW_EXPECT_STREQ( out.c_str(), "a**x" );
}

SW_TEST_CASE( ChatWordFilterTest, CleanTextRejectModeAndInvalidUtf8 )
{
    ChatWordFilter masking;
    SW_ASSERT_EQUAL( masking.initialize( makeWordList(), ChatFilterMode::Mask ), 4 );
    string out;
    SW_EXPECT_TRUE( masking.apply( "hello world", out ) == ChatFilterVerdict::Clean ); // w-o-r-l-d 는 word 가 아니다
    SW_EXPECT_STREQ( out.c_str(), "hello world" );
    SW_EXPECT_TRUE( masking.apply( "abc\xC3", out ) == ChatFilterVerdict::InvalidText );
    SW_EXPECT_TRUE( masking.apply( "ok \xEF\xBF\xBD", out ) == ChatFilterVerdict::Clean ); // 글에 그대로 적힌 U+FFFD 는 잘못된 UTF-8 이 아니다

    ChatWordFilter rejecting;
    SW_ASSERT_EQUAL( rejecting.initialize( makeWordList(), ChatFilterMode::Reject ), 4 );
    out = "untouched";
    SW_EXPECT_TRUE( rejecting.apply( "so bad", out ) == ChatFilterVerdict::Rejected );
    SW_EXPECT_STREQ( out.c_str(), "untouched" );

    ChatWordFilter ignored; // 빈 낱말 · 끼움 글자만인 낱말은 버린다
    SW_EXPECT_EQUAL( ignored.initialize( vector<string>{ "", " . ", "ok" }, ChatFilterMode::Mask ), 1 );
}

SW_TEST_CASE( ChatWordFilterTest, RepositoryWordFileLoads )
{
    const string   path = FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), "Config/Server/chat_banned_words.txt" );
    ChatWordFilter filter;
    SW_ASSERT_TRUE_MSG( filter.loadFile( path, ChatFilterMode::Mask ), path.c_str() );
    SW_EXPECT_EQUAL( filter.getWordCount(), 2 ); // 주석 줄은 낱말이 아니다
    string out;
    SW_EXPECT_TRUE( filter.apply( "N-a-s-t-y T-e-r-m", out ) == ChatFilterVerdict::Masked );

    SW_TEST_DEFENSIVE_SCOPE( "missing word file reads as an empty list" );
    ChatWordFilter missing;
    SW_EXPECT_FALSE( missing.loadFile( FileUtil::joinPath( ResourceUtil::getProjectFolderPath(), "Config/Server/no_such_words.txt" ), ChatFilterMode::Mask ) );
    SW_EXPECT_EQUAL( missing.getWordCount(), 0 );
}
