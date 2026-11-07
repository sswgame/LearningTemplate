#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketCanvasCollector.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Canvas/CanvasTestPattern.h"
#include "Engine/Graphics/Canvas/DebugDrawCanvas.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
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
    /**
     * @brief `-gv_canvasTestPattern=1`: 주 출력에 캔버스 테스트 그림(둥근 · 테두리 · 반투명 겹침 · 가위 · 둥근 자르기 · 그림자 · 글자)을 그립니다.
     * @details 위젯(UI 단계)이 생기기 전에 Canvas 패스 · canvas.hlsl · 글리프 아틀라스를 네 백엔드에서 눈으로 보는 길이다(스크린샷에도 들어간다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_canvasTestPattern, false, "주 출력에 캔버스(화면 2D) 테스트 그림을 그린다 — 사각형 · 자르기 · 그림자 · 글자" );

    namespace
    {
        struct RenderPacketCanvasCollectorInternal
        {
            /**
             * @brief 개발 겹치기(디버그 그리기 · 테스트 그림)를 칠할 주 출력 사본을 돌려줍니다. 처음 부를 때 UI 목록을 베껴 패킷에 싣고, 같은 프레임의 다음 부름은 그 사본을 쓴다.
             * @details UI 목록은 공유 값이라 제자리에 칠하지 않는다. 겹치기가 없는 프레임(배포 · 보통 실행)은 베끼지 않는다 — 예전에는 늘 베꼈다.
             */
            static CanvasDrawList& ensureOverlayList( CanvasFrameData& inoutFrame, shared_ptr<CanvasDrawList>& inoutOverlay )
            {
                if ( inoutOverlay == nullptr )
                {
                    inoutOverlay            = make_shared<CanvasDrawList>( inoutFrame.getMainOutput() );
                    inoutFrame._pMainOutput = inoutOverlay;
                }
                return *inoutOverlay;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RenderPacketCanvasCollector::RenderPacketCanvasCollector()
        : _listDebugDrawLine{}
    {
    }

    RenderPacketCanvasCollector::~RenderPacketCanvasCollector() = default;

    void RenderPacketCanvasCollector::populate( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI )
    {
        // 캔버스(화면 2D) — 런타임 UI 가 칠한 목록(내용 번호가 같으면 렌더러가 사각형을 다시 올리지 않는다), 개발 테스트 그림은 그 위에.
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
        // 글리프 캐시의 프레임 시계다(프로파일러 프레임 수는 꺼지면 늘 0 이라 래스터화 예산이 되살아나지 않는다). 캐시가 없으면 글리프를 그리지 않으므로 값이 쓰이지 않는다.
        shared_ptr<CanvasDrawList> pOverlay; // 개발 겹치기를 칠할 사본(겹치기가 있는 프레임만)
        const uint64               glyphFrameIndex = pFontService != nullptr && pFontService->isInitialized() ? pFontService->getGlyphCache().getFrameIndex() : 0;
        // 디버그 드로우 겹치기 — UI 위, 테스트 그림 아래. 주 시점이 카메라에서 온 패킷만(행렬이 없으면 그릴 눈이 없다).
        const bool bSubmitDebugDraw = bSceneMain == false && DebugDrawCanvasUtil::isGameViewOverlayEnabled() && packet._bHasViewProj == SW_TRUE && pRHI != nullptr && pRHI->hasDevice();
        if ( bSubmitDebugDraw )
        {
            const uint32    canvasWidth  = packet._viewportWidth > 0 ? packet._viewportWidth : pRHI->getDevice().getBackBufferWidth();
            const uint32    canvasHeight = packet._viewportHeight > 0 ? packet._viewportHeight : pRHI->getDevice().getBackBufferHeight();
            const float2    targetSize{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) };
            CanvasDrawList& overlay = RenderPacketCanvasCollectorInternal::ensureOverlayList( packet._canvas, pOverlay );
            if ( overlay.isEmpty() )
                overlay._targetSize = targetSize;
            const uint32 paintedCount = DebugDrawCanvasUtil::paint( engine::getSubsystem<DebugDrawQueue>(), packet._viewProj, targetSize, pFontService, glyphFrameIndex,
                                                                    _listDebugDrawLine, overlay );
            if ( paintedCount > 0 )
                packet._canvas._contentRevision = 0; // 겹친 도형은 프레임마다 바뀐다 — 내용 번호 없이 늘 올린다(테스트 그림과 같다)
        }
        const bool bSubmitTestPattern = bSceneMain == false && gv_canvasTestPattern && pRHI != nullptr && pRHI->hasDevice();
        if ( bSubmitTestPattern )
        {
            packet._canvas._contentRevision = 0; // 테스트 그림은 내용 번호가 없다 — 늘 올린다
            const uint32 canvasWidth        = packet._viewportWidth > 0 ? packet._viewportWidth : pRHI->getDevice().getBackBufferWidth();
            const uint32 canvasHeight       = packet._viewportHeight > 0 ? packet._viewportHeight : pRHI->getDevice().getBackBufferHeight();
            CanvasTestPattern::paint( RenderPacketCanvasCollectorInternal::ensureOverlayList( packet._canvas, pOverlay ), float2{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) }, pFontService,
                                      glyphFrameIndex );
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
