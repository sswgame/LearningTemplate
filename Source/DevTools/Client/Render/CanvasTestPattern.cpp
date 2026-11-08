#include "pch.h"

#include "DevTools/Client/Render/CanvasTestPattern.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Canvas/CanvasOverlay.h"
#include "Engine/Graphics/Canvas/CanvasPainter.h"
#include "Engine/Text/FontService.h"

namespace sw
{
    /**
     * @brief `-gv_canvasTestPattern=1`: 주 출력에 캔버스 테스트 그림(둥근 · 테두리 · 반투명 겹침 · 가위 · 둥근 자르기 · 그림자 · 글자)을 그립니다.
     * @details 위젯(UI 단계)이 생기기 전에 Canvas 패스 · canvas.hlsl · 글리프 아틀라스를 네 백엔드에서 눈으로 보는 길이다(스크린샷에도 들어간다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_canvasTestPattern, false, "주 출력에 캔버스(화면 2D) 테스트 그림을 그린다 — 사각형 · 자르기 · 그림자 · 글자" );

    namespace
    {
        struct CanvasTestPatternInternal
        {
            /** @brief 캔버스 겹치기: 켜져 있으면 테스트 그림을 칠한다. 내용 번호가 없어 늘 올린다. */
            static void paintOverlay( const CanvasOverlayContext& context, CanvasOverlayTarget& target )
            {
                if ( gv_canvasTestPattern == false )
                    return;
                target.markContentChanged();
                CanvasTestPattern::paint( target.acquire(), context._targetSize, context._pFontService, context._glyphFrameIndex );
            }
        };
    } // namespace

    // 주 출력 층 — 이 그림은 Canvas 패스 자체를 보는 확인 대상이라 사진 캡처 · 골든에 들어야 한다(개발 그림의 기본 층은 개발 겹치기다).
    SW_CANVAS_OVERLAY( CanvasTestPattern, "CanvasTestPattern", 1, CanvasOverlayLayer::MainOutput, &CanvasTestPatternInternal::paintOverlay );

    void CanvasTestPattern::paint( CanvasDrawList& outCanvas, const float2& targetSize, FontService* pFontService, uint64 frameIndex )
    {
        outCanvas._targetSize = targetSize;
        CanvasPainter painter( outCanvas, 1.0f );

        // 그림자 위의 패널(둥근 모서리, 테두리).
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
        CanvasPainter::drawLine( painter, *pFontService, "Canvas AaBb 0123", float2{ 48.0f, 230.0f }, title, frameIndex );
        CanvasGlyphStyle outlined{};
        outlined._fontSize     = 32.0f;
        outlined._color        = float4{ 1.0f, 0.85f, 0.3f, 1.0f };
        outlined._outlineColor = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        outlined._outlineWidth = 2.0f;
        // 저장소 글꼴(라틴)에는 한글이 없으므로 문화권별 대체 글꼴 체인의 시스템 글꼴을 쓴다. 시스템 글꼴도 없으면 빈 네모(두부)로 나온다.
        CanvasPainter::drawLine( painter, *pFontService, "UI 가나다 SDF", float2{ 48.0f, 290.0f }, outlined, frameIndex );
    }
} // namespace sw
