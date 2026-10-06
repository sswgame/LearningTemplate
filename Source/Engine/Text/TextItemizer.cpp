#include "pch.h"

#include "Engine/Text/TextItemizer.h"

#include "Core/String/StringUtil.h"

#include "Engine/Text/FontSystem.h"

namespace sw
{
    bool TextItemizer::isStrongRightToLeft( uint32 codepoint )
    {
        const bool bHebrewArabicSyriacThaana = 0x0590u <= codepoint && codepoint <= 0x08FFu;
        const bool bPresentationFormsA       = 0xFB1Du <= codepoint && codepoint <= 0xFDFFu;
        const bool bPresentationFormsB       = 0xFE70u <= codepoint && codepoint <= 0xFEFFu;
        // 아랍 숫자(U+0660..U+0669 · U+06F0..U+06F9)는 약한 문자다 — 방향을 바꾸지 않는다.
        const bool bArabicDigit = ( 0x0660u <= codepoint && codepoint <= 0x0669u ) || ( 0x06F0u <= codepoint && codepoint <= 0x06F9u );
        return ( bHebrewArabicSyriacThaana || bPresentationFormsA || bPresentationFormsB ) && bArabicDigit == false;
    }

    bool TextItemizer::isNeutral( uint32 codepoint )
    {
        if ( codepoint < 0x80u )
        {
            const bool bLetter = ( 'A' <= codepoint && codepoint <= 'Z' ) || ( 'a' <= codepoint && codepoint <= 'z' );
            return bLetter == false;
        }
        const bool bLatin1Symbol         = ( 0x00A0u <= codepoint && codepoint <= 0x00BFu ) || codepoint == 0x00D7u || codepoint == 0x00F7u;
        const bool bGeneralPunctuation   = 0x2000u <= codepoint && codepoint <= 0x206Fu;
        const bool bCjkSymbol            = 0x3000u <= codepoint && codepoint <= 0x303Fu;
        const bool bFullwidthPunctuation = ( 0xFF00u <= codepoint && codepoint <= 0xFF20u ) || ( 0xFF3Bu <= codepoint && codepoint <= 0xFF40u ) ||
                                           ( 0xFF5Bu <= codepoint && codepoint <= 0xFF65u );
        const bool bArabicDigit = ( 0x0660u <= codepoint && codepoint <= 0x0669u ) || ( 0x06F0u <= codepoint && codepoint <= 0x06F9u );
        return bLatin1Symbol || bGeneralPunctuation || bCjkSymbol || bFullwidthPunctuation || bArabicDigit || isZeroWidth( codepoint );
    }

    bool TextItemizer::isZeroWidth( uint32 codepoint )
    {
        const bool bFormatControl     = 0x200Bu <= codepoint && codepoint <= 0x200Fu;
        const bool bSeparatorEmbed    = 0x2028u <= codepoint && codepoint <= 0x202Eu;
        const bool bVariationSelector = 0xFE00u <= codepoint && codepoint <= 0xFE0Fu;
        const bool bCombiningMark     = 0x0300u <= codepoint && codepoint <= 0x036Fu;
        return bFormatControl || bSeparatorEmbed || bVariationSelector || bCombiningMark || codepoint == 0xFEFFu;
    }

    void TextItemizer::itemize( FontSystem& fontSystem, const FontFaceChain& chain, string_view text, vector<ShapingRun>& outListRun )
    {
        outListRun.clear();
        if ( chain._faceCount == 0 )
            return;
        IFontRasterizer& rasterizer = fontSystem.getRasterizer();
        bool             bHasRun    = false;
        ShapingRun       current{};
        size_t           offset = 0;
        while ( offset < text.size() )
        {
            const size_t  start     = offset;
            const uint32  codepoint = StringUtil::decodeUtf8( text, offset );
            FontFaceId    face      = kInvalidFontFaceId;
            TextDirection direction = bHasRun ? current._direction : TextDirection::LeftToRight;
            const bool    bNeutral  = isNeutral( codepoint );
            if ( bNeutral && bHasRun )
            {
                // 중립 문자는 지금 런을 잇는다 — 그 면에 글리프가 있거나 폭이 없으면 면도 그대로.
                const bool bCurrentFaceHasIt = isZeroWidth( codepoint ) || rasterizer.findGlyphIndex( current._face, codepoint ) != 0;
                if ( bCurrentFaceHasIt )
                    face = current._face;
            }
            if ( face == kInvalidFontFaceId )
            {
                uint32 glyphIndex = 0;
                face              = fontSystem.findFaceForCodepoint( chain, codepoint, glyphIndex );
            }
            if ( bNeutral == false )
                direction = isStrongRightToLeft( codepoint ) ? TextDirection::RightToLeft : TextDirection::LeftToRight;

            const bool bContinues = bHasRun && face == current._face && direction == current._direction;
            if ( bContinues )
            {
                current._text = text.substr( current._byteOffset, offset - current._byteOffset );
                continue;
            }
            if ( bHasRun )
                outListRun.push_back( current );
            current._byteOffset = static_cast<uint32>( start );
            current._text       = text.substr( start, offset - start );
            current._face       = face;
            current._direction  = direction;
            bHasRun             = true;
        }
        if ( bHasRun )
            outListRun.push_back( current );
    }
} // namespace sw
