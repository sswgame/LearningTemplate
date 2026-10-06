/**
 * @file MarkupTagScanner.h
 * @brief BBCode 꼴 표기(`[b]` · `[/b]` · `[color=#ff8800]` · `[[` = 글자 `[`)를 토큰으로 읽는 순수 문자열 도우미입니다.
 * @details 리치 텍스트(Engine/Text `RichTextParser`)와 번역 검사(원문 · 번역의 태그 열 비교) · 의사 로컬라이저(태그 안을 바꾸지 않는다)가 같은 규칙으로
 *          읽도록 Core 에 둡니다(현지화는 글자 계층보다 아래 층이라 그쪽을 include 할 수 없다). 어떤 태그 이름이 뜻이 있는지는 모릅니다.
 *          태그 모양: `[` + (`/`) + 이름(ASCII 글자로 시작, 글자 · 숫자 · `_`) + (여는 태그만 `=값`, 값에는 `[` `]` 없음) + `]`. 모양이 아닌 `[` 는 글자입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 표기 토큰 종류입니다. */
    enum class MarkupTokenKind : uint8
    {
        Text,          ///< 표기가 아닌 글(모양이 아닌 `[` 포함)
        OpenTag,       ///< `[이름]` · `[이름=값]`
        CloseTag,      ///< `[/이름]`
        EscapedBracket ///< `[[` — 글자 `[` 하나
    };
} // namespace sw

namespace sw
{
    /** @brief 표기 토큰 하나입니다. 뷰는 읽은 글의 부분입니다. */
    struct MarkupToken
    {
        string_view     _text{};                        ///< 토큰 원문 전체
        string_view     _name{};                        ///< 태그 이름(태그만)
        string_view     _value{};                       ///< `=` 뒤 값(여는 태그만, 없으면 빈 뷰)
        size_t          _offset{ 0 };                   ///< 원문 안의 시작 바이트
        MarkupTokenKind _kind{ MarkupTokenKind::Text }; ///< 종류
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MarkupTagScanner
     * @brief BBCode 꼴 표기 토큰 읽기 · 태그 열 비교입니다.
     */
    struct SW_API MarkupTagScanner
    {
        /** @brief @p inoutOffset 자리의 토큰 하나를 읽고 오프셋을 그 뒤로 옮깁니다. 끝이면 false 입니다. */
        [[nodiscard]] static bool readToken( string_view markup, size_t& inoutOffset, MarkupToken& outToken );
        /** @brief 태그 열입니다(순서대로, 여는 태그는 `이름`, 닫는 태그는 `/이름` — 값은 뺀다). @p outListTag 는 앞을 비운다. */
        static void collectTagSequence( string_view markup, vector<string>& outListTag );
        /** @brief 두 글의 태그 열(이름 · 순서)이 같은지 봅니다 — 번역 검사가 원문과 번역을 견준다. */
        static bool hasSameTags( string_view sourceMarkup, string_view translatedMarkup );
        /** @brief 태그나 `[[` 가 하나라도 있는지 봅니다. */
        static bool hasMarkup( string_view markup );
    };
} // namespace sw
