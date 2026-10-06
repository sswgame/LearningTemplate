/**
 * @file RichTextParser.h
 * @brief 리치 텍스트 최소판입니다 — BBCode 꼴 `[b]` · `[i]` · `[color=#rrggbb|#rrggbbaa|이름]` · `[size=배]`, 글자 `[` 는 `[[`.
 * @details XML 문서 속성 안에 쓸 때 `<` 를 이스케이프하지 않아도 되고 번역가가 다루기 쉽다(Godot `RichTextLabel` 과 같은 표기). 토큰 읽기는
 *          `Core/String/MarkupTagScanner` 이고, 번역 검사 · 의사 로컬라이저도 같은 스캐너를 쓴다.
 *          **모르는 태그 · 짝이 맞지 않는 태그 · 읽을 수 없는 값은 글자 그대로 남기고 경고합니다** — 번역 실수가 화면에서 보이게.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @brief 리치 텍스트 한 구간의 스타일입니다(표기를 뺀 글 기준). 구간끼리 겹치지 않고, 기본 스타일인 곳에는 구간이 없습니다. */
    struct RichTextSpan
    {
        uint32  _firstByte{ 0 };           ///< 표기를 뺀 글 기준 시작 바이트
        uint32  _byteCount{ 0 };           ///< 바이트 수
        uint32  _colorRgba{ 0xFFFFFFFFu }; ///< 0xRRGGBBAA. 0xFFFFFFFF = 위젯 색 그대로
        float32 _sizeScale{ 1.0f };        ///< 글꼴 크기 배
        uint16  _colorToken{ 0 };          ///< `[color=이름]` — `RichTextParseResult::_listColorName` 의 번호 + 1(스타일 변수가 푼다). 0 = 없음
        uint8   _bBold{ SW_FALSE };        ///< 굵게
        uint8   _bItalic{ SW_FALSE };      ///< 기울임
    };
} // namespace sw

namespace sw
{
    /** @brief 파싱 결과입니다. */
    struct RichTextParseResult
    {
        string               _plainText{};            ///< 표기를 뺀 글
        vector<RichTextSpan> _listSpan{};             ///< 스타일 구간(글 순서)
        vector<string>       _listColorName{};        ///< `[color=이름]` 의 이름들(첫 등장 순서, 중복 없음)
        uint32               _problemCount{ 0 };      ///< 글자로 남긴 태그 수(모르는 · 짝 없는 · 값이 틀린) — 경고한 수
        uint8                _bHasMarkup{ SW_FALSE }; ///< 표기가 하나라도 있었다(없으면 `_plainText` 는 원문과 같다)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RichTextParser
     * @brief BBCode 꼴 리치 텍스트 파서입니다.
     */
    struct SW_API RichTextParser
    {
        /** @brief 표기를 빼고 구간을 만듭니다. @p outResult 는 앞을 비우고 채웁니다. 문제가 있으면 문제마다 경고 한 줄입니다. */
        static void parse( string_view markup, RichTextParseResult& outResult );
        /** @brief 두 글의 태그 열(이름 · 순서)이 같은지 봅니다 — 번역 검사가 원문과 번역을 견준다(`MarkupTagScanner::hasSameTags`). */
        static bool hasSameTags( string_view sourceMarkup, string_view translatedMarkup );
    };
} // namespace sw
