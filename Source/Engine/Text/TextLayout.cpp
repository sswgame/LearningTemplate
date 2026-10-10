#include "pch.h"

#include "Engine/Text/TextLayout.h"

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
            /** @brief 측정 캐시가 이만큼 차면 통째로 비운다(문화권 · 글이 바뀌며 쌓이는 낡은 칸을 오래 들지 않게). */
            static constexpr size_t kMaxMeasureCacheEntryCount = 4096;
            /** @brief 너비 비교의 여유(UI 단위) — 글리프 폭 합의 부동소수 오차로 딱 맞는 줄이 넘치지 않게. */
            static constexpr float32 kWidthTolerance = 1e-3f;
            /** @brief 줄임표 문자(…). 사슬에 없으면 마침표 셋이다. */
            static constexpr uint32 kEllipsisCodepoint     = 0x2026u;
            static constexpr uint32 kEllipsisFallbackCount = 3;

            /** @brief 글리프 뒤에서 끊어도 되는가 · 반드시 끊는가입니다. */
            enum class BreakKind : uint8
            {
                None,
                Allowed,
                Mandatory
            };

            static bool isSpace( uint32 codepoint ) { return codepoint == 0x20u || codepoint == 0x09u || codepoint == 0x3000u; }
            static bool isLineBreakCharacter( uint32 codepoint ) { return codepoint == '\n' || codepoint == '\r'; }
            /** @brief 줄 끝에서 너비에 넣지 않는 문자(공백 · 줄 바꿈)입니다. */
            static bool isTrailingBlank( uint32 codepoint ) { return isSpace( codepoint ) || isLineBreakCharacter( codepoint ); }

            static bool isIdeographicOrHangul( uint32 codepoint )
            {
                return ( 0x4E00u <= codepoint && codepoint <= 0x9FFFu ) || ( 0x3400u <= codepoint && codepoint <= 0x4DBFu ) ||
                       ( 0x3040u <= codepoint && codepoint <= 0x30FFu ) || ( 0xAC00u <= codepoint && codepoint <= 0xD7AFu );
            }

            /** @brief 앞에서 끊으면 안 되는 문자(닫는 괄호 · 마침표 · 쉼표 · 물음표 · 느낌표 · 쌍점 · 일본 · 중국 구두점). */
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

            /** @brief 뒤에서 끊으면 안 되는 문자(여는 괄호 · 따옴표). */
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

            /** @brief @p current 뒤 · @p next 앞 자리의 기회입니다. */
            static BreakKind classifyBreakAfter( uint32 current, uint32 next, TextWordBreak wordBreak )
            {
                if ( current == '\n' )
                    return BreakKind::Mandatory;
                if ( forbidsBreakAfter( current ) || forbidsBreakBefore( next ) )
                    return BreakKind::None;
                if ( isSpace( current ) || current == '-' || current == 0x2010u )
                    return BreakKind::Allowed;
                const bool bCjkBoundary = isIdeographicOrHangul( current ) || isIdeographicOrHangul( next );
                if ( bCjkBoundary == false )
                    return BreakKind::None;
                // KeepAll 이면 한글 음절끼리는 끊지 않는다. 한자 · 가나도 KeepAll 에서는 CSS 처럼 공백에서만 끊는다.
                return wordBreak == TextWordBreak::Normal ? BreakKind::Allowed : BreakKind::None;
            }

            static uint64 hashFloat( uint64 seed, float32 value )
            {
                uint32 bits = 0;
                Memory::copy( &bits, &value, sizeof( bits ) );
                return HashUtil::combine( seed, bits );
            }

            static uint64 hashStyle( const TextLayoutStyle& style )
            {
                uint64 hash = StringUtil::computeHash64( style._font._family.data(), style._font._family.size(), true, 0 );
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

            /** @brief 줄 하나의 글리프 범위([첫, 끝))와 줄임표를 붙일지입니다. */
            struct LineRange
            {
                uint32 _begin{ 0 };
                uint32 _end{ 0 };
                bool   _bEllipsis{ false };
            };

            /** @brief 줄 하나의 가장 큰 면 메트릭을 모읍니다 — 대체 면이 섞인 줄이 겹치지 않게. */
            struct LineFaceMetrics
            {
                float32 _maxAscender{ 0.0f };
                float32 _minDescender{ 0.0f };
                float32 _maxLineGap{ 0.0f };
                bool    _bHasMetrics{ false };

                /** @brief 면 하나를 그 글꼴 크기로 더합니다(값은 UI 단위). */
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

            /** @brief 리치 텍스트 구간이 바꾼 글꼴(굵게 · 기울임)입니다. */
            static FontSpec makeSpanFont( const FontSpec& base, const RichTextSpan& span )
            {
                FontSpec font = base;
                if ( span._bBold == SW_TRUE && static_cast<uint16>( font._weight ) < static_cast<uint16>( FontWeight::Bold ) )
                    font._weight = FontWeight::Bold;
                if ( span._bItalic == SW_TRUE )
                    font._slant = FontSlant::Italic;
                return font;
            }

            /** @brief 줄임표 글리프입니다(면 · 글리프 · 폭). */
            struct EllipsisGlyph
            {
                FontFaceID _face{ kInvalidFontFaceID };
                uint32     _glyphIndex{ 0 };
                uint32     _count{ 0 };
                float32    _width{ 0.0f }; ///< 글리프 하나의 폭(UI 단위)
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    TextLayoutEngine::TextLayoutEngine( FontSystem& fontSystem )
        : _fontSystem{ fontSystem }
        , _shaper{}
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
        , _mapMeasure{}
        , _measureScratch{}
    {
    }

    void TextLayoutEngine::shapeSegment( string_view text, size_t segmentStart, size_t segmentEnd, const TextLayoutStyle& style, const FontFaceChain& chain,
                                         const RichTextSpan* pSpan )
    {
        using Internal                   = TextLayoutInternal;
        const FontFaceChain segmentChain = pSpan != nullptr ? _fontSystem.getFaceChain( Internal::makeSpanFont( style._font, *pSpan ) ) : chain;
        const float32       fontSize     = pSpan != nullptr ? style._fontSize * pSpan->_sizeScale : style._fontSize;
        const uint32        colorRgba    = pSpan != nullptr ? pSpan->_colorRgba : 0xFFFFFFFFu;
        const uint8         fauxBits     = Internal::makeFauxBits( segmentChain );
        const size_t        firstGlyph   = _listShapedScratch.size();
        TextItemizer::itemize( _fontSystem, segmentChain, text.substr( segmentStart, segmentEnd - segmentStart ), _listRunScratch );
        const IFontRasterizer& rasterizer = _fontSystem.getRasterizer();
        for ( ShapingRun& run : _listRunScratch )
        {
            run._byteOffset += static_cast<uint32>( segmentStart ); // 클러스터를 전체 글 기준으로
            _shaper.shape( rasterizer, run, _listShapedScratch );
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
            // 구간 경계에서 끊는다 — 구간 밖은 기본 스타일, 구간 안은 그 구간의 글꼴 · 크기 · 색.
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
                kind = Internal::BreakKind::None; // 글 끝 뒤에는 끊을 것이 없다
            _listBreakScratch[index] = static_cast<uint8>( kind );
        }
    }

    bool TextLayoutEngine::resolveGlyphLevels( string_view text, TextDirection paragraphDirection )
    {
        // 수준은 원문 코드 포인트로 정한다 — 셰이퍼가 버린 폭 없는 방향 제어(RLO · PDF)도 수준을 바꾼다. 글리프는 클러스터(원문 바이트)로 짝짓는다.
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
            return false; // LTR 문단에 RTL 문자 · RLO 가 없으면 모든 수준이 0 이다
        TextBidi::resolveLevels( _listCodepointScratch, paragraphDirection, _listLevelScratch );

        // 클러스터는 논리 순서라 줄지 않는다 — 코드 포인트를 앞으로만 민다. codepointEnd = 지금 코드 포인트 다음 바이트.
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
        if ( text.empty() || _fontSystem.isInitialized() == false )
            return;

        const FontFaceChain chain = _fontSystem.getFaceChain( style._font );
        if ( chain._faceCount == 0 )
            return;
        shapeText( text, style, chain, pListSpan );
        const uint32 glyphCount     = static_cast<uint32>( _listShapedScratch.size() );
        const bool   bReorder       = resolveGlyphLevels( text, style._paragraphDirection );
        const uint8  paragraphLevel = TextBidi::getParagraphLevel( style._paragraphDirection );
        const bool   bWrap          = style._bWrap && maxWidth > 0.0f;

        // 1) 욕심쟁이 줄 채우기.
        vector<Internal::LineRange> listLine;
        uint32                      lineStart  = 0;
        float32                     penX       = 0.0f;
        bool                        bHasBreak  = false;
        uint32                      lastBreak  = 0;
        uint32                      glyphIndex = 0;
        while ( glyphIndex < glyphCount )
        {
            const uint32  codepoint = _listShapedScratch[glyphIndex]._codepoint;
            const float32 width     = _listWidthScratch[glyphIndex];
            const bool    bOverflow = bWrap && glyphIndex > lineStart && Internal::isTrailingBlank( codepoint ) == false &&
                                   penX + width > maxWidth + Internal::kWidthTolerance;
            if ( bOverflow )
            {
                uint32 lineEnd = glyphIndex;
                if ( bHasBreak )
                {
                    lineEnd = lastBreak + 1;
                }
                else
                {
                    // 기회가 없다(한 낱말이 줄보다 길다) — 그 자리에서 끊되, 닫는 부호를 줄 머리로 · 여는 부호를 줄 끝으로 보내지 않게 물린다.
                    while ( lineEnd > lineStart + 1 && ( Internal::forbidsBreakBefore( _listShapedScratch[lineEnd]._codepoint ) ||
                                                         Internal::forbidsBreakAfter( _listShapedScratch[lineEnd - 1]._codepoint ) ) )
                    {
                        --lineEnd;
                    }
                }
                listLine.push_back( Internal::LineRange{ lineStart, lineEnd, false } );
                lineStart = lineEnd;
                penX      = 0.0f;
                bHasBreak = false;
                for ( uint32 carried = lineStart; carried < glyphIndex; ++carried )
                {
                    penX += _listWidthScratch[carried];
                    if ( static_cast<Internal::BreakKind>( _listBreakScratch[carried] ) == Internal::BreakKind::Allowed )
                    {
                        bHasBreak = true;
                        lastBreak = carried;
                    }
                }
                continue; // 이 글리프를 새 줄에서 다시 본다
            }
            penX += width;
            const Internal::BreakKind kind = static_cast<Internal::BreakKind>( _listBreakScratch[glyphIndex] );
            if ( kind == Internal::BreakKind::Allowed )
            {
                bHasBreak = true;
                lastBreak = glyphIndex;
            }
            else if ( kind == Internal::BreakKind::Mandatory )
            {
                listLine.push_back( Internal::LineRange{ lineStart, glyphIndex + 1, false } );
                lineStart = glyphIndex + 1;
                penX      = 0.0f;
                bHasBreak = false;
            }
            ++glyphIndex;
        }
        if ( lineStart < glyphCount )
            listLine.push_back( Internal::LineRange{ lineStart, glyphCount, false } );

        // 2) 줄 수 · 너비 제한 — 넘치면 자르고, 줄임표면 마지막(넘친) 줄에 붙인다.
        const bool bEllipsis = style._overflow == TextOverflow::Ellipsis;
        if ( style._maxLines > 0 && listLine.size() > style._maxLines )
        {
            listLine.resize( style._maxLines );
            outResult._bTruncated      = SW_TRUE;
            listLine.back()._bEllipsis = bEllipsis;
        }
        if ( bWrap == false && maxWidth > 0.0f )
        {
            for ( Internal::LineRange& line : listLine )
            {
                float32 lineWidth = 0.0f;
                for ( uint32 index = line._begin; index < line._end; ++index )
                {
                    lineWidth += _listWidthScratch[index];
                }
                if ( lineWidth > maxWidth + Internal::kWidthTolerance )
                {
                    outResult._bTruncated = SW_TRUE;
                    line._bEllipsis       = line._bEllipsis || bEllipsis;
                }
            }
        }

        IFontRasterizer&        rasterizer = _fontSystem.getRasterizer();
        Internal::EllipsisGlyph ellipsis{};
        const bool              bAnyEllipsis = outResult._bTruncated == SW_TRUE && bEllipsis;
        if ( bAnyEllipsis )
        {
            ellipsis._face  = _fontSystem.findFaceForCodepoint( chain, Internal::kEllipsisCodepoint, ellipsis._glyphIndex );
            ellipsis._count = 1;
            if ( ellipsis._glyphIndex == 0 )
            {
                ellipsis._face  = _fontSystem.findFaceForCodepoint( chain, '.', ellipsis._glyphIndex );
                ellipsis._count = Internal::kEllipsisFallbackCount;
            }
            GlyphMetrics metrics{};
            if ( rasterizer.findGlyphMetrics( ellipsis._face, ellipsis._glyphIndex, metrics ) )
                ellipsis._width = ( metrics._advance + style._letterSpacing ) * style._fontSize;
        }

        // 3) 줄마다 글리프 · 메트릭 · 기준선(정렬 전, x 는 줄 왼쪽 기준).
        float32 lineTop      = 0.0f;
        float32 maxLineWidth = 0.0f;
        for ( size_t lineIndex = 0; lineIndex < listLine.size(); ++lineIndex )
        {
            const Internal::LineRange& range      = listLine[lineIndex];
            uint32                     end        = range._end;
            float32                    trimmed    = 0.0f;
            uint32                     visibleEnd = end;
            // 끝 공백 · 줄 바꿈은 너비에서 뺀다(글리프는 남긴다 — 커서 · 선택이 쓴다).
            while ( visibleEnd > range._begin && Internal::isTrailingBlank( _listShapedScratch[visibleEnd - 1]._codepoint ) )
            {
                --visibleEnd;
            }
            for ( uint32 index = range._begin; index < visibleEnd; ++index )
            {
                trimmed += _listWidthScratch[index];
            }
            const float32 ellipsisWidth = range._bEllipsis ? ellipsis._width * static_cast<float32>( ellipsis._count ) : 0.0f;
            if ( range._bEllipsis )
            {
                // 줄임표가 들어갈 때까지 뒤에서 글리프를 뺀다(빼고 남은 끝 공백도 뺀다).
                while ( visibleEnd > range._begin && maxWidth > 0.0f && trimmed + ellipsisWidth > maxWidth + Internal::kWidthTolerance )
                {
                    --visibleEnd;
                    trimmed -= _listWidthScratch[visibleEnd];
                }
                while ( visibleEnd > range._begin && Internal::isTrailingBlank( _listShapedScratch[visibleEnd - 1]._codepoint ) )
                {
                    --visibleEnd;
                    trimmed -= _listWidthScratch[visibleEnd];
                }
                end = visibleEnd;
            }

            // 줄의 가장 큰 면 메트릭 — 대체 면이 섞인 줄이 겹치지 않게.
            Internal::LineFaceMetrics faceMetrics{};
            faceMetrics.accumulate( rasterizer, chain._arrFace[0], style._fontSize );
            for ( uint32 index = range._begin; index < end; ++index )
            {
                faceMetrics.accumulate( rasterizer, _listShapedScratch[index]._face, _listSizeScratch[index] );
            }
            if ( range._bEllipsis )
                faceMetrics.accumulate( rasterizer, ellipsis._face, style._fontSize );
            const float32 maxAscender   = faceMetrics._maxAscender;
            const float32 naturalHeight = maxAscender - faceMetrics._minDescender;
            const float32 lineHeight    = ( naturalHeight + faceMetrics._maxLineGap ) * style._lineHeight;

            LaidOutLine line{};
            line._firstGlyph = static_cast<uint32>( outResult._listGlyph.size() );
            line._width      = trimmed + ellipsisWidth;
            line._height     = lineHeight;
            line._baseline   = lineTop + ( lineHeight - naturalHeight ) * 0.5f + maxAscender;
            line._firstByte  = range._begin < glyphCount ? _listShapedScratch[range._begin]._cluster : static_cast<uint32>( text.size() );
            const uint32 nextByte =
                lineIndex + 1 < listLine.size() && listLine[lineIndex + 1]._begin < glyphCount ? _listShapedScratch[listLine[lineIndex + 1]._begin]._cluster : static_cast<uint32>( text.size() );
            line._byteCount = nextByte - line._firstByte;

            // 눈에 보이는 순서: 줄 안 글리프 [첫, end) 뒤에 줄임표(문단 수준)를 논리로 붙이고, 줄 끝 공백은 문단 수준(L1)으로 되돌린 뒤 뒤집는다(L2).
            const uint32 lineGlyphCount = end - range._begin;
            const uint32 itemCount      = lineGlyphCount + ( range._bEllipsis ? ellipsis._count : 0u );
            _listVisualScratch.resize( itemCount );
            if ( bReorder )
            {
                _listLineLevelScratch.assign( itemCount, paragraphLevel );
                for ( uint32 index = 0; index < lineGlyphCount; ++index )
                {
                    _listLineLevelScratch[index] = _listGlyphLevelScratch[range._begin + index];
                }
                for ( uint32 index = lineGlyphCount; index > 0 && Internal::isTrailingBlank( _listShapedScratch[range._begin + index - 1]._codepoint ); --index )
                {
                    _listLineLevelScratch[index - 1] = paragraphLevel;
                }
                TextBidi::reorderVisually( _listLineLevelScratch, _listVisualScratch );
            }
            else
            {
                for ( uint32 index = 0; index < itemCount; ++index )
                {
                    _listVisualScratch[index] = index;
                }
            }

            // RTL 문단의 끝 공백은 왼쪽 끝으로 간다 — 너비에서 뺀 만큼 왼쪽 밖에서 시작해 보이는 글리프가 [0, 너비] 에 놓이게.
            float32 pen = 0.0f;
            if ( TextBidi::isRightToLeftLevel( paragraphLevel ) )
            {
                for ( uint32 index = visibleEnd; index < end; ++index )
                {
                    pen -= _listWidthScratch[index];
                }
            }
            for ( const uint32 item : _listVisualScratch )
            {
                if ( item >= lineGlyphCount )
                {
                    LaidOutGlyph glyph{};
                    glyph._origin      = float2{ pen, line._baseline };
                    glyph._fontSize    = style._fontSize;
                    glyph._glyphIndex  = ellipsis._glyphIndex;
                    glyph._cluster     = end < glyphCount ? _listShapedScratch[end]._cluster : static_cast<uint32>( text.size() );
                    glyph._face        = ellipsis._face;
                    glyph._bFauxBold   = chain._bFauxBold;
                    glyph._bFauxItalic = chain._bFauxItalic;
                    outResult._listGlyph.push_back( glyph );
                    pen += ellipsis._width;
                    continue;
                }
                const uint32       index  = range._begin + item;
                const ShapedGlyph& shaped = _listShapedScratch[index];
                if ( Internal::isLineBreakCharacter( shaped._codepoint ) == false )
                {
                    LaidOutGlyph  glyph{};
                    const float32 glyphSize = _listSizeScratch[index];
                    const uint8   fauxBits  = _listFauxScratch[index];
                    glyph._origin           = float2{ pen + shaped._offset._x * glyphSize, line._baseline - shaped._offset._y * glyphSize };
                    glyph._fontSize         = glyphSize;
                    glyph._glyphIndex       = shaped._glyphIndex;
                    glyph._cluster          = shaped._cluster;
                    glyph._colorRgba        = _listColorScratch[index];
                    glyph._face             = shaped._face;
                    glyph._bFauxBold        = ( fauxBits & Internal::kFauxBoldBit ) != 0 ? SW_TRUE : SW_FALSE;
                    glyph._bFauxItalic      = ( fauxBits & Internal::kFauxItalicBit ) != 0 ? SW_TRUE : SW_FALSE;
                    // RTL 수준의 괄호는 짝 글리프로 그린다(L4). 면에 짝이 없으면 그대로.
                    const bool   bRightToLeftGlyph = bReorder && TextBidi::isRightToLeftLevel( _listGlyphLevelScratch[index] );
                    const uint32 mirrored          = bRightToLeftGlyph ? TextBidi::getMirroredCodepoint( shaped._codepoint ) : shaped._codepoint;
                    if ( mirrored != shaped._codepoint )
                    {
                        const uint32 mirroredGlyph = rasterizer.findGlyphIndex( shaped._face, mirrored );
                        if ( mirroredGlyph != 0 )
                            glyph._glyphIndex = mirroredGlyph;
                    }
                    outResult._listGlyph.push_back( glyph );
                }
                pen += _listWidthScratch[index];
            }
            line._glyphCount = static_cast<uint32>( outResult._listGlyph.size() ) - line._firstGlyph;
            outResult._listLine.push_back( line );
            lineTop += lineHeight;
            maxLineWidth = MathUtil::max( maxLineWidth, line._width );
        }
        outResult._size = float2{ maxLineWidth, lineTop };

        // 4) 정렬 — 상자 너비는 maxWidth(무한이면 가장 넓은 줄). Start · End 는 문단 방향을 따른다(RTL 이면 Start = 오른쪽).
        const float32 boxWidth   = maxWidth > 0.0f ? maxWidth : maxLineWidth;
        const bool    bRightPara = TextBidi::isRightToLeftLevel( paragraphLevel );
        const bool    bCenter    = style._alignment == TextAlignment::Center;
        const bool    bEnd       = style._alignment == TextAlignment::Right || ( style._alignment == TextAlignment::End && bRightPara == false ) ||
                          ( style._alignment == TextAlignment::Start && bRightPara );
        const float32 alignFactor = bCenter ? 0.5f : ( bEnd ? 1.0f : 0.0f );
        if ( alignFactor == 0.0f )
            return;
        for ( const LaidOutLine& line : outResult._listLine )
        {
            const float32 shift = MathUtil::max( 0.0f, boxWidth - line._width ) * alignFactor;
            for ( uint32 index = line._firstGlyph; index < line._firstGlyph + line._glyphCount; ++index )
            {
                outResult._listGlyph[index]._origin._x += shift;
            }
        }
    }

    float2 TextLayoutEngine::measure( string_view text, const TextLayoutStyle& style, float32 maxWidth, const vector<RichTextSpan>* pListSpan )
    {
        using Internal = TextLayoutInternal;
        if ( text.empty() || _fontSystem.isInitialized() == false )
            return float2{};
        const FontFaceChain chain = _fontSystem.getFaceChain( style._font );
        uint64              key   = StringUtil::computeHash64( text.data(), text.size(), false, Internal::hashStyle( style ) );
        key                       = Internal::hashFloat( key, maxWidth );
        for ( uint32 index = 0; index < chain._faceCount; ++index )
        {
            key = HashUtil::combine( key, chain._arrFace[index] );
        }
        if ( pListSpan != nullptr )
        {
            for ( const RichTextSpan& span : *pListSpan )
            {
                key = HashUtil::combine( key, ( static_cast<uint64>( span._firstByte ) << 32 ) | span._byteCount );
                key = HashUtil::combine( key, span._colorRgba );
                key = Internal::hashFloat( key, span._sizeScale );
                key = HashUtil::combine( key, ( static_cast<uint64>( span._bBold ) << 1 ) | span._bItalic );
            }
        }
        const auto iter = _mapMeasure.find( key );
        if ( iter != _mapMeasure.end() )
            return iter->second;
        layout( text, style, maxWidth, _measureScratch, pListSpan );
        if ( _mapMeasure.size() >= Internal::kMaxMeasureCacheEntryCount )
            _mapMeasure.clear();
        _mapMeasure.emplace( key, _measureScratch._size );
        return _measureScratch._size;
    }
} // namespace sw
