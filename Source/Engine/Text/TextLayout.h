/**
 * @file TextLayout.h
 * @brief 글 배치입니다 — 런 나누기 → 셰이핑 → 줄 바꿈(UAX #14 단순판) → 정렬 · 줄임표 → 크기. 위젯은 측정(`measure`)과 배치(`layout`) 두 질문만 합니다.
 * @details 언리얼 `FTextLayout` · 유니티 TextCore 배치 · Godot `TextLine/TextParagraph` 의 자리입니다. 길이는 UI 단위(글꼴 크기 × em 비율)이고,
 *          배율(DPI · UI 배율)은 곱하기 전입니다. 같은 입력은 같은 결과입니다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/FontSystem.h"
#include "Engine/Text/ITextShaper.h"
#include "Engine/Text/RichTextParser.h"
#include "Engine/Text/SimpleTextShaper.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    /** @brief 줄 안 정렬입니다. Start · End 는 문단 방향을 따른다(LTR 이면 왼쪽 · 오른쪽). */
    ENUM()
    enum class TextAlignment : uint8
    {
        Start,
        Center,
        End,
        Left,
        Right
    };
} // namespace sw

namespace sw
{
    /** @brief 낱말 안에서 끊어도 되는가입니다(CSS word-break). */
    ENUM()
    enum class TextWordBreak : uint8
    {
        Normal, ///< 한자 · 가나 · 한글 음절 사이는 끊어도 된다(UAX #14)
        KeepAll ///< 한글 · 한자도 공백에서만 끊는다(한국어 조판의 보통 값)
    };
} // namespace sw

namespace sw
{
    /** @brief 넘침 처리입니다. */
    ENUM()
    enum class TextOverflow : uint8
    {
        Clip,
        Ellipsis
    };
} // namespace sw

namespace sw
{
    /** @brief 배치 인자입니다(크기 · 너비는 UI 단위 — 배율이 곱하기 전). */
    REFLECT()
    struct SW_API TextLayoutStyle
    {
        REFLECT_BODY();
        PROPERTY( DisplayName = "Font" )
        FontSpec _font{};
        PROPERTY( DisplayName = "Size", Min = 1.0, Tooltip = "Font size in UI units (one em)" )
        float32 _fontSize{ 18.0f };
        PROPERTY( DisplayName = "Line Height", Tooltip = "Multiplier of the font's natural line height", Min = 0.5 )
        float32 _lineHeight{ 1.0f };
        PROPERTY( DisplayName = "Letter Spacing", Tooltip = "Extra advance per glyph in em" )
        float32 _letterSpacing{ 0.0f };
        PROPERTY( DisplayName = "Alignment" )
        TextAlignment _alignment{ TextAlignment::Start };
        PROPERTY( DisplayName = "Word Break" )
        TextWordBreak _wordBreak{ TextWordBreak::KeepAll };
        PROPERTY( DisplayName = "Overflow" )
        TextOverflow _overflow{ TextOverflow::Clip };
        PROPERTY( DisplayName = "Max Lines", Tooltip = "0 = unlimited" )
        uint16 _maxLines{ 0 };
        PROPERTY( DisplayName = "Wrap", Tooltip = "Break lines to fit the width; off breaks only at newlines" )
        bool _bWrap{ true };
        /** @brief 문단 방향입니다 — 스타일 데이터가 아니라 위젯이 자기 흐름 방향(`Widget::isRightToLeft`)으로 채운다. 정렬 Start · End 와 양방향 수준이 따른다. */
        TextDirection _paragraphDirection{ TextDirection::LeftToRight };
    };
} // namespace sw

namespace sw
{
    /** @brief 배치한 글리프 하나입니다(UI 단위, 글 상자 왼쪽 위 원점 · y 아래가 +). */
    struct LaidOutGlyph
    {
        float2     _origin{};                   ///< 기준선 위의 펜 원점
        float32    _fontSize{ 0.0f };           ///< 이 글리프의 글꼴 크기(리치 텍스트 크기 배 포함)
        uint32     _glyphIndex{ 0 };            ///< 면 안의 글리프 번호(0 = 두부)
        uint32     _cluster{ 0 };               ///< 원문 바이트 위치
        uint32     _colorRgba{ 0xFFFFFFFFu };   ///< 리치 텍스트 색. 0xFFFFFFFF = 위젯 색 그대로
        FontFaceID _face{ kInvalidFontFaceID }; ///< 글리프를 가진 면
        uint8      _bFauxBold{ SW_FALSE };      ///< SDF 문턱을 옮겨 굵게 그린다
        uint8      _bFauxItalic{ SW_FALSE };    ///< 기울여 그린다
    };
} // namespace sw

namespace sw
{
    /** @brief 배치한 줄 하나입니다. */
    struct LaidOutLine
    {
        uint32  _firstGlyph{ 0 };  ///< `TextLayoutResult::_listGlyph` 안의 첫 글리프
        uint32  _glyphCount{ 0 };  ///< 글리프 수(끝 공백 포함 — 커서 · 선택이 쓴다)
        float32 _width{ 0.0f };    ///< 끝 공백을 뺀 너비
        float32 _baseline{ 0.0f }; ///< 상자 위에서 기준선까지
        float32 _height{ 0.0f };   ///< 줄 높이
        uint32  _firstByte{ 0 };   ///< 원문 시작 바이트
        uint32  _byteCount{ 0 };   ///< 원문 바이트 수(줄 바꿈 문자 포함)
    };
} // namespace sw

namespace sw
{
    /** @brief 배치 결과입니다. */
    struct TextLayoutResult
    {
        vector<LaidOutGlyph> _listGlyph{};            ///< 그릴 글리프(줄 바꿈 문자 · 폭 없는 문자는 없다). 줄 안에서는 눈에 보이는 순서(왼쪽 → 오른쪽)
        vector<LaidOutLine>  _listLine{};             ///< 줄
        float2               _size{};                 ///< 가장 넓은 줄 × 줄 높이 합
        uint8                _bTruncated{ SW_FALSE }; ///< 줄 수 · 너비 제한으로 잘렸다
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextLayoutEngine
     * @brief 글 배치기입니다(게임 스레드). 같은 입력은 같은 결과입니다(결정적).
     * @details 줄 바꿈은 욕심쟁이(상용 엔진의 기본)입니다 — 넘치면 마지막 기회에서 끊고, 기회가 없으면(한 낱말이 줄보다 길다) 그 자리에서 강제로 끊습니다
     *          (CSS `overflow-wrap: anywhere`, 닫는 부호를 줄 머리로 보내지 않게 한 글자 앞으로 물린다). 줄 높이는 그 줄의 가장 큰 면 메트릭입니다.
     */
    class SW_API TextLayoutEngine
    {
    public:
        explicit TextLayoutEngine( FontSystem& fontSystem );

        TextLayoutEngine( const TextLayoutEngine& )            = delete;
        TextLayoutEngine& operator=( const TextLayoutEngine& ) = delete;

        /**
         * @brief 너비 @p maxWidth(0 이하 = 무한) 안에 배치합니다. @p outResult 는 앞을 비우고 채웁니다.
         * @param pListSpan 리치 텍스트 구간(`RichTextParser` — @p text 는 표기를 뺀 평문). 구간 경계에서도 런을 끊고 굵게 · 기울임은 그 굵기 · 기울기의 사슬을,
         *                  크기 배는 글리프 크기를, 색은 `LaidOutGlyph::_colorRgba` 를 바꾼다. nullptr 이면 스타일 하나.
         */
        void layout( string_view text, const TextLayoutStyle& style, float32 maxWidth, TextLayoutResult& outResult, const vector<RichTextSpan>* pListSpan = nullptr );
        /**
         * @brief 크기만 잽니다(레이아웃의 measure 단계). `layout(...)._size` 와 같습니다.
         * @details (글 · 스타일 · 너비 · 지금 면 사슬)로 캐시합니다 — 문화권이 바뀌어 사슬이 달라지면 캐시도 갈린다. 캐시가 차면 통째로 비운다.
         */
        float2 measure( string_view text, const TextLayoutStyle& style, float32 maxWidth, const vector<RichTextSpan>* pListSpan = nullptr );
        /** @brief 측정 캐시를 비웁니다. */
        void clearMeasureCache() { _mapMeasure.clear(); }

    private:
        /** @brief 셰이핑 · 줄 바꿈 기회 · 글리프 폭 · 크기 · 색 · 가짜 굵게/기울임을 스크래치에 채웁니다. */
        void shapeText( string_view text, const TextLayoutStyle& style, const FontFaceChain& chain, const vector<RichTextSpan>* pListSpan );
        /** @brief 글의 한 구간(같은 스타일)을 셰이핑해 스크래치 뒤에 붙입니다. */
        void shapeSegment( string_view text, size_t segmentStart, size_t segmentEnd, const TextLayoutStyle& style, const FontFaceChain& chain, const RichTextSpan* pSpan );
        /** @brief 글의 양방향 수준을 정해 글리프마다 옮깁니다(`TextBidi`). 모든 수준이 0 이면 false — 줄 안 재배열을 건너뛴다. */
        bool resolveGlyphLevels( string_view text, TextDirection paragraphDirection );

        FontSystem&                   _fontSystem;
        SimpleTextShaper              _shaper;
        vector<ShapingRun>            _listRunScratch;        ///< 런 나누기 결과
        vector<ShapedGlyph>           _listShapedScratch;     ///< 셰이핑 결과(논리 순서)
        vector<uint8>                 _listBreakScratch;      ///< 글리프마다 뒤의 줄 바꿈 기회(TextLayout.cpp 의 BreakKind)
        vector<float32>               _listWidthScratch;      ///< 글리프마다 폭(UI 단위, 자간 포함)
        vector<float32>               _listSizeScratch;       ///< 글리프마다 글꼴 크기(리치 텍스트 크기 배 포함)
        vector<uint32>                _listColorScratch;      ///< 글리프마다 색(0xFFFFFFFF = 위젯 색)
        vector<uint8>                 _listFauxScratch;       ///< 글리프마다 가짜 굵게(비트 0) · 기울임(비트 1)
        vector<uint32>                _listCodepointScratch;  ///< 글의 코드 포인트(양방향 수준을 정한다 — 폭 없는 방향 제어 포함)
        vector<uint8>                 _listLevelScratch;      ///< 코드 포인트마다 양방향 수준
        vector<uint8>                 _listGlyphLevelScratch; ///< 글리프마다 양방향 수준
        vector<uint8>                 _listLineLevelScratch;  ///< 한 줄의 수준(줄 끝 공백은 문단 수준 · 줄임표 포함)
        vector<uint32>                _listVisualScratch;     ///< 한 줄의 눈에 보이는 순서(줄 안 논리 번호)
        unordered_map<uint64, float2> _mapMeasure;            ///< 측정 캐시
        TextLayoutResult              _measureScratch;        ///< 측정이 쓰는 배치 결과(재사용)
    };
} // namespace sw
