#include "pch.h"

#include "Engine/Graphics/Canvas/CanvasTestPattern.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/Text/IFontRasterizer.h"

namespace sw
{
    namespace
    {
        /** @brief 글 한 줄을 왼쪽에서 오른쪽으로 칠합니다(셰이핑 · 커닝 없음 — 배치 단계가 생기기 전의 테스트 그림). */
        void drawLine( CanvasPainter& painter, FontService& fontService, string_view text, const float2& origin, const CanvasGlyphStyle& style,
                       uint64 frameIndex )
        {
            const FontFaceChain chain      = fontService.getFaceChain( FontSpec{} );
            CanvasGlyphStyle    glyphStyle = style;
            glyphStyle._bFauxBold          = chain._bFauxBold;
            glyphStyle._bFauxItalic        = chain._bFauxItalic;
            float32 penX                   = origin._x;
            size_t  offset                 = 0;
            while ( offset < text.size() )
            {
                const uint32     codepoint  = StringUtil::decodeUtf8( text, offset );
                uint32           glyphIndex = 0;
                const FontFaceID face       = fontService.findFaceForCodepoint( chain, codepoint, glyphIndex );
                if ( face == kInvalidFontFaceID )
                    continue;
                (void)painter.drawGlyph( float2{ penX, origin._y }, face, glyphIndex, glyphStyle, fontService.getGlyphCache(), frameIndex );
                GlyphMetrics metrics{};
                if ( fontService.getRasterizer().findGlyphMetrics( face, glyphIndex, metrics ) )
                    penX += metrics._advance * style._fontSize;
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    void CanvasTestPattern::paint( CanvasDrawList& outCanvas, const float2& targetSize, FontService* pFontService, uint64 frameIndex )
    {
        outCanvas._targetSize = targetSize;
        CanvasPainter painter( outCanvas, 1.0f );

        // 그림자 위의 패널(둥근 모서리 · 테두리).
        painter.drawShadow( float2{ 24.0f, 24.0f }, float2{ 520.0f, 300.0f }, float4{ 16.0f, 16.0f, 16.0f, 16.0f }, float4{ 0.0f, 0.0f, 0.0f, 0.6f }, 10.0f,
                            float2{ 6.0f, 8.0f } );
        CanvasBrush panel{};
        panel._color        = float4{ 0.08f, 0.09f, 0.12f, 0.85f };
        panel._borderColor  = float4{ 0.95f, 0.75f, 0.2f, 1.0f };
        panel._cornerRadius = float4{ 16.0f, 16.0f, 16.0f, 16.0f };
        panel._borderWidth  = 3.0f;
        painter.fillRect( float2{ 24.0f, 24.0f }, float2{ 520.0f, 300.0f }, panel );

        // 빨간 둥근 사각형과 반투명 파랑 겹침(겹친 곳은 프리멀티플라이로 보라).
        CanvasBrush red{};
        red._color        = float4{ 1.0f, 0.0f, 0.0f, 1.0f };
        red._cornerRadius = float4{ 12.0f, 12.0f, 12.0f, 12.0f };
        painter.fillRect( float2{ 48.0f, 48.0f }, float2{ 160.0f, 100.0f }, red );
        CanvasBrush blue{};
        blue._color = float4{ 0.0f, 0.3f, 1.0f, 0.5f };
        painter.fillRect( float2{ 160.0f, 80.0f }, float2{ 120.0f, 100.0f }, blue );

        // 가위 자르기(오른쪽 절반이 잘린다)와 둥근 자르기(원 안만).
        painter.pushClip( float2{ 300.0f, 48.0f }, float2{ 80.0f, 100.0f }, 0.0f );
        CanvasBrush green{};
        green._color = float4{ 0.1f, 0.9f, 0.2f, 1.0f };
        painter.fillRect( float2{ 300.0f, 48.0f }, float2{ 160.0f, 100.0f }, green );
        painter.popClip();
        painter.pushClip( float2{ 400.0f, 48.0f }, float2{ 100.0f, 100.0f }, 50.0f );
        CanvasBrush white{};
        white._color = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
        painter.fillRect( float2{ 400.0f, 48.0f }, float2{ 100.0f, 100.0f }, white );
        painter.popClip();

        if ( pFontService == nullptr || pFontService->isInitialized() == false )
            return;
        CanvasGlyphStyle title{};
        title._fontSize = 40.0f;
        title._color    = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
        drawLine( painter, *pFontService, "Canvas AaBb 0123", float2{ 48.0f, 230.0f }, title, frameIndex );
        CanvasGlyphStyle outlined{};
        outlined._fontSize     = 32.0f;
        outlined._color        = float4{ 1.0f, 0.85f, 0.3f, 1.0f };
        outlined._outlineColor = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        outlined._outlineWidth = 2.0f;
        // 한글은 저장소 글꼴(라틴)에 없어 문화권 대체 사슬의 시스템 글꼴로 간다(없으면 두부).
        drawLine( painter, *pFontService, "UI 가나다 SDF", float2{ 48.0f, 290.0f }, outlined, frameIndex );
    }
} // namespace sw
