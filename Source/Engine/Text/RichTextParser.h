/**
 * @file RichTextParser.h
 * @brief 리치 텍스트 최소 버전을 정의합니다. BBCode 형태의 `[b]`, `[i]`, `[color=#rrggbb|#rrggbbaa|이름]`, `[size=배]` 를 지원하며, 글자 `[` 는 `[[` 로 씁니다.
 * @details XML 문서 속성 안에 쓸 때 `<` 를 이스케이프하지 않아도 되고 번역가가 다루기 쉽다(Godot `RichTextLabel` 과 같은 표기). 토큰 읽기는
 *          `Core/String/MarkupTagScanner` 이고, 번역 검사와 의사 로컬라이저도 같은 스캐너를 쓴다.
 *          **모르는 태그, 짝이 맞지 않는 태그, 읽을 수 없는 값은 글자 그대로 남기고 경고한다.** 번역 실수가 화면에서 보이게 하기 위해서다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 리치 텍스트 한 구간의 스타일입니다(표기를 뺀 글 기준). 구간끼리 겹치지 않고, 기본 스타일인 곳에는 구간이 없습니다. */
    struct RichTextSpan
    {
        uint32  _firstByte{ 0 };           ///< 표기를 뺀 텍스트 기준의 시작 바이트입니다.
        uint32  _byteCount{ 0 };           ///< 바이트 수
        uint32  _colorRGBA{ 0xFFFFFFFFu }; ///< 0xRRGGBBAA 입니다. 0xFFFFFFFF 는 위젯 색 그대로입니다.
        float32 _sizeScale{ 1.0f };        ///< 폰트 크기 배율입니다.
        uint16  _colorToken{ 0 };          ///< `[color=이름]` 입니다. `RichTextParseResult::_listColorName` 의 번호 + 1 이며 스타일 변수가 해석합니다. 0 이면 없습니다.
        uint8   _bBold{ SW_FALSE };        ///< 굵게입니다.
        uint8   _bItalic{ SW_FALSE };      ///< 기울임
    };
} // namespace sw

namespace sw
{
    /** @brief 파싱 결과입니다. */
    struct RichTextParseResult
    {
        string               _plainText{};            ///< 표기를 뺀 텍스트입니다.
        vector<RichTextSpan> _listSpan{};             ///< 스타일 구간(글 순서)
        vector<string>       _listColorName{};        ///< `[color=이름]` 의 이름들(첫 등장 순서, 중복 없음)
        uint32               _problemCount{ 0 };      ///< 글자로 남긴 태그 수입니다(모르는 태그, 짝 없는 태그, 값이 틀린 태그). 경고한 수와 같습니다.
        uint8                _bHasMarkup{ SW_FALSE }; ///< 표기가 하나라도 있었다(없으면 `_plainText` 는 원문과 같다)
    };
} // namespace sw

namespace sw
{
    /** @brief 액션 이름에서 지금 입력 장치의 글리프 텍스트(`[ E ]`)를 구하는 함수입니다(`RichTextParser::expandActionTags`). 텍스트 계층은 입력을 모르므로 UI 가 넘깁니다. */
    using RichTextActionGlyphResolver = Delegate<string( string_view )>;

    /**
     * @struct RichTextParser
     * @brief BBCode 꼴 리치 텍스트 파서입니다.
     */
    struct SW_API RichTextParser
    {
        /** @brief 표기를 빼고 구간을 만듭니다. @p outResult 는 앞을 비우고 채웁니다. 문제가 있으면 문제마다 경고 한 줄입니다. */
        static void parse( string_view markup, RichTextParseResult& outResult );
        /** @brief 두 텍스트의 태그 열(이름과 순서)이 같은지 확인합니다. 번역 검사가 원문과 번역을 비교합니다(`MarkupTagScanner::hasSameTags`). */
        static bool hasSameTags( string_view sourceMarkup, string_view translatedMarkup );
        /**
         * @brief 액션 태그 `[action=이름]` 을 @p resolve 가 준 글리프 텍스트로 바꿔 @p outText 에 씁니다(배치 앞에서 하며, 다른 태그는 그대로 둡니다). `[[` 는 건드리지 않습니다.
         * @return 바꾼 태그 수입니다(0 이면 @p outText 는 원문과 같습니다). 입력 장치가 바뀌면 다시 불러야 하며, 텍스트 위젯이 `onInputGlyphsChanged` 로 다시 배치합니다.
         */
        static uint32 expandActionTags( string_view markup, const RichTextActionGlyphResolver& resolve, string& outText );
    };
} // namespace sw
