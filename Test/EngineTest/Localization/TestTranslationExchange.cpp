#include "pch.h"

#include "Engine/Localization/PortableObjectFile.h"
#include "Engine/Localization/TranslationMemory.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [TranslationMemoryTest] 정확히 같은 원문 → 그대로, 정규화가 같거나 비슷한 원문 → 근사 일치, 너무 다르면 없음
 */
SW_TEST_CASE( TranslationMemoryTest, ExactNormalizedAndFuzzyMatches )
{
    sw::TranslationMemory memory;
    memory.setCulture( "ko" );
    SW_EXPECT_TRUE( memory.addPair( "Open the door", "문을 연다" ) );
    SW_EXPECT_FALSE( memory.addPair( "Open the door", "문을 연다" ) ); // 같은 쌍은 바뀐 것이 아니다
    (void)memory.addPair( "Save your progress?", "진행 상황을 저장할까요?" );

    sw::TranslationMemoryMatch match;
    SW_ASSERT_TRUE( memory.findBestMatch( "Open the door", match ) );
    SW_EXPECT_TRUE( match._bExact );

    SW_ASSERT_TRUE( memory.findBestMatch( "open  the door.", match ) ); // 대소문자 · 공백 · 끝 문장부호
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_TRUE( match._score > 0.95f );

    SW_ASSERT_TRUE( memory.findBestMatch( "Save your progress now?", match ) );
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_STREQ( "진행 상황을 저장할까요?", match._text.c_str() );

    SW_EXPECT_FALSE( memory.findBestMatch( "Completely different words", match ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, sw::TranslationMemory::computeSimilarity( "abc", "xyz" ), 0.001f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, sw::TranslationMemory::computeSimilarity( "가나다라", "가나다마" ), 0.001f ); // 글자 단위(바이트가 아니다)
    SW_EXPECT_STREQ( "Open the door", memory.findSourceOfText( "문을 연다" ).c_str() );

    sw::TranslationMemory reloaded;
    SW_ASSERT_TRUE( reloaded.loadFromJSONText( memory.toJSONText(), "tm" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), reloaded.getEntryCount() );
}

/**
 * @brief [PortableObjectTest] PO 왕복 — 머리 · 번역가 메모 · 추출 설명 · 자리 · fuzzy · 옛 원문 · 이스케이프 · 여러 줄 글을 지킨다
 */
SW_TEST_CASE( PortableObjectTest, RoundTripKeepsCommentsAndFlags )
{
    sw::PortableObjectFile file;
    file._language                 = "ko";
    file._projectName              = "test";
    sw::PortableObjectEntry& entry = file._listEntry.emplace_back();
    entry._context                 = "menu.start";
    entry._source                  = "Say \"hi\"\nto {name}";
    entry._translation             = "{name}에게\n\"안녕\" 하고 말하기";
    entry._translatorComment       = "줄 1\n줄 2";
    entry._listExtractedComment    = { "Context: button", "Max length: 12" };
    entry._listReference           = { "Menu.cpp" };
    entry._previousSource          = "Say hi";
    entry._bFuzzy                  = true;

    const sw::string       text = file.toText();
    sw::PortableObjectFile parsed;
    sw::string             error;
    SW_ASSERT_TRUE_MSG( parsed.parse( text, &error ), error.c_str() );
    SW_EXPECT_STREQ( "ko", parsed._language.c_str() );
    SW_EXPECT_STREQ( "test", parsed._projectName.c_str() );
    SW_ASSERT_EQUAL( size_t( 1 ), parsed._listEntry.size() );
    const sw::PortableObjectEntry& back = parsed._listEntry[0];
    SW_EXPECT_STREQ( entry._source.c_str(), back._source.c_str() );
    SW_EXPECT_STREQ( entry._translation.c_str(), back._translation.c_str() );
    SW_EXPECT_STREQ( entry._translatorComment.c_str(), back._translatorComment.c_str() );
    SW_EXPECT_STREQ( "Say hi", back._previousSource.c_str() );
    SW_EXPECT_TRUE( back._bFuzzy );
    SW_EXPECT_EQUAL( size_t( 2 ), back._listExtractedComment.size() );

    // 도구가 쓰는 여러 줄 msgid(빈 첫 줄 + 이어지는 줄)도 읽는다.
    SW_ASSERT_TRUE( parsed.parse( "msgid \"\"\nmsgstr \"Language: ja\\n\"\n\nmsgctxt \"a\"\nmsgid \"\"\n\"Hello \"\n\"world\"\nmsgstr \"\"\n\"こんにちは\"\n", &error ) );
    SW_EXPECT_STREQ( "Hello world", parsed._listEntry[0]._source.c_str() );
    SW_EXPECT_STREQ( "こんにちは", parsed._listEntry[0]._translation.c_str() );
    SW_EXPECT_FALSE( parsed.parse( "msgid \"a\"\nmsgid_plural \"b\"\nmsgstr[0] \"x\"\n", &error ) );
}
