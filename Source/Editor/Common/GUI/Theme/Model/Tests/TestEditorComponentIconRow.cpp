// EditorComponentIconRowTest — 글리프 글(UTF-8 한 글자)의 코드 포인트: 아이콘 표의 기본 다국어 평면만 받고 틀린 글은 0 이다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorComponentIconRowTest] ASCII · 사설 영역 글리프는 그 코드 포인트, 널 · 빈 글 · 잘린 글 · 기본 평면 밖은 0 이다 */
SW_TEST_CASE( EditorComponentIconRowTest, DecodesOnlyBasicPlaneGlyphs )
{
    SW_EXPECT_EQUAL( uint32{ 'A' }, EditorComponentIconRow::decodeGlyph( "A" ) );
    SW_EXPECT_EQUAL( uint32{ 0xF013 }, EditorComponentIconRow::decodeGlyph( "\xEF\x80\x93" ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, EditorComponentIconRow::decodeGlyph( nullptr ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, EditorComponentIconRow::decodeGlyph( "" ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, EditorComponentIconRow::decodeGlyph( "\xEF\x80" ) );         // 잘린 글
    SW_EXPECT_EQUAL( uint32{ 0 }, EditorComponentIconRow::decodeGlyph( "\xF0\x9F\x98\x80" ) ); // 기본 평면 밖
}
