#include "pch.h"

#include "Engine/Text/SimpleTextShaper.h"

#include "Core/String/StringUtil.h"

#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/TextItemizer.h"

namespace sw
{
    void SimpleTextShaper::shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const
    {
        size_t offset         = 0;
        uint32 previousGlyph  = 0;
        size_t previousOutput = 0;
        bool   bHasPrevious   = false;
        while ( offset < run._text.size() )
        {
            const size_t clusterOffset = offset;
            const uint32 codepoint     = StringUtil::decodeUtf8( run._text, offset );
            // 폭 없는 문자(ZWJ · ZWNJ · 방향 제어 · 변이 선택자 · 결합 분음)는 단순 셰이퍼에서 글리프를 내지 않는다.
            if ( TextItemizer::isZeroWidth( codepoint ) )
                continue;
            ShapedGlyph glyph{};
            glyph._face       = run._face;
            glyph._glyphIndex = rasterizer.findGlyphIndex( run._face, codepoint );
            glyph._cluster    = run._byteOffset + static_cast<uint32>( clusterOffset );
            glyph._codepoint  = codepoint;
            GlyphMetrics metrics{};
            if ( rasterizer.findGlyphMetrics( run._face, glyph._glyphIndex, metrics ) )
                glyph._advance = metrics._advance;
            const bool bKerns = bHasPrevious && previousGlyph != 0 && glyph._glyphIndex != 0;
            if ( bKerns )
                inoutListGlyph[previousOutput]._advance += rasterizer.getKerning( run._face, previousGlyph, glyph._glyphIndex );
            previousGlyph  = glyph._glyphIndex;
            previousOutput = inoutListGlyph.size();
            bHasPrevious   = true;
            inoutListGlyph.push_back( glyph );
        }
    }
} // namespace sw
