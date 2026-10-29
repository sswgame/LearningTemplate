/**
 * @file SimpleTextShaper.cpp
 * @brief 단순 셰이퍼를 정의합니다. 코드 포인트에서 글리프(cmap)와 커닝을 구합니다. 합자, 위치 테이블, 결합 문자 겹쳐 그리기는 없습니다.
 * @details 라틴, 한글(완성형 음절), 한자, 가나는 이것으로 맞다. 아랍어 연결형, 인도계 문자, 합자, 결합 분음(U+0300..U+036F)은
 *          HarfBuzz 가 필요한 경계다. 결합 분음과 폭 없는 제어 문자는 글리프를 내지 않고 버린다(Engine/Text/README.md).
 *          `ITextShaper::createDefault` 만 만들어 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Text/IFontRasterizer.h"
#include "Engine/Text/ITextShaper.h"
#include "Engine/Text/TextItemizer.h"

#if defined( SW_TEXT_COMPLEX_SHAPING )
    #include "Engine/Text/HarfBuzz/ComplexTextShaper.h"
#endif

namespace sw
{
    /**
     * @class SimpleTextShaper
     * @brief cmap + 커닝 셰이퍼입니다(`ITextShaper` 의 기본 구현).
     */
    class SimpleTextShaper final : public ITextShaper
    {
    public:
        void shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const override;
    };
} // namespace sw

namespace sw
{
    unique_ptr<ITextShaper> ITextShaper::createDefault()
    {
#if defined( SW_TEXT_COMPLEX_SHAPING )
        return ComplexTextShaperUtil::create();
#else
        return make_unique<SimpleTextShaper>();
#endif
    }

    void SimpleTextShaper::shape( const IFontRasterizer& rasterizer, const ShapingRun& run, vector<ShapedGlyph>& inoutListGlyph ) const
    {
        size_t offset         = 0;
        uint32 previousGlyph  = 0;
        size_t previousOutput = 0;
        bool   bHasPrevious   = false;
        while ( offset < run._text.size() )
        {
            const size_t clusterOffset = offset;
            const uint32 codepoint     = StringUtil::decodeUTF8( run._text, offset );
            // 폭 없는 문자(ZWJ, ZWNJ, 방향 제어, 변이 선택자, 결합 분음)는 단순 셰이퍼에서 글리프를 내지 않는다.
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
            // 커닝 쌍은 눈에 보이는 (왼쪽, 오른쪽)이고 왼쪽 글리프의 전진에 더한다. RTL 런은 배치가 뒤집어 두므로 지금 글리프가 앞 글리프의 왼쪽에 선다.
            const bool bKerns = bHasPrevious && previousGlyph != 0 && glyph._glyphIndex != 0;
            if ( bKerns && run._direction == TextDirection::RightToLeft )
                glyph._advance += rasterizer.getKerning( run._face, glyph._glyphIndex, previousGlyph );
            else if ( bKerns )
                inoutListGlyph[previousOutput]._advance += rasterizer.getKerning( run._face, previousGlyph, glyph._glyphIndex );
            previousGlyph  = glyph._glyphIndex;
            previousOutput = inoutListGlyph.size();
            bHasPrevious   = true;
            inoutListGlyph.push_back( glyph );
        }
    }
} // namespace sw
