/**
 * @file EditorIconGlyphs.h
 * @brief 에디터 아이콘을 이름으로 찾습니다. 이름 → 코드포인트는 데이터(`editor/fonts/sweditoricons.glyphs.xml`, `EditorIconGlyphTable`)이고 글꼴과 짝입니다.
 * @details 코드는 이름만 안다 — 글꼴을 바꾸면 표만 바꾼다. 테마 색(`SW_THEMECOLOR`)과 같은 모양이다: 표에 없는 이름은 Missing 아이콘을 그리고 이름마다 한 번 오류를 남기며,
 *          단위 테스트(`EditorIconNames`)가 소스의 이름 리터럴이 모두 표에 있는지 본다. 등록 줄 · 상수 표는 글리프가 아니라 이름(`SW_EDITORICON_NAME`)을 들고
 *          그릴 때 찾는다(`EditorIconUtil::findGlyphByName`) — 정적 초기화는 표를 읽기 전이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw::editor
{
    /** @brief 아이콘 이름 → 글리프입니다. 표는 `EditorTheme` 서브시스템이 들고, 글꼴을 세울 때 읽는다(`EditorFontSetup`). */
    struct SW_EDITOR_API EditorIconUtil
    {
        /** @brief 이름의 글리프(UTF-8)입니다. 표에 없으면 Missing 아이콘이고 오류를 남깁니다(이름마다 한 번). 늘 nullptr 이 아닙니다. */
        static const utf8* findGlyph( const hashed_string& name );
        /** @brief 데이터 · 등록 줄이 든 이름으로 찾습니다(`findGlyph` 와 같은 규칙). @p pName 이 nullptr 이면 nullptr(아이콘 없음)입니다. */
        static const utf8* findGlyphByName( const utf8* pName );
        /** @brief 표에 그 이름이 있는지 봅니다(검사 · 메타 확인용 — 오류를 남기지 않는다). */
        static bool hasIcon( string_view name );
    };
} // namespace sw::editor

/** @brief 아이콘 이름 리터럴입니다. 등록 줄 · 상수 표처럼 나중에 찾는 자리가 쓴다. 단위 테스트가 `SW_EDITORICON` 처럼 훑는다. */
#define SW_EDITORICON_NAME( NameLiteral ) NameLiteral
/** @brief 아이콘 이름 @p NameLiteral(문자열 리터럴)의 글리프입니다. 호출 자리마다 이름 키를 한 번만 intern 한다. */
#define SW_EDITORICON( NameLiteral ) \
    ::sw::editor::EditorIconUtil::findGlyph( []() -> const ::sw::hashed_string& {                                                                         \
        static const ::sw::hashed_string s_key{ NameLiteral };                                                                                             \
        return s_key; }() )
