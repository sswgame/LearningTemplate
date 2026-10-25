#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/Utf8Util.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

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
