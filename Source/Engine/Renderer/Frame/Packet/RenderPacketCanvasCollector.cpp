#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketCanvasCollector.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasOverlay.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/UI/Screen/UIOffscreenScreens.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/World/UIWidgetComponentService.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    RenderPacketCanvasCollector::RenderPacketCanvasCollector() = default;

    RenderPacketCanvasCollector::~RenderPacketCanvasCollector() = default;

    void RenderPacketCanvasCollector::populate( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI )
    {
        // 캔버스(화면 2D) — 런타임 UI 가 칠한 목록(내용 번호가 같으면 렌더러가 사각형을 다시 올리지 않는다), 등록된 겹치기는 그 위에.
        // 글리프 아틀라스의 새 구간은 칠한 것이 없어도 늘 넘긴다(렌더러의 거울이 게임 스레드의 페이지와 어긋나지 않게).
        const bool bUIReady          = pUISystem != nullptr && pUISystem->isInitialized();
        const bool bSubmitMainCanvas = bSceneMain == false && bUIReady && pUISystem->getPaintState().getCanvas().isEmpty() == false;
        if ( bSubmitMainCanvas )
        {
            const UIScreenMarkers& markers  = engine::getSubsystem<UIWidgetComponentService>().getMarkers();
            packet._canvas._pMainOutput     = pUISystem->getPaintState().getSharedCanvas(); // 포인터만 싣는다(베끼지 않는다)
            packet._canvas._listAnchor      = markers.getCanvasAnchors();                   // 화면 마커 위치 — 사각형과 달리 프레임마다 바뀐다
            packet._canvas._contentRevision = pUISystem->getPaintState().getCanvasRevision();
            // GPU 가 투영하는 마커 — 월드 점만 싣고 화면 좌표는 렌더러의 컴퓨트(`CanvasMarkerProjection::record`)가 앵커 원소에 쓴다. 켜진 마커가 없으면 싣지 않는다.
            if ( markers.getGPUMarkerCount() > 0 )
            {
                packet._canvas._listMarker = markers.getGPUMarkers();
                packet._canvas._markerView = markers.getGPUMarkerView();
            }
        }
        if ( bUIReady )
        {
            engine::getSubsystem<UIWidgetComponentService>().appendWorldCanvases( packet._canvas._listTarget ); // 월드 공간 UI 의 렌더 텍스처
            engine::getSubsystem<UIOffscreenScreens>().appendCanvases( packet._canvas._listTarget );            // 화면 스택 밖 화면(에디터 UI 미리보기)
        }
        // 개발 겹치기(디버그 드로우 · 테스트 그림 — 개발 도구가 `SW_CANVAS_OVERLAY` 로 등록한다)를 UI 위에 칠한다. 등록이 없으면(배포본) 값도 만들지 않는다.
        // 글리프 캐시의 프레임 시계다(프로파일러 프레임 수는 꺼지면 늘 0 이라 래스터화 예산이 되살아나지 않는다). 캐시가 없으면 글리프를 그리지 않으므로 값이 쓰이지 않는다.
        const bool bPaintOverlays = bSceneMain == false && CanvasOverlays::getCount() > 0 && pRHI != nullptr && pRHI->hasDevice();
        if ( bPaintOverlays )
        {
            const uint32         canvasWidth  = packet._viewportWidth > 0 ? packet._viewportWidth : pRHI->getDevice().getBackBufferWidth();
            const uint32         canvasHeight = packet._viewportHeight > 0 ? packet._viewportHeight : pRHI->getDevice().getBackBufferHeight();
            CanvasOverlayContext context{};
            context._pViewProj       = packet._bHasViewProj == SW_TRUE ? &packet._viewProj : nullptr;
            context._targetSize      = float2{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) };
            context._pFontService    = pFontService;
            context._glyphFrameIndex = pFontService != nullptr && pFontService->isInitialized() ? pFontService->getGlyphCache().getFrameIndex() : 0;
            CanvasOverlayTarget target{ packet._canvas, context._targetSize };
            CanvasOverlays::paintAll( context, target );
        }
        if ( pFontService != nullptr && pFontService->isInitialized() )
        {
            packet._canvas._pAtlasSource = &pFontService->getGlyphCache().getAtlas();
            packet._canvas._pAtlasSource->takeUploads( packet._canvas._listAtlasUpload );
        }
        // 색각 보정 — UI 는 톤맵 뒤라 캔버스 셰이더가 직접 건다(사용자 설정 accessibility.colorVision, 범위 밖은 끔).
        packet._canvas._colorVisionMode = ( 0 <= gv_colorVisionMode && gv_colorVisionMode <= 3 ) ? static_cast<uint32>( gv_colorVisionMode ) : 0u;
    }
} // namespace sw
