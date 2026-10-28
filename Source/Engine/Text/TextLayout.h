/**
 * @file TextLayout.h
 * @brief 텍스트 배치를 정의합니다. 런 나누기, 셰이핑, 줄 바꿈(UAX #14 단순 버전), 정렬과 줄임표, 크기 계산 순서로 진행합니다. 위젯은 측정(`measure`)과 배치(`layout`) 두 질문만 합니다.
 * @details 언리얼 `FTextLayout`, 유니티 TextCore 배치, Godot `TextLine/TextParagraph` 에 해당한다. 길이는 UI 단위(폰트 크기 × em 비율)이고,
 *          배율(DPI, UI 배율)은 곱하기 전이다. 같은 입력은 같은 결과가 나온다(결정적).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/ITextShaper.h"
#include "Engine/Text/RichTextParser.h"
#include "Engine/Text/TextMeasureCache.h"
#include "Engine/Text/TextTypes.h"

namespace sw
{
    /** @brief 줄 안 정렬입니다. Start 와 End 는 문단 방향을 따릅니다(LTR 이면 왼쪽과 오른쪽). */
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
        Normal, ///< 한자, 가나, 한글 음절 사이는 끊어도 됩니다(UAX #14).
        KeepAll ///< 한글과 한자도 공백에서만 끊습니다(한국어 조판의 일반적인 값).
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
    /** @brief 배치 인자입니다(크기와 너비는 UI 단위이며 배율을 곱하기 전입니다). */
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
        /** @brief 문단 방향입니다. 스타일 데이터가 아니라 위젯이 자기 흐름 방향(`Widget::isRightToLeft`)으로 채웁니다. 정렬 Start 와 End, 양방향 수준이 이를 따릅니다. */
        TextDirection _paragraphDirection{ TextDirection::LeftToRight };
    };
} // namespace sw

namespace sw
{
    /** @brief 배치한 글리프 하나입니다(UI 단위이며, 텍스트 상자 왼쪽 위가 원점이고 y 는 아래가 +입니다). */
    struct LaidOutGlyph
    {
        float2     _origin{};                   ///< 기준선 위의 펜 원점입니다.
        float32    _fontSize{ 0.0f };           ///< 이 글리프의 폰트 크기입니다(리치 텍스트 크기 배율 포함).
        uint32     _glyphIndex{ 0 };            ///< 면 안의 글리프 번호(0 = 두부)
        uint32     _cluster{ 0 };               ///< 원문 바이트 위치입니다.
        uint32     _colorRGBA{ 0xFFFFFFFFu };   ///< 리치 텍스트 색입니다. 0xFFFFFFFF 는 위젯 색 그대로입니다.
        FontFaceID _face{ kInvalidFontFaceID }; ///< 글리프를 가진 면입니다.
        uint8      _bFauxBold{ SW_FALSE };      ///< SDF 임계값을 옮겨 굵게 그립니다.
        uint8      _bFauxItalic{ SW_FALSE };    ///< 기울여 그립니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 배치한 줄 하나입니다. */
    struct LaidOutLine
    {
        uint32  _firstGlyph{ 0 };  ///< `TextLayoutResult::_listGlyph` 안의 첫 글리프입니다.
        uint32  _glyphCount{ 0 };  ///< 글리프 수입니다(끝 공백 포함이며 커서와 선택이 씁니다).
        float32 _width{ 0.0f };    ///< 끝 공백을 뺀 너비입니다.
        float32 _baseline{ 0.0f }; ///< 상자 위에서 기준선까지의 거리입니다.
        float32 _height{ 0.0f };   ///< 줄 높이입니다.
        uint32  _firstByte{ 0 };   ///< 원문 시작 바이트입니다.
        uint32  _byteCount{ 0 };   ///< 원문 바이트 수(줄 바꿈 문자 포함)
    };
} // namespace sw

namespace sw
{
    /** @brief 배치 결과입니다. */
    struct TextLayoutResult
    {
        vector<LaidOutGlyph> _listGlyph{};            ///< 그릴 글리프입니다(줄 바꿈 문자와 폭 없는 문자는 없습니다). 줄 안에서는 눈에 보이는 순서(왼쪽에서 오른쪽)입니다.
        vector<LaidOutLine>  _listLine{};             ///< 줄
        float2               _size{};                 ///< 가장 넓은 줄 너비 × 줄 높이 합입니다.
        uint8                _bTruncated{ SW_FALSE }; ///< 줄 수나 너비 제한으로 잘렸는지 나타냅니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextLayoutEngine
     * @brief 텍스트 배치기입니다(게임 스레드). 같은 입력은 같은 결과가 나옵니다(결정적).
     * @details 줄 바꿈은 그리디 방식(상용 엔진의 기본)이다. 넘치면 마지막 기회에서 끊고, 기회가 없으면(한 단어가 줄보다 길다) 그 위치에서 강제로 끊는다
     *          (CSS `overflow-wrap: anywhere` 와 같으며, 닫는 부호를 줄 머리로 보내지 않게 한 글자 앞으로 물린다). 줄 높이는 그 줄의 가장 큰 면 메트릭이다.
     */
    class SW_API TextLayoutEngine
    {
    public:
        explicit TextLayoutEngine( FontService& fontService );

        TextLayoutEngine( const TextLayoutEngine& )            = delete;
        TextLayoutEngine& operator=( const TextLayoutEngine& ) = delete;

        /**
         * @brief 너비 @p maxWidth(0 이하면 무한) 안에 배치합니다. @p outResult 는 앞을 비우고 채웁니다.
         * @param pListSpan 리치 텍스트 구간입니다(`RichTextParser`이며 @p text 는 표기를 뺀 평문입니다). 구간 경계에서도 런을 끊고, 굵게와 기울임은 그 굵기와 기울기의 연쇄를,
         *                  크기 배율은 글리프 크기를, 색은 `LaidOutGlyph::_colorRGBA` 를 바꿉니다. nullptr 이면 스타일 하나를 씁니다.
         */
        void layout( string_view text, const TextLayoutStyle& style, float32 maxWidth, TextLayoutResult& outResult, const vector<RichTextSpan>* pListSpan = nullptr );
        /**
         * @brief 크기만 측정합니다(레이아웃의 measure 단계). `layout(...)._size` 와 같습니다.
         * @details (텍스트, 스타일, 너비, 지금 면 연쇄)로 캐시한다. 문화권이 바뀌어 연쇄가 달라지면 키도 달라져 낡은 항목은 조회되지 않는다. 캐시는 두 세대이고
         *          (`TextMeasureCache`) 상한(세대당 `kMeasureCacheGenerationCapacity`, 항목은 그 두 배까지)을 넘으면 오래된 세대만 버린다 — 전체를 비우지 않는다.
         */
        float2 measure( string_view text, const TextLayoutStyle& style, float32 maxWidth, const vector<RichTextSpan>* pListSpan = nullptr );
        /**
         * @brief 측정 캐시를 비웁니다. 지금 부르는 곳은 없습니다.
         * @details 폰트 교체 · 문화권 변경에 연결하지 않는 까닭: 키가 면 연쇄(면 번호는 `FontService` 가 처음 여는 순서로 매기고 다시 쓰지 않는다)를 담고, 문화권의 대체 폰트는
         *          연쇄를 바꾸므로(`UISystem::updateBindings` 가 `invalidateFaceChains` 를 부르면 다음 측정은 새 연쇄로 키를 만든다) 낡은 항목은 부딪히지 않고 세대 교대로 사라진다.
         *          배치가 폰트 메트릭과 연쇄 말고 문화권을 읽지도 않는다(셰이퍼에 언어 인자가 없다). 면의 메트릭이 같은 번호로 바뀌는 길이 생기면 그 알림에서 이것을 부른다.
         */
        void clearMeasureCache() { _measureCache.clear(); }

    private:
        /** @brief 셰이핑, 줄 바꿈 기회, 글리프 폭, 크기, 색, 가짜 굵게와 기울임을 스크래치에 채웁니다. */
        void shapeText( string_view text, const TextLayoutStyle& style, const FontFaceChain& chain, const vector<RichTextSpan>* pListSpan );
        /** @brief 글의 한 구간(같은 스타일)을 셰이핑해 스크래치 뒤에 붙입니다. */
        void shapeSegment( string_view text, size_t segmentStart, size_t segmentEnd, const TextLayoutStyle& style, const FontFaceChain& chain, const RichTextSpan* pSpan );
        /** @brief 텍스트의 양방향 수준을 정해 글리프마다 옮깁니다(`TextBidi`). 모든 수준이 0 이면 false 이며, 줄 안 재배열을 건너뜁니다. */
        [[nodiscard]] bool resolveGlyphLevels( string_view text, TextDirection paragraphDirection );

        FontService&            _fontService;
        unique_ptr<ITextShaper> _pShaper;               ///< 이 빌드의 셰이퍼(`ITextShaper::createDefault` — 단순 또는 복합 문자)
        vector<ShapingRun>      _listRunScratch;        ///< 런 나누기 결과입니다.
        vector<ShapedGlyph>     _listShapedScratch;     ///< 셰이핑 결과(논리 순서)
        vector<uint8>           _listBreakScratch;      ///< 글리프마다 뒤의 줄 바꿈 기회(TextLayout.cpp 의 BreakKind)
        vector<float32>         _listWidthScratch;      ///< 글리프마다 폭(UI 단위, 자간 포함)
        vector<float32>         _listSizeScratch;       ///< 글리프마다의 폰트 크기입니다(리치 텍스트 크기 배율 포함).
        vector<uint32>          _listColorScratch;      ///< 글리프마다 색(0xFFFFFFFF = 위젯 색)
        vector<uint8>           _listFauxScratch;       ///< 글리프마다의 가짜 굵게(비트 0)와 기울임(비트 1)입니다.
        vector<uint32>          _listCodepointScratch;  ///< 텍스트의 코드 포인트입니다(양방향 수준을 정하는 데 쓰며 폭 없는 방향 제어도 포함합니다).
        vector<uint8>           _listLevelScratch;      ///< 코드 포인트마다의 양방향 수준입니다.
        vector<uint8>           _listGlyphLevelScratch; ///< 글리프마다의 양방향 수준입니다.
        vector<uint8>           _listLineLevelScratch;  ///< 한 줄의 수준입니다(줄 끝 공백은 문단 수준이고 줄임표도 포함합니다).
        vector<uint32>          _listVisualScratch;     ///< 한 줄의 눈에 보이는 순서(줄 안 논리 번호)
        TextMeasureCache        _measureCache;          ///< 측정 캐시입니다(두 세대, 파일 `TextMeasureCache.h`).
        TextLayoutResult        _measureScratch;        ///< 측정이 쓰는 배치 결과(재사용)
    };
} // namespace sw
