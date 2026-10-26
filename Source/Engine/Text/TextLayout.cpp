#include "pch.h"

#include "Engine/Text/TextLayout.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#include "Engine/Localization/PseudoLocalizer.h"
#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/TextBidi.h"
#include "Engine/Text/TextItemizer.h"

namespace sw
{
    namespace
    {
        struct TextLayoutInternal
        {
            /**
             * @brief 측정 캐시 한 세대가 담는 항목 수입니다. 캐시는 두 세대라 항목은 이 값의 두 배까지 들고, 넘치면 오래된 세대만 버립니다(`TextMeasureCache`).
             * @details 4096 은 옛 구현의 전체 상한이던 값입니다. 화면 하나의 글 수(위젯 수백 + 목록 항목)가 한 세대에 들어 한 프레임에 현재로 모이는 크기이고, 항목은
             *          float2 하나라 두 세대여도 해시 표 오버헤드 포함 수백 KB 안입니다(대략 8192 × 약 40 B). 숫자로 정한 값이 아니라 옛 값을 이었으며, 적중률은 `[CacheStats]
             *          TextMeasure` 줄(`-gv_profileFrames`)로 본다.
             */
            static constexpr size_t kMeasureCacheGenerationCapacity = 4096;
            /** @brief 측정 캐시의 계측 칸 이름입니다(`CacheStats`). 엔진의 모든 `TextLayoutEngine` 이 한 칸에 합쳐집니다. */
            static constexpr const utf8* kMeasureCacheStatsName = "TextMeasure";
            /** @brief 너비 비교의 여유(UI 단위)입니다. 글리프 폭 합의 부동소수 오차로 딱 맞는 줄이 넘치지 않게 합니다. */
            static constexpr float32 kWidthTolerance = 1e-3f;
            /** @brief 줄임표 문자(…)입니다. 연쇄에 없으면 마침표 셋을 씁니다. */
            static constexpr uint32 kEllipsisCodepoint     = 0x2026u;
            static constexpr uint32 kEllipsisFallbackCount = 3;

            /** @brief 글리프 뒤에서 끊어도 되는지, 반드시 끊는지 나타냅니다. */
            enum class BreakKind : uint8
            {
                None,
                Allowed,
                Mandatory
            };

            static bool isSpace( uint32 codepoint ) { return codepoint == 0x20u || codepoint == 0x09u || codepoint == 0x3000u; }
            static bool isLineBreakCharacter( uint32 codepoint ) { return codepoint == '\n' || codepoint == '\r'; }
            /** @brief 줄 끝에서 너비에 넣지 않는 문자(공백, 줄 바꿈)입니다. */
            static bool isTrailingBlank( uint32 codepoint ) { return isSpace( codepoint ) || isLineBreakCharacter( codepoint ); }

            static bool isIdeographicOrHangul( uint32 codepoint )
            {
                return ( 0x4E00u <= codepoint && codepoint <= 0x9FFFu ) || ( 0x3400u <= codepoint && codepoint <= 0x4DBFu ) ||
                       ( 0x3040u <= codepoint && codepoint <= 0x30FFu ) || ( 0xAC00u <= codepoint && codepoint <= 0xD7AFu );
            }

            /** @brief 앞에서 끊으면 안 되는 문자(닫는 괄호, 마침표, 쉼표, 물음표, 느낌표, 쌍점, 일본과 중국 구두점)입니다. */
            static bool forbidsBreakBefore( uint32 codepoint )
            {
                switch ( codepoint )
                {
                    case ')':
                    case ']':
                    case '}':
                    case ',':
                    case '.':
                    case '!':
                    case '?':
                    case ':':
                    case ';':
                    case 0x2026u:
                    case 0x3001u:
                    case 0x3002u:
                    case 0x300Du:
                    case 0x300Fu:
                    case 0xFF09u:
                    case 0xFF0Cu:
                    case 0xFF0Eu:
                        return true;
                    default:
                        return false;
                }
            }

            /** @brief 뒤에서 끊으면 안 되는 문자(여는 괄호, 따옴표)입니다. */
            static bool forbidsBreakAfter( uint32 codepoint )
            {
                switch ( codepoint )
                {
                    case '(':
                    case '[':
                    case '{':
                    case 0x300Cu:
                    case 0x300Eu:
                    case 0xFF08u:
                        return true;
                    default:
                        return false;
                }
            }

            /** @brief @p current 뒤와 @p next 앞 위치의 줄 바꿈 기회입니다. */
            static BreakKind classifyBreakAfter( uint32 current, uint32 next, TextWordBreak wordBreak )
            {
                if ( current == '\n' )
                    return BreakKind::Mandatory;
                if ( forbidsBreakAfter( current ) || forbidsBreakBefore( next ) )
                    return BreakKind::None;
                const bool bBreakOpportunity = isSpace( current ) || current == '-' || current == 0x2010u;
                if ( bBreakOpportunity )
                    return BreakKind::Allowed;
                const bool bCjkBoundary = isIdeographicOrHangul( current ) || isIdeographicOrHangul( next );
                if ( bCjkBoundary == false )
                    return BreakKind::None;
                // KeepAll 이면 한글 음절끼리는 끊지 않는다. 한자와 가나도 KeepAll 에서는 CSS 처럼 공백에서만 끊는다.
                return wordBreak == TextWordBreak::Normal ? BreakKind::Allowed : BreakKind::None;
            }

            static uint64 hashFloat( uint64 seed, float32 value )
            {
                return HashUtil::combine( seed, BitUtil::floatToBits( value ) );
            }

            static uint64 hashStyle( const TextLayoutStyle& style )
            {
                uint64 hash = HashUtil::computeStringHash64( style._font._family.data(), style._font._family.size(), true, 0 );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._font._weight ) );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._font._slant ) );
                hash        = hashFloat( hash, style._fontSize );
                hash        = hashFloat( hash, style._lineHeight );
                hash        = hashFloat( hash, style._letterSpacing );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._alignment ) );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._wordBreak ) );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._overflow ) );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._maxLines ) );
                hash        = HashUtil::combine( hash, static_cast<uint64>( style._paragraphDirection ) );
                return HashUtil::combine( hash, style._bWrap ? 1u : 0u );
            }

            /** @brief 줄 하나의 글리프 범위([첫, 끝))와 줄임표를 붙일지 나타냅니다. */
            struct LineRange
            {
                uint32 _begin{ 0 };
                uint32 _end{ 0 };
                bool   _bEllipsis{ false };
            };

            /** @brief 줄 하나의 가장 큰 면 메트릭을 모읍니다. 대체 면이 섞인 줄이 겹치지 않게 합니다. */
            struct LineFaceMetrics
            {
                float32 _maxAscender{ 0.0f };
                float32 _minDescender{ 0.0f };
                float32 _maxLineGap{ 0.0f };
                bool    _bHasMetrics{ false };

                /** @brief 면 하나를 그 폰트 크기로 더합니다(값은 UI 단위). */
                void accumulate( const IFontRasterizer& rasterizer, FontFaceID face, float32 fontSize )
                {
                    FontFaceMetrics metrics{};
                    if ( rasterizer.findFaceMetrics( face, metrics ) == false )
                        return;
                    const float32 ascender  = metrics._ascender * fontSize;
                    const float32 descender = metrics._descender * fontSize;
                    const float32 lineGap   = metrics._lineGap * fontSize;
                    _maxAscender            = _bHasMetrics ? MathUtil::max( _maxAscender, ascender ) : ascender;
                    _minDescender           = _bHasMetrics ? MathUtil::min( _minDescender, descender ) : descender;
                    _maxLineGap             = _bHasMetrics ? MathUtil::max( _maxLineGap, lineGap ) : lineGap;
                    _bHasMetrics            = true;
                }
            };

            static constexpr uint8 kFauxBoldBit   = 1u << 0;
            static constexpr uint8 kFauxItalicBit = 1u << 1;

            static uint8 makeFauxBits( const FontFaceChain& chain )
            {
                const uint8 bold   = chain._bFauxBold == SW_TRUE ? kFauxBoldBit : static_cast<uint8>( 0 );
                const uint8 italic = chain._bFauxItalic == SW_TRUE ? kFauxItalicBit : static_cast<uint8>( 0 );
                return static_cast<uint8>( bold | italic );
            }

            /** @brief 리치 텍스트 구간이 바꾼 폰트(굵게, 기울임)입니다. */
            static FontSpec makeSpanFont( const FontSpec& base, const RichTextSpan& span )
            {
                FontSpec font = base;
                if ( span._bBold == SW_TRUE && static_cast<uint16>( font._weight ) < static_cast<uint16>( FontWeight::Bold ) )
                    font._weight = FontWeight::Bold;
                if ( span._bItalic == SW_TRUE )
                    font._slant = FontSlant::Italic;
                return font;
            }

            /** @brief 줄임표 글리프입니다(면, 글리프, 폭). */
            struct EllipsisGlyph
            {
                FontFaceID _face{ kInvalidFontFaceID };
                uint32     _glyphIndex{ 0 };
                uint32     _count{ 0 };
                float32    _width{ 0.0f }; ///< 글리프 하나의 폭(UI 단위)
            };

            /** @brief 배치 한 번이 읽는 셰이핑 결과입니다(엔진의 스크래치 목록을 읽기 전용으로 묶는다). 글리프마다 한 칸씩이다. */
            struct ShapedView
            {
                const vector<ShapedGlyph>& _listShaped;     ///< 셰이핑 결과(논리 순서)
                const vector<float32>&     _listWidth;      ///< 폭(UI 단위, 자간 포함)
                const vector<float32>&     _listSize;       ///< 폰트 크기
                const vector<uint32>&      _listColor;      ///< 색(0xFFFFFFFF = 위젯 색)
                const vector<uint8>&       _listFaux;       ///< 가짜 굵게 · 기울임 비트
                const vector<uint8>&       _listBreak;      ///< 뒤의 줄 바꿈 기회(`BreakKind`)
                const vector<uint8>&       _listGlyphLevel; ///< 양방향 수준
            };

            /** @brief 1) 욕심쟁이 줄 채우기: 폭이 넘치면 마지막 끊을 기회에서(없으면 금칙을 지키며 그 자리에서) 줄을 나눈다. 강제 줄 바꿈은 늘 나눈다. */
            static void breakIntoLines( const ShapedView& view, float32 maxWidth, bool bWrap, vector<LineRange>& outListLine )
            {
                const uint32 glyphCount = static_cast<uint32>( view._listShaped.size() );
                uint32       lineStart  = 0;
                float32      penX       = 0.0f;
                bool         bHasBreak  = false;
                uint32       lastBreak  = 0;
                uint32       glyphIndex = 0;
                while ( glyphIndex < glyphCount )
                {
                    const uint32  codepoint = view._listShaped[glyphIndex]._codepoint;
                    const float32 width     = view._listWidth[glyphIndex];
                    const bool    bOverflow = bWrap && glyphIndex > lineStart && isTrailingBlank( codepoint ) == false && penX + width > maxWidth + kWidthTolerance;
                    if ( bOverflow )
                    {
                        uint32 lineEnd = glyphIndex;
                        if ( bHasBreak )
                        {
                            lineEnd = lastBreak + 1;
                        }
                        else
                        {
                            // 기회가 없다(한 단어가 줄보다 길다). 그 위치에서 끊되, 닫는 부호를 줄 머리로, 여는 부호를 줄 끝으로 보내지 않게 한 글자 물린다.
                            while ( lineEnd > lineStart + 1 && ( forbidsBreakBefore( view._listShaped[lineEnd]._codepoint ) || forbidsBreakAfter( view._listShaped[lineEnd - 1]._codepoint ) ) )
                            {
                                --lineEnd;
                            }
                        }
                        outListLine.push_back( LineRange{ lineStart, lineEnd, false } );
                        lineStart = lineEnd;
                        penX      = 0.0f;
                        bHasBreak = false;
                        for ( uint32 carried = lineStart; carried < glyphIndex; ++carried )
                        {
                            penX += view._listWidth[carried];
                            if ( static_cast<BreakKind>( view._listBreak[carried] ) == BreakKind::Allowed )
                            {
                                bHasBreak = true;
                                lastBreak = carried;
                            }
                        }
                        continue; // 이 글리프를 새 줄에서 다시 본다
                    }
                    penX += width;
                    const BreakKind kind = static_cast<BreakKind>( view._listBreak[glyphIndex] );
                    if ( kind == BreakKind::Allowed )
                    {
                        bHasBreak = true;
                        lastBreak = glyphIndex;
                    }
                    else if ( kind == BreakKind::Mandatory )
                    {
                        outListLine.push_back( LineRange{ lineStart, glyphIndex + 1, false } );
                        lineStart = glyphIndex + 1;
                        penX      = 0.0f;
                        bHasBreak = false;
                    }
                    ++glyphIndex;
                }
                if ( lineStart < glyphCount )
                    outListLine.push_back( LineRange{ lineStart, glyphCount, false } );
            }

            /** @brief 2) 줄 수와 너비 제한: 넘치면 자르고, 줄임표면 마지막(넘친) 줄에 붙인다. 잘렸으면 @p outResult 에 표시한다. */
            static void limitLines( const ShapedView& view, const TextLayoutStyle& style, float32 maxWidth, bool bWrap, vector<LineRange>& inoutListLine, TextLayoutResult& outResult )
            {
                const bool bEllipsis = style._overflow == TextOverflow::Ellipsis;
                if ( style._maxLines > 0 && inoutListLine.size() > style._maxLines )
                {
                    inoutListLine.resize( style._maxLines );
                    outResult._bTruncated           = SW_TRUE;
                    inoutListLine.back()._bEllipsis = bEllipsis;
                }
                if ( bWrap == false && maxWidth > 0.0f )
                {
                    for ( LineRange& line : inoutListLine )
                    {
                        float32 lineWidth = 0.0f;
                        for ( uint32 index = line._begin; index < line._end; ++index )
                        {
                            lineWidth += view._listWidth[index];
                        }
                        if ( lineWidth > maxWidth + kWidthTolerance )
                        {
                            outResult._bTruncated = SW_TRUE;
                            line._bEllipsis       = line._bEllipsis || bEllipsis;
                        }
                    }
                }
            }

            /** @brief 줄임표 글리프를 찾습니다. 줄임표 문자(…)가 연쇄에 없으면 마침표 셋을 씁니다. */
            static EllipsisGlyph findEllipsis( FontService& fontService, const FontFaceChain& chain, const TextLayoutStyle& style )
            {
                EllipsisGlyph ellipsis{};
                ellipsis._face  = fontService.findFaceForCodepoint( chain, kEllipsisCodepoint, ellipsis._glyphIndex );
                ellipsis._count = 1;
                if ( ellipsis._glyphIndex == 0 )
                {
                    ellipsis._face  = fontService.findFaceForCodepoint( chain, '.', ellipsis._glyphIndex );
                    ellipsis._count = kEllipsisFallbackCount;
                }
                GlyphMetrics metrics{};
                if ( fontService.getRasterizer().findGlyphMetrics( ellipsis._face, ellipsis._glyphIndex, metrics ) )
                    ellipsis._width = ( metrics._advance + style._letterSpacing ) * style._fontSize;
                return ellipsis;
            }

            /** @brief 한 줄에서 보이는 끝과 너비입니다. */
            struct LineFit
            {
                uint32  _end{ 0 };        ///< 줄에 남기는 글리프의 끝(줄임표를 붙이면 보이는 끝과 같다)
                uint32  _visibleEnd{ 0 }; ///< 끝 공백과 줄 바꿈을 뺀 끝
                float32 _trimmed{ 0.0f }; ///< [첫, 보이는 끝) 글리프 폭의 합(줄임표 폭 제외)
            };

            /** @brief 3-1) 줄의 보이는 끝과 너비를 정합니다. 끝 공백과 줄 바꿈은 너비에서 뺀다(글리프는 남기며, 커서와 선택이 쓴다). 줄임표를 붙이면 줄임표가 들어갈 때까지 뒤에서 글리프를 뺀다. */
            static LineFit fitLine( const ShapedView& view, const LineRange& range, float32 maxWidth, float32 ellipsisWidth )
            {
                LineFit fit{};
                fit._end        = range._end;
                fit._visibleEnd = fit._end;
                while ( fit._visibleEnd > range._begin && isTrailingBlank( view._listShaped[fit._visibleEnd - 1]._codepoint ) )
                {
                    --fit._visibleEnd;
                }
                for ( uint32 index = range._begin; index < fit._visibleEnd; ++index )
                {
                    fit._trimmed += view._listWidth[index];
                }
                if ( range._bEllipsis )
                {
                    // 줄임표가 들어갈 때까지 뒤에서 글리프를 뺀다(빼고 남은 끝 공백도 뺀다).
                    while ( true )
                    {
                        const bool bEllipsisOverflows = fit._visibleEnd > range._begin && maxWidth > 0.0f && fit._trimmed + ellipsisWidth > maxWidth + kWidthTolerance;
                        if ( bEllipsisOverflows == false )
                            break;
                        --fit._visibleEnd;
                        fit._trimmed -= view._listWidth[fit._visibleEnd];
                    }
                    while ( fit._visibleEnd > range._begin && isTrailingBlank( view._listShaped[fit._visibleEnd - 1]._codepoint ) )
                    {
                        --fit._visibleEnd;
                        fit._trimmed -= view._listWidth[fit._visibleEnd];
                    }
                    fit._end = fit._visibleEnd;
                }
                return fit;
            }

            /** @brief 3-2) 줄의 가장 큰 면 메트릭을 구합니다. 대체 면이 섞인 줄이 겹치지 않게 한다. */
            static LineFaceMetrics measureLineFaces( const IFontRasterizer& rasterizer, const ShapedView& view, const FontFaceChain& chain, const TextLayoutStyle& style, const LineRange& range, uint32 end,
                                                     const EllipsisGlyph& ellipsis )
            {
                LineFaceMetrics faceMetrics{};
                faceMetrics.accumulate( rasterizer, chain._arrFace[0], style._fontSize );
                for ( uint32 index = range._begin; index < end; ++index )
                {
                    faceMetrics.accumulate( rasterizer, view._listShaped[index]._face, view._listSize[index] );
                }
                if ( range._bEllipsis )
                    faceMetrics.accumulate( rasterizer, ellipsis._face, style._fontSize );
                return faceMetrics;
            }

            /**
             * @brief 3-3) 눈에 보이는 순서를 정합니다. 줄 안 글리프 [첫, end) 뒤에 줄임표(문단 수준)를 논리로 붙이고, 줄 끝 공백은 문단 수준(L1)으로 되돌린 뒤 뒤집는다(L2).
             * @param inoutLineLevel 한 줄의 수준 스크래치입니다. @param outVisual 눈에 보이는 순서(줄 안 논리 번호)입니다.
             */
            static void orderLineVisually( const ShapedView& view, const LineRange& range, uint32 lineGlyphCount, uint32 itemCount, bool bReorder, uint8 paragraphLevel,
                                           vector<uint8>& inoutLineLevel, vector<uint32>& outVisual )
            {
                outVisual.resize( itemCount );
                if ( bReorder == false )
                {
                    for ( uint32 index = 0; index < itemCount; ++index )
                    {
                        outVisual[index] = index;
                    }
                    return;
                }
                inoutLineLevel.assign( itemCount, paragraphLevel );
                for ( uint32 index = 0; index < lineGlyphCount; ++index )
                {
                    inoutLineLevel[index] = view._listGlyphLevel[range._begin + index];
                }
                for ( uint32 index = lineGlyphCount; index > 0 && isTrailingBlank( view._listShaped[range._begin + index - 1]._codepoint ); --index )
                {
                    inoutLineLevel[index - 1] = paragraphLevel;
                }
                TextBidi::reorderVisually( inoutLineLevel, outVisual );
            }

            /** @brief 한 줄을 내는 데 필요한 값입니다(`appendLineGlyphs`). */
            struct LineEmit
            {
                const LineRange* _pRange{ nullptr };
                LineFit          _fit{};
                uint32           _lineGlyphCount{ 0 };
                float32          _baseline{ 0.0f };
                bool             _bReorder{ false };
                uint8            _paragraphLevel{ 0 };
            };

            /** @brief 3-4) 줄의 글리프를 눈에 보이는 순서대로 @p outResult 에 냅니다. RTL 문단의 끝 공백은 왼쪽 끝으로 가므로 너비에서 뺀 만큼 왼쪽 밖에서 시작해 보이는 글리프가 [0, 너비] 에 놓이게 한다. */
            static void appendLineGlyphs( const ShapedView& view, const IFontRasterizer& rasterizer, const FontFaceChain& chain, const TextLayoutStyle& style, const EllipsisGlyph& ellipsis,
                                          const LineEmit& emit, uint32 textByteCount, const vector<uint32>& listVisual, TextLayoutResult& outResult )
            {
                const LineRange& range      = *emit._pRange;
                const uint32     end        = emit._fit._end;
                const uint32     glyphCount = static_cast<uint32>( view._listShaped.size() );
                float32          pen        = 0.0f;
                if ( TextBidi::isRightToLeftLevel( emit._paragraphLevel ) )
                {
                    for ( uint32 index = emit._fit._visibleEnd; index < end; ++index )
                    {
                        pen -= view._listWidth[index];
                    }
                }
                for ( const uint32 item : listVisual )
                {
                    if ( item >= emit._lineGlyphCount )
                    {
                        LaidOutGlyph glyph{};
                        glyph._origin      = float2{ pen, emit._baseline };
                        glyph._fontSize    = style._fontSize;
                        glyph._glyphIndex  = ellipsis._glyphIndex;
                        glyph._cluster     = end < glyphCount ? view._listShaped[end]._cluster : textByteCount;
                        glyph._face        = ellipsis._face;
                        glyph._bFauxBold   = chain._bFauxBold;
                        glyph._bFauxItalic = chain._bFauxItalic;
                        outResult._listGlyph.push_back( glyph );
                        pen += ellipsis._width;
                        continue;
                    }
                    const uint32       index  = range._begin + item;
                    const ShapedGlyph& shaped = view._listShaped[index];
                    if ( isLineBreakCharacter( shaped._codepoint ) == false )
                    {
                        LaidOutGlyph  glyph{};
                        const float32 glyphSize = view._listSize[index];
                        const uint8   fauxBits  = view._listFaux[index];
                        glyph._origin           = float2{ pen + shaped._offset._x * glyphSize, emit._baseline - shaped._offset._y * glyphSize };
                        glyph._fontSize         = glyphSize;
                        glyph._glyphIndex       = shaped._glyphIndex;
                        glyph._cluster          = shaped._cluster;
                        glyph._colorRgba        = view._listColor[index];
                        glyph._face             = shaped._face;
                        glyph._bFauxBold        = ( fauxBits & kFauxBoldBit ) != 0 ? SW_TRUE : SW_FALSE;
                        glyph._bFauxItalic      = ( fauxBits & kFauxItalicBit ) != 0 ? SW_TRUE : SW_FALSE;
                        // RTL 수준의 괄호는 짝 글리프로 그린다(L4). 면에 짝이 없으면 그대로.
                        const bool   bRightToLeftGlyph = emit._bReorder && TextBidi::isRightToLeftLevel( view._listGlyphLevel[index] );
                        const uint32 mirrored          = bRightToLeftGlyph ? TextBidi::getMirroredCodepoint( shaped._codepoint ) : shaped._codepoint;
                        if ( mirrored != shaped._codepoint )
                        {
                            const uint32 mirroredGlyph = rasterizer.findGlyphIndex( shaped._face, mirrored );
                            if ( mirroredGlyph != 0 )
                                glyph._glyphIndex = mirroredGlyph;
                        }
                        outResult._listGlyph.push_back( glyph );
                    }
                    pen += view._listWidth[index];
                }
            }

            /** @brief 4) 정렬: 상자 너비는 maxWidth 이다(무한이면 가장 넓은 줄). Start 와 End 는 문단 방향을 따른다(RTL 이면 Start 가 오른쪽). */
            static void alignLines( const TextLayoutStyle& style, float32 maxWidth, float32 maxLineWidth, uint8 paragraphLevel, TextLayoutResult& inoutResult )
            {
                const float32 boxWidth   = maxWidth > 0.0f ? maxWidth : maxLineWidth;
                const bool    bRightPara = TextBidi::isRightToLeftLevel( paragraphLevel );
                const bool    bCenter    = style._alignment == TextAlignment::Center;
                const bool    bEnd       = style._alignment == TextAlignment::Right || ( style._alignment == TextAlignment::End && bRightPara == false ) ||
                                  ( style._alignment == TextAlignment::Start && bRightPara );
                const float32 alignFactor = bCenter ? 0.5f : ( bEnd ? 1.0f : 0.0f );
                if ( alignFactor == 0.0f )
                    return;
                for ( const LaidOutLine& line : inoutResult._listLine )
                {
                    const float32 shift = MathUtil::max( 0.0f, boxWidth - line._width ) * alignFactor;
                    for ( uint32 index = line._firstGlyph; index < line._firstGlyph + line._glyphCount; ++index )
                    {
                        inoutResult._listGlyph[index]._origin._x += shift;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TextLayoutEngine::TextLayoutEngine( FontService& fontService )
        : _fontService{ fontService }
        , _pShaper{ fontService.getRasterizer().createShaper() }
        , _listRunScratch{}
        , _listShapedScratch{}
        , _listBreakScratch{}
        , _listWidthScratch{}
        , _listSizeScratch{}
        , _listColorScratch{}
        , _listFauxScratch{}
        , _listCodepointScratch{}
        , _listLevelScratch{}
        , _listGlyphLevelScratch{}
        , _listLineLevelScratch{}
        , _listVisualScratch{}
        , _measureCache{ TextLayoutInternal::kMeasureCacheGenerationCapacity, TextLayoutInternal::kMeasureCacheStatsName }
        , _measureScratch{}
    {
    }

    void TextLayoutEngine::shapeSegment( string_view text, size_t segmentStart, size_t segmentEnd, const TextLayoutStyle& style, const FontFaceChain& chain,
                                         const RichTextSpan* pSpan )
    {
        using Internal                   = TextLayoutInternal;
        const FontFaceChain segmentChain = pSpan != nullptr ? _fontService.getFaceChain( Internal::makeSpanFont( style._font, *pSpan ) ) : chain;
        const float32       fontSize     = pSpan != nullptr ? style._fontSize * pSpan->_sizeScale : style._fontSize;
        const uint32        colorRgba    = pSpan != nullptr ? pSpan->_colorRgba : 0xFFFFFFFFu;
        const uint8         fauxBits     = Internal::makeFauxBits( segmentChain );
        const size_t        firstGlyph   = _listShapedScratch.size();
        TextItemizer::itemize( _fontService, segmentChain, text.substr( segmentStart, segmentEnd - segmentStart ), _listRunScratch );
        const IFontRasterizer& rasterizer = _fontService.getRasterizer();
        for ( ShapingRun& run : _listRunScratch )
        {
            run._byteOffset += static_cast<uint32>( segmentStart ); // 클러스터를 전체 텍스트 기준으로 맞춘다
            _pShaper->shape( rasterizer, run, _listShapedScratch );
        }
        for ( size_t index = firstGlyph; index < _listShapedScratch.size(); ++index )
        {
            _listSizeScratch.push_back( fontSize );
            _listColorScratch.push_back( colorRgba );
            _listFauxScratch.push_back( fauxBits );
        }
    }

    void TextLayoutEngine::shapeText( string_view text, const TextLayoutStyle& style, const FontFaceChain& chain, const vector<RichTextSpan>* pListSpan )
    {
        using Internal = TextLayoutInternal;
        _listShapedScratch.clear();
        _listSizeScratch.clear();
        _listColorScratch.clear();
        _listFauxScratch.clear();
        if ( pListSpan == nullptr || pListSpan->empty() )
        {
            shapeSegment( text, 0, text.size(), style, chain, nullptr );
        }
        else
        {
            // 구간 경계에서 끊는다. 구간 밖은 기본 스타일, 구간 안은 그 구간의 폰트, 크기, 색을 쓴다.
            size_t cursor = 0;
            for ( const RichTextSpan& span : *pListSpan )
            {
                const size_t spanStart = MathUtil::min( static_cast<size_t>( span._firstByte ), text.size() );
                const size_t spanEnd   = MathUtil::min( spanStart + span._byteCount, text.size() );
                if ( spanStart > cursor )
                    shapeSegment( text, cursor, spanStart, style, chain, nullptr );
                if ( spanEnd > spanStart && spanStart >= cursor )
                    shapeSegment( text, spanStart, spanEnd, style, chain, &span );
                cursor = MathUtil::max( cursor, spanEnd );
            }
            if ( cursor < text.size() )
                shapeSegment( text, cursor, text.size(), style, chain, nullptr );
        }
        const size_t glyphCount = _listShapedScratch.size();
        _listWidthScratch.resize( glyphCount );
        _listBreakScratch.resize( glyphCount );
        for ( size_t index = 0; index < glyphCount; ++index )
        {
            const ShapedGlyph& glyph      = _listShapedScratch[index];
            const bool         bLineBreak = Internal::isLineBreakCharacter( glyph._codepoint );
            _listWidthScratch[index]      = bLineBreak ? 0.0f : ( glyph._advance + style._letterSpacing ) * _listSizeScratch[index];
            const uint32        next      = index + 1 < glyphCount ? _listShapedScratch[index + 1]._codepoint : 0u;
            Internal::BreakKind kind      = Internal::classifyBreakAfter( glyph._codepoint, next, style._wordBreak );
            if ( index + 1 == glyphCount && kind == Internal::BreakKind::Allowed )
                kind = Internal::BreakKind::None; // 텍스트 끝 뒤에는 끊을 것이 없다
            _listBreakScratch[index] = static_cast<uint8>( kind );
        }
    }

    bool TextLayoutEngine::resolveGlyphLevels( string_view text, TextDirection paragraphDirection )
    {
        // 수준은 원문 코드 포인트로 정한다. 셰이퍼가 버린 폭 없는 방향 제어(RLO, PDF)도 수준을 바꾼다. 글리프는 클러스터(원문 바이트)로 짝짓는다.
        _listGlyphLevelScratch.assign( _listShapedScratch.size(), 0 );
        _listCodepointScratch.clear();
        bool   bAnyRightToLeft = paragraphDirection == TextDirection::RightToLeft;
        size_t offset          = 0;
        while ( offset < text.size() )
        {
            const uint32 codepoint = StringUtil::decodeUtf8( text, offset );
            bAnyRightToLeft        = bAnyRightToLeft || TextItemizer::isStrongRightToLeft( codepoint ) || codepoint == PseudoLocalizer::kRightToLeftOverride;
            _listCodepointScratch.push_back( codepoint );
        }
        if ( bAnyRightToLeft == false )
            return false; // LTR 문단에 RTL 문자나 RLO 가 없으면 모든 수준이 0 이다
        TextBidi::resolveLevels( _listCodepointScratch, paragraphDirection, _listLevelScratch );

        // 클러스터는 논리 순서라 줄어들지 않는다. 코드 포인트를 앞으로만 민다. codepointEnd 는 지금 코드 포인트 다음 바이트다.
        size_t codepointIndex = 0;
        size_t codepointEnd   = 0;
        (void)StringUtil::decodeUtf8( text, codepointEnd );
        bool bAnyLevel = false;
        for ( size_t glyphIndex = 0; glyphIndex < _listShapedScratch.size(); ++glyphIndex )
        {
            const uint32 cluster = _listShapedScratch[glyphIndex]._cluster;
            while ( codepointEnd <= cluster && codepointEnd < text.size() )
            {
                (void)StringUtil::decodeUtf8( text, codepointEnd );
                ++codepointIndex;
            }
            const uint8 level                  = _listLevelScratch[codepointIndex];
            _listGlyphLevelScratch[glyphIndex] = level;
            bAnyLevel                          = bAnyLevel || level != 0;
        }
        return bAnyLevel;
    }

    void TextLayoutEngine::layout( string_view text, const TextLayoutStyle& style, float32 maxWidth, TextLayoutResult& outResult, const vector<RichTextSpan>* pListSpan )
    {
        using Internal = TextLayoutInternal;
        outResult._listGlyph.clear();
        outResult._listLine.clear();
        outResult._size       = float2{};
        outResult._bTruncated = SW_FALSE;
        if ( text.empty() || _fontService.isInitialized() == false )
            return;

        const FontFaceChain chain = _fontService.getFaceChain( style._font );
        if ( chain._faceCount == 0 )
            return;
        shapeText( text, style, chain, pListSpan );
        const uint32               glyphCount     = static_cast<uint32>( _listShapedScratch.size() );
        const bool                 bReorder       = resolveGlyphLevels( text, style._paragraphDirection );
        const uint8                paragraphLevel = TextBidi::getParagraphLevel( style._paragraphDirection );
        const bool                 bWrap          = style._bWrap && maxWidth > 0.0f;
        const Internal::ShapedView view{ _listShapedScratch, _listWidthScratch, _listSizeScratch, _listColorScratch, _listFauxScratch, _listBreakScratch, _listGlyphLevelScratch };

        // 1) 줄 채우기, 2) 줄 수와 너비 제한.
        vector<Internal::LineRange> listLine;
        Internal::breakIntoLines( view, maxWidth, bWrap, listLine );
        Internal::limitLines( view, style, maxWidth, bWrap, listLine, outResult );

        Internal::EllipsisGlyph ellipsis{};
        const bool              bAnyEllipsis = outResult._bTruncated == SW_TRUE && style._overflow == TextOverflow::Ellipsis;
        if ( bAnyEllipsis )
            ellipsis = Internal::findEllipsis( _fontService, chain, style );

        // 3) 줄마다 글리프, 메트릭, 기준선(정렬 전이며 x 는 줄 왼쪽 기준)을 정한다.
        const IFontRasterizer& rasterizer   = _fontService.getRasterizer();
        float32                lineTop      = 0.0f;
        float32                maxLineWidth = 0.0f;
        for ( size_t lineIndex = 0; lineIndex < listLine.size(); ++lineIndex )
        {
            const Internal::LineRange& range         = listLine[lineIndex];
            const float32              ellipsisWidth = range._bEllipsis ? ellipsis._width * static_cast<float32>( ellipsis._count ) : 0.0f;
            const Internal::LineFit    fit           = Internal::fitLine( view, range, maxWidth, ellipsisWidth );

            const Internal::LineFaceMetrics faceMetrics   = Internal::measureLineFaces( rasterizer, view, chain, style, range, fit._end, ellipsis );
            const float32                   maxAscender   = faceMetrics._maxAscender;
            const float32                   naturalHeight = maxAscender - faceMetrics._minDescender;
            const float32                   lineHeight    = ( naturalHeight + faceMetrics._maxLineGap ) * style._lineHeight;

            LaidOutLine line{};
            line._firstGlyph = static_cast<uint32>( outResult._listGlyph.size() );
            line._width      = fit._trimmed + ellipsisWidth;
            line._height     = lineHeight;
            line._baseline   = lineTop + ( lineHeight - naturalHeight ) * 0.5f + maxAscender;
            line._firstByte  = range._begin < glyphCount ? _listShapedScratch[range._begin]._cluster : static_cast<uint32>( text.size() );
            const uint32 nextByte =
                lineIndex + 1 < listLine.size() && listLine[lineIndex + 1]._begin < glyphCount ? _listShapedScratch[listLine[lineIndex + 1]._begin]._cluster : static_cast<uint32>( text.size() );
            line._byteCount = nextByte - line._firstByte;

            Internal::LineEmit emit{};
            emit._pRange           = &range;
            emit._fit              = fit;
            emit._lineGlyphCount   = fit._end - range._begin;
            emit._baseline         = line._baseline;
            emit._bReorder         = bReorder;
            emit._paragraphLevel   = paragraphLevel;
            const uint32 itemCount = emit._lineGlyphCount + ( range._bEllipsis ? ellipsis._count : 0u );
            Internal::orderLineVisually( view, range, emit._lineGlyphCount, itemCount, bReorder, paragraphLevel, _listLineLevelScratch, _listVisualScratch );
            Internal::appendLineGlyphs( view, rasterizer, chain, style, ellipsis, emit, static_cast<uint32>( text.size() ), _listVisualScratch, outResult );

            line._glyphCount = static_cast<uint32>( outResult._listGlyph.size() ) - line._firstGlyph;
            outResult._listLine.push_back( line );
            lineTop += lineHeight;
            maxLineWidth = MathUtil::max( maxLineWidth, line._width );
        }
        outResult._size = float2{ maxLineWidth, lineTop };

        // 4) 정렬.
        Internal::alignLines( style, maxWidth, maxLineWidth, paragraphLevel, outResult );
    }

    float2 TextLayoutEngine::measure( string_view text, const TextLayoutStyle& style, float32 maxWidth, const vector<RichTextSpan>* pListSpan )
    {
        using Internal = TextLayoutInternal;
        if ( text.empty() || _fontService.isInitialized() == false )
            return float2{};
        const FontFaceChain chain = _fontService.getFaceChain( style._font );
        // 글 밖에서 결과를 정하는 입력이 문맥이다. 글은 키가 따로 훑고 글 사본으로 확인한다(`TextMeasureKey` — 해시만으로는 부딪힘이 다른 글의 크기를 돌려준다).
        uint64 context = Internal::hashStyle( style );
        context        = Internal::hashFloat( context, maxWidth );
        for ( uint32 index = 0; index < chain._faceCount; ++index )
        {
            context = HashUtil::combine( context, chain._arrFace[index] );
        }
        if ( pListSpan != nullptr )
        {
            for ( const RichTextSpan& span : *pListSpan )
            {
                context = HashUtil::combine( context, ( static_cast<uint64>( span._firstByte ) << 32 ) | span._byteCount );
                context = HashUtil::combine( context, span._colorRgba );
                context = Internal::hashFloat( context, span._sizeScale );
                context = HashUtil::combine( context, ( static_cast<uint64>( span._bBold ) << 1 ) | span._bItalic );
            }
        }
        const TextMeasureKey key{ text, context };
        const float2*        pCached = _measureCache.find( key );
        if ( pCached != nullptr )
            return *pCached;
        layout( text, style, maxWidth, _measureScratch, pListSpan );
        _measureCache.insert( key, _measureScratch._size );
        return _measureScratch._size;
    }
} // namespace sw
