#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/Utf8Util.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /**
     * @brief 검증기(`isValidUtf8`)와 디코더(`tryDecodeUtf8`)와 UCS-2 변환이 @p bytes 에 같은 판정을 내는가입니다.
     * @details 검증기가 true 면 디코더가 끝까지 오류 없이 소비해야 하고, UCS-2 의 U+FFFD 개수는 (잘못된 시퀀스 수 + 올바른 U+FFFD · 기본 평면 밖 글자 수)와 같아야 한다.
     */
    bool agree( const string& bytes )
    {
        size_t offset      = 0;
        size_t replaceable = 0;
        bool   bAllValid   = true;
        while ( offset < bytes.size() )
        {
            uint32     codepoint           = 0;
            const bool bValid              = StringUtil::tryDecodeUtf8( bytes, offset, codepoint );
            bAllValid                      = bAllValid && bValid;
            const bool bBecomesReplacement = codepoint == 0xFFFD || codepoint > 0xFFFF; // 올바른 U+FFFD 와 기본 평면 밖 글자도 U+FFFD 로 나온다
            if ( bValid == false || bBecomesReplacement )
                ++replaceable;
        }
        vector<uint16> listUnit;
        StringUtil::utf8ToUcs2( bytes, listUnit );
        size_t replacementCount = 0;
        for ( const uint16 unit : listUnit )
        {
            if ( unit == 0xFFFD )
                ++replacementCount;
        }
        return StringUtil::isValidUtf8( bytes.c_str() ) == bAllValid && replacementCount == replaceable;
    }

    /** @brief @p bytes 와 그 모든 앞부분(잘린 시퀀스)이 일치하는지 세어 어긋난 개수를 돌려줍니다. */
    size_t countDisagreementsWithPrefixes( const string& bytes )
    {
        size_t count = 0;
        for ( size_t length = 1; length <= bytes.size(); ++length )
        {
            count += agree( bytes.substr( 0, length ) ) ? 0u : 1u;
        }
        return count;
    }
} // namespace

/**
 * @brief [Utf8UtilTest] 검증기와 디코더와 UCS-2 변환은 모든 1 · 2 바이트 입력과 3 · 4 바이트 경계값(overlong · 서로게이트 · 상한 초과 · 잘림)에서 같은 판정을 낸다
 */
SW_TEST_CASE( Utf8UtilTest, ValidatorAndDecoderAgreeOnEverySmallInputAndBoundary )
{
    size_t disagreements = 0;
    for ( uint32 first = 1; first < 256; ++first )
    {
        string one( 1, static_cast<utf8>( first ) );
        disagreements += agree( one ) ? 0u : 1u;
        for ( uint32 second = 1; second < 256; ++second )
        {
            string two = one;
            two += static_cast<utf8>( second );
            disagreements += agree( two ) ? 0u : 1u;
        }
    }

    const uint8 arrThreeLead[]    = { 0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF };
    const uint8 arrFourLead[]     = { 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xF7, 0xF8 };
    const uint8 arrSecond[]       = { 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0 };
    const uint8 arrContinuation[] = { 0x7F, 0x80, 0xBF, 0xC0 };
    for ( const uint8 lead : arrThreeLead )
    {
        for ( const uint8 second : arrSecond )
        {
            for ( const uint8 third : arrContinuation )
            {
                string bytes;
                bytes += static_cast<utf8>( lead );
                bytes += static_cast<utf8>( second );
                bytes += static_cast<utf8>( third );
                disagreements += countDisagreementsWithPrefixes( bytes );
            }
        }
    }
    for ( const uint8 lead : arrFourLead )
    {
        for ( const uint8 second : arrSecond )
        {
            for ( const uint8 third : arrContinuation )
            {
                for ( const uint8 fourth : arrContinuation )
                {
                    string bytes;
                    bytes += static_cast<utf8>( lead );
                    bytes += static_cast<utf8>( second );
                    bytes += static_cast<utf8>( third );
                    bytes += static_cast<utf8>( fourth );
                    disagreements += countDisagreementsWithPrefixes( bytes );
                }
            }
        }
    }
    SW_EXPECT_EQUAL( disagreements, size_t{ 0 } );

    // RFC 3629: 서로게이트(ED A0 80), overlong(C0 80 · E0 80 80), U+110000(F4 90 80 80)은 올바르지 않고 U+10FFFF(F4 8F BF BF)는 올바르다.
    SW_EXPECT_FALSE( StringUtil::isValidUtf8( "\xED\xA0\x80" ) );
    SW_EXPECT_FALSE( StringUtil::isValidUtf8( "\xC0\x80" ) );
    SW_EXPECT_FALSE( StringUtil::isValidUtf8( "\xE0\x80\x80" ) );
    SW_EXPECT_FALSE( StringUtil::isValidUtf8( "\xF4\x90\x80\x80" ) );
    SW_EXPECT_TRUE( StringUtil::isValidUtf8( "\xF4\x8F\xBF\xBF" ) );
}

/**
 * @brief [Utf8UtilTest] 연속 바이트 판정은 10xxxxxx 만 true 이고, 선두 바이트 · ASCII · 잘못된 바이트는 false 다
 */
SW_TEST_CASE( Utf8UtilTest, ContinuationIsOnlyTheTenBitPattern )
{
    SW_EXPECT_TRUE( Utf8Util::isContinuation( static_cast<utf8>( 0x80 ) ) );
    SW_EXPECT_TRUE( Utf8Util::isContinuation( static_cast<utf8>( 0xBF ) ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( 'a' ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( static_cast<utf8>( 0xC3 ) ) );
    SW_EXPECT_FALSE( Utf8Util::isContinuation( static_cast<utf8>( 0xF0 ) ) );
}

/**
 * @brief [Utf8UtilTest] 글자 한가운데 자르는 위치는 글자 시작으로 물러나고, 경계 · 끝 · 잘못된 연속 바이트 열에서는 멈춘다
 */
SW_TEST_CASE( Utf8UtilTest, BackOffStopsAtCharacterStarts )
{
    const string text = "a가😀b"; // 1 + 3 + 4 + 1 바이트
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 0 ), size_t{ 0 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 1 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 2 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 3 ), size_t{ 1 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 4 ), size_t{ 4 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 7 ), size_t{ 4 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 8 ), size_t{ 8 } );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 9 ), text.size() );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( text, 100 ), text.size() );

    // 연속 바이트만 이어지는 잘못된 글은 세 칸까지만 물러난다(끝없이 거슬러 가지 않는다).
    const string garbage( 8, static_cast<utf8>( 0x80 ) );
    SW_EXPECT_EQUAL( StringUtil::floorToUtf8Boundary( garbage, 6 ), size_t{ 3 } );
}

/**
 * @brief [Utf8UtilTest] 글자 하나 디코드는 올바른 글자만 true 로 읽고, 정확히 한 글자인 글만 단일로 인정한다
 */
SW_TEST_CASE( Utf8UtilTest, DecodeReportsValidityAndSingleCharacters )
{
    uint32 codepoint = 0;
    SW_EXPECT_TRUE( StringUtil::tryDecodeSingleCodepoint( "가", codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 0xAC00 } );
    SW_EXPECT_TRUE( StringUtil::tryDecodeSingleCodepoint( "😀", codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 0x1F600 } );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( "", codepoint ) );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( "가a", codepoint ) );
    SW_EXPECT_FALSE( StringUtil::tryDecodeSingleCodepoint( string_view( "\xE3\x81", 2 ), codepoint ) ); // 잘린 3 바이트 글자

    size_t offset = 0;
    SW_EXPECT_FALSE( StringUtil::tryDecodeUtf8( "\x80z", offset, codepoint ) ); // 선두가 될 수 없는 바이트
    SW_EXPECT_EQUAL( codepoint, uint32{ 0xFFFD } );
    SW_EXPECT_EQUAL( offset, size_t{ 1 } );
    SW_EXPECT_TRUE( StringUtil::tryDecodeUtf8( "\x80z", offset, codepoint ) );
    SW_EXPECT_EQUAL( codepoint, uint32{ 'z' } );
}

/**
 * @brief [Utf8UtilTest] UCS-2 변환은 기본 평면 글자를 그대로 두고, 평면 밖 글자와 잘못된 바이트를 U+FFFD 한 글자로 바꾼다
 */
SW_TEST_CASE( Utf8UtilTest, Ucs2ReplacesWhatItCannotHold )
{
    vector<uint16> listUnit;
    StringUtil::utf8ToUcs2( string( "a가😀" ) + string( 1, static_cast<utf8>( 0x80 ) ) + "b", listUnit );
    SW_ASSERT_EQUAL( listUnit.size(), size_t{ 5 } );
    SW_EXPECT_EQUAL( listUnit[0], uint16{ 'a' } );
    SW_EXPECT_EQUAL( listUnit[1], uint16{ 0xAC00 } );
    SW_EXPECT_EQUAL( listUnit[2], uint16{ 0xFFFD } );
    SW_EXPECT_EQUAL( listUnit[3], uint16{ 0xFFFD } );
    SW_EXPECT_EQUAL( listUnit[4], uint16{ 'b' } );
}
