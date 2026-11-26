#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Canvas/CanvasOverlay.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Text/FontSubsystem.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/UI/Screen/UIOffscreenScreens.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/World/UIWidgetComponentSubsystem.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    RenderPacketBuilder::RenderPacketBuilder()
        : _packetScratch{ nullptr }
        , _gpuUploadQueue{ nullptr }
        , _scene{}
        , _views{}
        , _previews{}
    {
    }

    RenderPacketBuilder::~RenderPacketBuilder() = default;

    void RenderPacketBuilder::initialize( IRHIDevice& device, TaskManager* pTaskManager )
    {
        _scene.initialize( device );
        if ( _packetScratch == nullptr )
            _packetScratch = make_unique<RenderFramePacket>();
        if ( _gpuUploadQueue == nullptr )
            _gpuUploadQueue = make_unique<GPUUploadQueue>();
        _views.initialize();
        // 미리보기 씬도 주 화면과 같은 디바이스 규칙(배치 합치기)으로 채운다.
        _previews.initialize( device );
        _gpuUploadQueue->bindDevice( &device, pTaskManager );
    }

    void RenderPacketBuilder::shutdown()
    {
        _scene.shutdown();
        // 미리보기 빌더도 머티리얼 · 인스턴스를 든다 — 디바이스가 살아 있을 때 놓는다.
        _previews.clear();
    }

    void RenderPacketBuilder::destroy()
    {
        // 만든 순서(initialize)의 역순이다.
        _previews.clear();
        _views.destroy();
        _gpuUploadQueue.reset();
        _packetScratch.reset();
        _scene.destroy();
    }

    bool RenderPacketBuilder::isInitialized() const
    {
        return _packetScratch != nullptr && _scene.isCreated();
    }

    bool RenderPacketBuilder::isMergeBatchesAcrossMaterials() const
    {
        return _scene.isMergeBatchesAcrossMaterials();
    }

    RenderFramePacket& RenderPacketBuilder::beginPacket()
    {
        RenderFramePacket& packet = *_packetScratch;
        packet.resetForFrame();
        packet._bValid = 1;
        // 렌더 설정 스위치는 여기서 한 번 뜬다(게임 스레드) — 이 패킷을 짓는 쪽(그림자 텍셀 스냅)과 렌더 스레드가 같은 값을 본다.
        packet._settings = RenderFrameSettings::capture();
        // 패킷 밖 소비자(텍스처 로드 워커 · 업로드 펌프)의 사본도 같은 자리에서 다시 뜬다.
        if ( engine::areEngineSubsystemsBound() )
            engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>().refreshQualitySettings();
        return packet;
    }

    void RenderPacketBuilder::populateScene( RenderFramePacket& packet, const FrameViewports& views, const ViewCameraProviderDelegate& sceneViewCameraProvider,
                                             Scene* pActiveScene, float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer )
    {
        const ViewportTarget& mainOutput = views.getMainOutput();
        packet._outputRenderTarget       = mainOutput._renderTarget;
        // 크기는 여기서 한 번 푼다 — 대상의 0 은 "백버퍼(스왑체인) 크기" 라는 뜻이고, 패킷은 늘 실제 픽셀 수를 싣는다(읽는 쪽이 0 을 저마다 풀다가 렌즈 플레어는 0 을 "빈 화면" 으로 읽어 그리지 않았다).
        const bool bBackBufferSize = mainOutput._width == 0 || mainOutput._height == 0;
        packet._viewportWidth      = bBackBufferSize ? rhi.getDevice().getBackBufferWidth() : mainOutput._width;
        packet._viewportHeight     = bBackBufferSize ? rhi.getDevice().getBackBufferHeight() : mainOutput._height;

        if ( pActiveScene == nullptr )
        {
            // 스냅샷을 채우지 않는 프레임이다(게임 재시작이 씬을 닫은 사이). 다시 쓰는 패킷에 앞 씬의 인스턴스가 남으면 "주 시점 없이 3D 내용을 실은 패킷" 이 된다.
            packet._gpuScene = GPUSceneSnapshot{};
            return;
        }

        // 순서가 계약이다: 빛 · 하늘 → 주 시점(카메라 · 안개 · 투명 정렬 축) → 주 시점 그림자 → 추가 뷰 → 추가 뷰 그림자 → LOD 뷰 → 스냅샷.
        const RenderPacketKeyLights lights   = _scene.collectLights( packet, *pActiveScene );
        const RenderPacketMainView  mainView = _views.populateMainView( packet, views, sceneViewCameraProvider, *pActiveScene, pFrameRenderer );
        if ( mainView._pCamera != nullptr )
            _scene.applyMainCamera( packet, *pActiveScene, *mainView._pCamera );
        // 그림자 배치는 카메라가 정해진 **뒤에** 만든다 — 볼륨을 카메라가 보는 곳에 맞추는 빛이 있다.
        _scene.populateShadows( packet, lights );
        _views.collectExtraViews( packet, views, *pActiveScene, mainView, deltaTime, pFrameRenderer );
        _scene.populateViewShadows( packet, lights );
        _scene.updateLODViews( packet, *pActiveScene );
        _scene.populateGPUSceneSnapshot( packet, pActiveScene, _gpuUploadQueue.get() );
    }

    void RenderPacketBuilder::populateScenePreviews( RenderFramePacket& packet, const FrameViewports& views, const SceneManager& sceneManager )
    {
        _previews.populate( packet, views, sceneManager, _gpuUploadQueue.get() );
    }

    void RenderPacketBuilder::resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const
    {
        _scene.resolveMarkerInstanceSlots( packet, pUISystem, pActiveScene );
    }

    void RenderPacketBuilder::populateCanvas( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontSubsystem* pFontSubsystem, const RHI* pRHI )
    {
        // 캔버스(화면 2D) — 런타임 UI 가 칠한 목록(내용 번호가 같으면 렌더러가 사각형을 다시 올리지 않는다), 등록된 겹치기는 그 위에.
        // 글리프 아틀라스의 새 구간은 칠한 것이 없어도 늘 넘긴다(렌더러의 거울이 게임 스레드의 페이지와 어긋나지 않게).
        const bool bUIReady          = pUISystem != nullptr && pUISystem->isInitialized();
        const bool bSubmitMainCanvas = bSceneMain == false && bUIReady && pUISystem->getPaintState().getCanvas().isEmpty() == false;
        if ( bSubmitMainCanvas )
        {
            const UIScreenMarkers& markers  = engine::getSubsystem<UIWidgetComponentSubsystem>().getMarkers();
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
        // 개발 겹치기(디버그 HUD)는 화면 UI 와 다른 목록이다 — 렌더러가 사진 캡처를 읽은 뒤에 화면에 합친다. 주 출력이 호스트 렌더 타깃(에디터 게임 뷰)이면
        // 그 타깃(에디터 UI 캡처 · 게임 뷰 사진이 읽는다)에 그리지 않고 자기 렌더 텍스처로 싣는다 — 에디터가 게임 뷰 위에 겹치고, 에디터 UI 캡처는 그 텍스처를 빼고 다시 그린다.
        const UICanvasOutput* const pDevOverlay       = bUIReady ? &pUISystem->getPaintState().getDevOverlay() : nullptr;
        const bool                  bSubmitDevOverlay = bSceneMain == false && pDevOverlay != nullptr && pDevOverlay->getCanvas().isEmpty() == false;
        if ( bSubmitDevOverlay && packet._outputRenderTarget == 0 )
        {
            packet._canvas._pDevOverlay        = pDevOverlay->getSharedCanvas();
            packet._canvas._devOverlayRevision = pDevOverlay->getRevision();
        }
        else if ( bSubmitDevOverlay )
        {
            CanvasTargetDrawList& target = packet._canvas._listTarget.emplace_back();
            target._targetPath           = hashed_string( CanvasFrameData::kDevOverlayTargetPath );
            target._list                 = pDevOverlay->getCanvas(); // 에디터 게임 뷰에서만 드는 사본(개발 도구가 떠 있을 때)
            target._clearColor           = float4{};
            target._contentRevision      = pDevOverlay->getRevision();
        }
        if ( bUIReady )
        {
            engine::getSubsystem<UIWidgetComponentSubsystem>().appendWorldCanvases( packet._canvas._listTarget ); // 월드 공간 UI 의 렌더 텍스처
            engine::getSubsystem<UIOffscreenScreens>().appendCanvases( packet._canvas._listTarget );              // 화면 스택 밖 화면(에디터 UI 미리보기)
        }
        // 개발 겹치기(디버그 드로우 · 테스트 그림 — 개발 도구가 `SW_CANVAS_OVERLAY` 로 등록한다)를 등록이 밝힌 층(주 출력 UI 위 · 개발 겹치기 층)에 칠한다. 등록이 없으면(배포본) 값도 만들지 않는다.
        // 글리프 캐시의 프레임 시계다(프로파일러 프레임 수는 꺼지면 늘 0 이라 래스터화 예산이 되살아나지 않는다). 캐시가 없으면 글리프를 그리지 않으므로 값이 쓰이지 않는다.
        const bool bPaintOverlays = bSceneMain == false && CanvasOverlays::getCount() > 0 && pRHI != nullptr && pRHI->hasDevice();
        if ( bPaintOverlays )
        {
            const uint32         canvasWidth  = packet._viewportWidth; // 씬 채우기가 백버퍼 크기까지 풀어 두었다(`RenderPacketBuilder::populateScene`)
            const uint32         canvasHeight = packet._viewportHeight;
            CanvasOverlayContext context{};
            context._pViewProj       = packet._bHasViewProj == SW_TRUE ? &packet._viewProj : nullptr;
            context._targetSize      = float2{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) };
            context._pFontSubsystem  = pFontSubsystem;
            context._glyphFrameIndex = pFontSubsystem != nullptr && pFontSubsystem->isInitialized() ? pFontSubsystem->getGlyphCache().getFrameIndex() : 0;
            CanvasOverlayTarget target{ packet._canvas, context._targetSize, packet._outputRenderTarget != 0 };
            CanvasOverlays::paintAll( context, target );
        }
        if ( pFontSubsystem != nullptr && pFontSubsystem->isInitialized() )
        {
            packet._deltas._pAtlasSource = &pFontSubsystem->getGlyphCache().getAtlas();
            packet._deltas._pAtlasSource->takeUploads( packet._deltas._listAtlasUpload );
        }
        // 색각 보정 — UI 는 톤맵 뒤라 캔버스 셰이더가 직접 건다(사용자 설정 accessibility.colorVision, 범위 밖은 끔).
        packet._canvas._colorVisionMode = ( 0 <= gv_colorVisionMode && gv_colorVisionMode <= 3 ) ? static_cast<uint32>( gv_colorVisionMode ) : 0u;
    }
} // namespace sw
