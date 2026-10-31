// DebugOverlayState — 그릴 줄은 키 사전순이고 float 은 소수 둘째 자리, 빈 문자열은 빠진다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/DebugOverlayState.h"

using namespace sw;

/**
 * @brief [DebugOverlayStateTest] 그릴 줄은 키 사전순이고 float 은 소수 둘째 자리, 빈 문자열은 빠진다
 * @details 에디터 게임 뷰가 이 줄을 그대로 그린다. 순서가 맵 순서를 따르면 프레임마다 줄이 뒤섞여 읽을 수 없다.
 */
SW_TEST_CASE( DebugOverlayStateTest, RowsAreSortedAndFormatted )
{
    DebugOverlayState overlay;
    overlay.setFloat( "hud.fade", 0.5f );
    overlay.setString( "hud.dialogue", "Hello" );
    overlay.setFloat( "arena.wave", 3.0f );
    overlay.setString( "empty", "" );

    vector<DebugOverlayRow> listRow;
    overlay.collectRows( listRow );
    SW_REQUIRE_EQUAL( size_t( 3 ), listRow.size() );
    SW_EXPECT_EQUAL( string( "arena.wave" ), listRow[0]._key );
    SW_EXPECT_EQUAL( string( "3.00" ), listRow[0]._value );
    SW_EXPECT_EQUAL( string( "hud.dialogue" ), listRow[1]._key );
    SW_EXPECT_EQUAL( string( "Hello" ), listRow[1]._value );
    SW_EXPECT_EQUAL( string( "hud.fade" ), listRow[2]._key );
    SW_EXPECT_EQUAL( string( "0.50" ), listRow[2]._value );

    overlay.remove( "hud.fade" );
    overlay.remove( "hud.dialogue" );
    overlay.collectRows( listRow );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRow.size() );
    SW_EXPECT_EQUAL( string( "arena.wave" ), listRow[0]._key );
}
