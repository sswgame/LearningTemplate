// CursorShapeTest — 엔진 커서 모양을 브라우저 CSS cursor 이름으로 옮기는 표(nogpu). 웹 창(`WebWindow::setCursorShape`)이 캔버스 style.cursor 에 이 이름을 건다.
#include "pch.h"

#include "Core/Container/string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Window/CursorShape.h"

using namespace sw;

/**
 * @brief [CursorShapeTest] 모양마다 CSS 표준 커서 이름이 있다(https://developer.mozilla.org/docs/Web/CSS/cursor). 숨김은 "none" 이다.
 *        예전에는 웹 창이 커서 모양 · 숨기기를 하지 않았다 — 글 칸 위에서도 화살표였고, 커서를 숨겨도 보였다.
 */
SW_TEST_CASE( CursorShapeTest, EveryShapeHasACSSName )
{
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::Arrow ) ) == "default" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::Hand ) ) == "pointer" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::TextBeam ) ) == "text" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::ResizeHorizontal ) ) == "ew-resize" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::ResizeVertical ) ) == "ns-resize" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::toCSSName( CursorShape::NotAllowed ) ) == "not-allowed" );
    SW_EXPECT_TRUE( string_view( CursorShapeUtil::kHiddenCSSName ) == "none" );
}
