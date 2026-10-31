#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Localization/TranslationMemory.h"

// TranslationMemoryTest — 번역 메모리의 일치: 같은 원문은 그대로, 정규화가 같거나 비슷한 원문은 근사 일치, 너무 다르면 없음. 유사도는 글자 단위, XML 왕복(nogpu).

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
    SW_REQUIRE_TRUE( memory.findBestMatch( "Open the door", match ) );
    SW_EXPECT_TRUE( match._bExact );

    SW_REQUIRE_TRUE( memory.findBestMatch( "open  the door.", match ) ); // 대소문자 · 공백 · 끝 문장부호
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_TRUE( match._score > 0.95f );

    SW_REQUIRE_TRUE( memory.findBestMatch( "Save your progress now?", match ) );
    SW_EXPECT_FALSE( match._bExact );
    SW_EXPECT_EQUAL( sw::string( "진행 상황을 저장할까요?" ), match._text );

    SW_EXPECT_FALSE( memory.findBestMatch( "Completely different words", match ) );
    SW_EXPECT_NEAR( 0.0f, sw::TranslationMemory::computeSimilarity( "abc", "xyz" ), 0.001 );
    SW_EXPECT_NEAR( 0.75f, sw::TranslationMemory::computeSimilarity( "가나다라", "가나다마" ), 0.001 ); // 글자 단위(바이트가 아니다)
    SW_EXPECT_EQUAL( sw::string( "Open the door" ), memory.findSourceOfText( "문을 연다" ) );

    sw::TranslationMemory reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromXMLText( memory.toXMLText(), "tm" ) );
    SW_EXPECT_EQUAL( size_t( 2 ), reloaded.getEntryCount() );
    SW_EXPECT_EQUAL( memory.getCulture(), reloaded.getCulture() );
}
