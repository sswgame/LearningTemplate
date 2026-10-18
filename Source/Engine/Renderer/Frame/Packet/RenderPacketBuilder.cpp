#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Canvas/CanvasTestPattern.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Graphics/Upload/GPUUploadQueue.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"
#include "Engine/Renderer/Light/GPULightBuffer.h"
#include "Engine/Renderer/Light/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneBuilder.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Text/FontService.h"
#include "Engine/Text/GlyphCache.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    /**
     * @brief `-gv_canvasTestPattern=1`: 주 출력에 캔버스 테스트 그림(둥근 · 테두리 · 반투명 겹침 · 가위 · 둥근 자르기 · 그림자 · 글자)을 그립니다.
     * @details 위젯(UI 단계)이 생기기 전에 Canvas 패스 · canvas.hlsl · 글리프 아틀라스를 네 백엔드에서 눈으로 보는 길이다(스크린샷에도 들어간다).
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_canvasTestPattern, false, "주 출력에 캔버스(화면 2D) 시험 그림을 그린다 — 사각형 · 자르기 · 그림자 · 글자" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RenderPacketBuilder" );

    RenderPacketBuilder::RenderPacketBuilder()
        : _gpuSceneBuilder{ nullptr }
        , _packetScratch{ nullptr }
        , _gpuUploadQueue{ nullptr }
        , _renderViewScheduler{ nullptr }
        , _renderViewClock{ 0.0 }
        , _listAnimationLODView{}
        , _listShadowLightIndex{}
        , _listScenePreviewBuilder{}
        , _listPreviewLODView{}
        , _bMergeBatchesAcrossMaterials{ false }
        , _bMissingCameraReported{ false }
    {
    }

    RenderPacketBuilder::~RenderPacketBuilder() = default;

    void RenderPacketBuilder::initialize( IRHIDevice& device, TaskManager* pTaskManager )
    {
        if ( _gpuSceneBuilder == nullptr )
            _gpuSceneBuilder = make_unique<GPUSceneBuilder>();
        if ( _packetScratch == nullptr )
            _packetScratch = make_unique<RenderFramePacket>();
        if ( _gpuUploadQueue == nullptr )
            _gpuUploadQueue = make_unique<GPUUploadQueue>();
        if ( _renderViewScheduler == nullptr )
            _renderViewScheduler = make_unique<RenderViewScheduler>();
        // GT 쪽 GPUScene 이 배치를 만든다. 텍스처를 인덱스로 고를 수 있는 백엔드면 머티리얼이 달라도
        // 셰이더 타입 단위로 합친다(언리얼 GPUScene).
        _bMergeBatchesAcrossMaterials = device.supportsNativeBindlessSampling();
        _gpuSceneBuilder->setMergeBatchesAcrossMaterials( _bMergeBatchesAcrossMaterials );
        for ( ScenePreviewBuilder& preview : _listScenePreviewBuilder )
        {
            preview._builder->setMergeBatchesAcrossMaterials( _bMergeBatchesAcrossMaterials );
        }
        _gpuUploadQueue->bindDevice( &device, pTaskManager );
    }

    void RenderPacketBuilder::shutdown()
    {
        _gpuSceneBuilder->clear();
        // 미리보기 빌더도 머티리얼 · 인스턴스를 든다 — 디바이스가 살아 있을 때 놓는다.
        _listScenePreviewBuilder.clear();
    }

    void RenderPacketBuilder::destroy()
    {
        // 만든 순서(initialize)의 역순이다.
        _listScenePreviewBuilder.clear();
        _renderViewScheduler.reset();
        _gpuUploadQueue.reset();
        _packetScratch.reset();
        _gpuSceneBuilder.reset();
    }

    bool RenderPacketBuilder::isInitialized() const
    {
        return _packetScratch != nullptr && _gpuSceneBuilder != nullptr;
    }

    bool RenderPacketBuilder::isMergeBatchesAcrossMaterials() const
    {
        return _gpuSceneBuilder != nullptr && _gpuSceneBuilder->isMergeBatchesAcrossMaterials();
    }

    RenderFramePacket& RenderPacketBuilder::beginPacket()
    {
        RenderFramePacket& packet = *_packetScratch;
        packet.resetForFrame();
        packet._bValid = 1;
        return packet;
    }

    void RenderPacketBuilder::populateScene( RenderFramePacket& packet, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider,
                                             Scene* pActiveScene, float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer )
    {
        const HostViewTarget& mainOutput = views.getMainOutput();
        const bool            bSceneMain = views.isSceneViewMain();
        packet._outputRenderTarget       = mainOutput._renderTarget;
        packet._viewportWidth            = mainOutput._width;
        packet._viewportHeight           = mainOutput._height;

        if ( pActiveScene == nullptr )
            return;

        // 주광은 씬이 갖고, 렌더 스레드는 패킷으로만 받는다. executePacket 은 _pScene 을
        // null 로 두므로 렌더 스레드에서 씬을 조회할 수 없다.
        // 그림자 행렬은 그림자 맵을 가져가는 빛(`findShadowCastingDirectionalLight`)에서 — 라이트 목록의 그림자 플래그와 같은 빛이다.
        DirectionalLightComponent* pLight       = pActiveScene->findActiveDirectionalLight();
        DirectionalLightComponent* pShadowLight = pActiveScene->findShadowCastingDirectionalLight();
        if ( pLight != nullptr )
        {
            const float3 dir          = pLight->getLightDirection();
            const float3 color        = pLight->getColor();
            packet._lightDirIntensity = float4{ dir._x, dir._y, dir._z, pLight->getIntensity() };
            packet._lightColorAmbient = float4{ color._x, color._y, color._z, pLight->getAmbient() };
            packet._bHasLight         = SW_TRUE;
        }

        // 씬의 **모든** 라이트다. 방향광 · 점광 · 스폿이 한 목록으로 간다. 위의 키라이트는 앰비언트의 출처이고, 목록이 비면
        // 그 씬은 앰비언트만 받는다(키라이트로 대신 비추지 않는다). 그림자를 켠 점광 · 스폿은 번호를 따로 받아 아래에서 아틀라스 칸을 준다.
        collectSceneLights( pActiveScene, packet._listLight, &_listShadowLightIndex );

        pActiveScene->ensureDefaultCameras();
        // 핫 리로드 · 씬 전환 · 씬 틱이 GameObject 를 파괴했을 수 있으므로 여기서 조회한다. 씬 뷰 카메라는 씬 뷰가 있을 때만 묻는다.
        CameraComponent* pSceneViewCamera = ( views._scene.isValid() && sceneViewCameraProvider.isBound() ) ? sceneViewCameraProvider() : nullptr;
        if ( pSceneViewCamera != nullptr && pSceneViewCamera->isActive() == false )
            pSceneViewCamera = nullptr;
        CameraComponent* pCam = bSceneMain ? pSceneViewCamera : nullptr;
        if ( pCam == nullptr || pCam->isActive() == false )
            pCam = pActiveScene->getActiveGameCamera();
        // 주 출력의 크기 — 게임 뷰 RT 면 그 크기, 백버퍼 경로면 스왑체인 크기다(화면 사각형 뷰 · 주 시점 사각형의 비율이 이것을 본다).
        const uint32 outputWidth  = packet._viewportWidth > 0 ? packet._viewportWidth : rhi.getDevice().getBackBufferWidth();
        const uint32 outputHeight = packet._viewportHeight > 0 ? packet._viewportHeight : rhi.getDevice().getBackBufferHeight();
        if ( pCam != nullptr )
        {
            // 주 시점의 출력 설정(사각형 · 배율 · 끌 기능)과 컷 표시. 비율은 사각형의 것이다(분할 화면의 한 칸).
            packet._mainView = RenderViewCollector::makeMainSettings( pCam );
            packet.setMainView( pCam->getViewProjectionMatrix( RenderViewCollector::computeAspect( packet._mainView, outputWidth, outputHeight ) ), pCam->getCameraPosition() );
            // 투명 정렬의 깊이 — 직교 카메라는 시선 축(같은 Z 의 스프라이트가 카메라를 따라 앞뒤가 바뀌지 않게), 원근은 거리(render2d.xml).
            _gpuSceneBuilder->setTransparentSortAxis( Render2DSettings::getActive().computeTransparentSortAxis( pCam->isOrthographic(), pCam->getCameraForward() ) );
            _bMissingCameraReported = false;
        }
        else if ( _bMissingCameraReported == false )
        {
            // 주 시점이 없는 패킷은 렌더러가 그리지 않는다(기본 카메라로 대신 그리지 않는다). 씬 데이터의 결함이므로 한 번 알린다.
            SW_LOG_ERROR( "Active scene '%#' has no usable camera - the main view is not drawn", pActiveScene->getName() );
            _bMissingCameraReported = true;
        }
        // 보기 모드는 뷰마다다 — 씬 뷰(호스트 타깃 뷰든 주 출력이든)는 씬 뷰 툴바의 모드, 게임 화면은 주 출력 모드(-gv_viewMode, 기본 Lit).
        const RenderViewMode sceneViewMode = pFrameRenderer != nullptr ? pFrameRenderer->getSceneViewMode() : RenderViewMode::Lit;
        packet._mainView._viewMode         = bSceneMain ? sceneViewMode : ( pFrameRenderer != nullptr ? pFrameRenderer->getViewMode() : RenderViewMode::Lit );
        // 그림자 배치(아틀라스 칸 · 캐스케이드 · 점광 · 스폿 타일)는 카메라가 정해진 **뒤에** 만든다 — `_shadowViewDistance` 를 준 빛은 볼륨을
        // 카메라가 보는 곳에 맞춘다. 텍셀 스냅 · 바이어스는 렌더 스레드가 그릴 캐스케이드 칸과 같은 해상도로 잰다.
        ShadowSetupUtil::populateFrame( pLight != nullptr ? pShadowLight : nullptr, packet._bHasViewProj == SW_TRUE ? &packet._viewProj : nullptr,
                                        FrameRenderer::getShadowMapResolution(), _listShadowLightIndex, packet._listLight, packet._shadow );
        // 추가 뷰(캡처 카메라 · 화면 사각형) — 갱신 주기 · 보이는가 · 예산으로 이번 프레임에 그릴 것을 고른다. 쉬는 뷰도 실린다.
        _renderViewClock += static_cast<float64>( MathUtil::max( 0.0f, deltaTime ) );
        if ( _renderViewScheduler != nullptr && pActiveScene->getObjectManager() != nullptr )
        {
            RenderViewCollector::collectExtraViews( *pActiveScene->getObjectManager(), pCam, packet._viewProj, outputWidth, outputHeight, _renderViewClock,
                                                    RenderViewCollector::getDefaultBudget(), *_renderViewScheduler, packet._listView );
        }
        // 씬 뷰 — 주 출력이면 게임 화면의 일부(화면 사각형 뷰)를 빼고, 게임 뷰와 함께면 자기 RT 에 그리는 추가 뷰로 싣는다.
        if ( bSceneMain )
            RenderViewCollector::removeScreenRectViews( packet._listView );
        else if ( pSceneViewCamera != nullptr && pSceneViewCamera != pCam )
            RenderViewCollector::appendHostView( *pSceneViewCamera, views._scene, sceneViewMode, packet._listView );
        // 추가 뷰마다 자기 카메라에 맞춘 방향광 그림자 — 주 시점 볼륨을 쓰면 다른 곳을 보는 CCTV · 분할 화면은 그림자가 빠진다.
        for ( RenderViewRequest& view : packet._listView )
        {
            if ( view._bRender == SW_TRUE )
                view._shadow = ShadowSetupUtil::makeViewShadowFor( pLight != nullptr ? pShadowLight : nullptr, view._viewProj, packet._shadow );
        }
        // 애니메이션 LOD 의 뷰 — 주 시점과 이번 프레임에 그리는 추가 뷰(CCTV · 분할 화면). 다음 프레임 평가가 이것으로 가시성 · 화면 크기를 본다.
        // 갱신 주기로 쉬는 추가 뷰는 넣지 않는다 — 그 뷰에만 보이는 캐릭터는 그 뷰가 그리는 프레임에만 포즈를 만든다.
        if ( pActiveScene->getObjectManager() != nullptr )
        {
            _listAnimationLODView.clear();
            if ( packet._bHasViewProj == SW_TRUE )
                _listAnimationLODView.push_back( AnimationLODView::make( packet._viewProj, packet._cameraPos ) );
            for ( const RenderViewRequest& view : packet._listView )
            {
                if ( view._bRender == SW_TRUE )
                    _listAnimationLODView.push_back( AnimationLODView::make( view._viewProj, view._position ) );
            }
            pActiveScene->getObjectManager()->getAnimationSystem().setLODViews( _listAnimationLODView );
        }
        _gpuSceneBuilder->populateFromScene( pActiveScene, packet._cameraPos );
        // 추가 뷰(CCTV · PiP)는 자기 눈으로 투명을 정렬한다 — 주 카메라 순서로 그리면 반대편을 보는 뷰에서 앞뒤가 뒤집힌다.
        _gpuSceneBuilder->computeViewTransparentOrders( packet._listView );

        // 그릴 것이 정해졌으니 **스냅샷을 내보내기 전에** GPU 쪽을 만들어 둔다. 렌더 스레드는 그리기만
        // 하면 된다(새 메시가 등장한 프레임에 RT 가 정점 버퍼 생성을 떠안지 않게).
        if ( _gpuUploadQueue != nullptr )
        {
            _gpuSceneBuilder->requestGPUUploads( *_gpuUploadQueue );
            _gpuUploadQueue->flush();
        }

        {
            SW_PROFILE_SCOPE( "GT.Packet.export" );
            _gpuSceneBuilder->exportCPUSnapshot( packet._gpuScene );
        }
    }

    RenderPacketBuilder::ScenePreviewBuilder& RenderPacketBuilder::getOrCreateScenePreviewBuilder( uint32 sceneKey )
    {
        for ( ScenePreviewBuilder& preview : _listScenePreviewBuilder )
        {
            if ( preview._sceneKey == sceneKey )
                return preview;
        }
        ScenePreviewBuilder created;
        created._builder  = make_unique<GPUSceneBuilder>();
        created._sceneKey = sceneKey;
        created._builder->setMergeBatchesAcrossMaterials( _bMergeBatchesAcrossMaterials );
        _listScenePreviewBuilder.push_back( std::move( created ) );
        return _listScenePreviewBuilder.back();
    }

    void RenderPacketBuilder::populateScenePreviews( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager )
    {
        packet._scenePreviewCount = 0;
        for ( ScenePreviewBuilder& preview : _listScenePreviewBuilder )
        {
            preview._bSeen = SW_FALSE;
        }

        const uint32 requestCount = MathUtil::min( views._sceneViewCount, kMaxHostSceneView );
        for ( uint32 requestIndex = 0; requestIndex < requestCount; ++requestIndex )
        {
            const HostSceneView& request = views._arrSceneView[requestIndex];
            Scene* const         pScene  = sceneManager.findPreviewScene( request._sceneKey );
            if ( pScene == nullptr || request._target.isValid() == false )
                continue;

            // 저장소를 다시 쓴다 — 미리보기 패킷의 스냅샷 · 라이트 목록이 프레임마다 새로 할당되지 않게 한다.
            if ( packet._listScenePreview.size() <= packet._scenePreviewCount )
                packet._listScenePreview.push_back( make_unique<RenderFramePacket>() );
            RenderFramePacket& preview = *packet._listScenePreview[packet._scenePreviewCount];
            ++packet._scenePreviewCount;
            preview.resetForFrame();
            preview._bValid             = SW_TRUE;
            preview._sceneKey           = request._sceneKey;
            preview._frameIndex         = packet._frameIndex;
            preview._outputRenderTarget = request._target._renderTarget;
            preview._viewportWidth      = request._target._width;
            preview._viewportHeight     = request._target._height;
            preview._mainView           = request._settings;
            preview.setMainView( request._viewProj, request._cameraPos );

            // 주 패킷과 같은 규칙으로 이 씬의 빛을 싣는다 — 렌더 스레드는 미리보기 씬도 볼 수 없다.
            DirectionalLightComponent* pLight       = pScene->findActiveDirectionalLight();
            DirectionalLightComponent* pShadowLight = pScene->findShadowCastingDirectionalLight();
            if ( pLight != nullptr )
            {
                const float3 dir           = pLight->getLightDirection();
                const float3 color         = pLight->getColor();
                preview._lightDirIntensity = float4{ dir._x, dir._y, dir._z, pLight->getIntensity() };
                preview._lightColorAmbient = float4{ color._x, color._y, color._z, pLight->getAmbient() };
                preview._bHasLight         = SW_TRUE;
            }
            collectSceneLights( pScene, preview._listLight, &_listShadowLightIndex );
            ShadowSetupUtil::populateFrame( pLight != nullptr ? pShadowLight : nullptr, &preview._viewProj, FrameRenderer::getShadowMapResolution(), _listShadowLightIndex,
                                            preview._listLight, preview._shadow );

            // 미리보기 씬의 애니메이션 LOD 는 이 눈 하나로 본다(뷰가 없으면 화면 밖으로 보고 포즈를 거른다).
            if ( pScene->getObjectManager() != nullptr )
            {
                _listPreviewLODView.clear();
                _listPreviewLODView.push_back( AnimationLODView::make( preview._viewProj, preview._cameraPos ) );
                pScene->getObjectManager()->getAnimationSystem().setLODViews( _listPreviewLODView );
            }

            ScenePreviewBuilder& entry = getOrCreateScenePreviewBuilder( request._sceneKey );
            entry._bSeen               = SW_TRUE;
            GPUSceneBuilder& builder   = *entry._builder;
            builder.populateFromScene( pScene, preview._cameraPos );
            if ( _gpuUploadQueue != nullptr )
            {
                builder.requestGPUUploads( *_gpuUploadQueue );
                _gpuUploadQueue->flush();
            }
            builder.exportCPUSnapshot( preview._gpuScene );
        }

        // 요청이 끊긴 씬(창을 닫았거나 가렸다)의 빌더를 내린다 — 머티리얼 · 인스턴스 소유를 놓는다.
        for ( size_t index = _listScenePreviewBuilder.size(); index > 0; --index )
        {
            ScenePreviewBuilder& entry = _listScenePreviewBuilder[index - 1];
            if ( entry._bSeen == SW_TRUE )
                continue;
            entry._builder->clear();
            _listScenePreviewBuilder.erase( _listScenePreviewBuilder.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void RenderPacketBuilder::populateCanvas( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI )
    {
        // 캔버스(화면 2D) — 런타임 UI 가 칠한 목록(내용 번호가 같으면 렌더러가 사각형을 다시 올리지 않는다), 개발 테스트 그림은 그 위에.
        // 글리프 아틀라스의 새 구간은 칠한 것이 없어도 늘 넘긴다(렌더러의 거울이 게임 스레드의 페이지와 어긋나지 않게).
        const bool bUIReady          = pUISystem != nullptr && pUISystem->isInitialized();
        const bool bSubmitMainCanvas = bSceneMain == false && bUIReady && pUISystem->getCanvas().isEmpty() == false;
        if ( bSubmitMainCanvas )
        {
            packet._canvas._mainOutput      = pUISystem->getCanvas();
            packet._canvas._listAnchor      = pUISystem->getCanvasAnchors(); // 화면 마커 위치 — 사각형과 달리 프레임마다 바뀐다
            packet._canvas._contentRevision = pUISystem->getCanvasRevision();
        }
        if ( bUIReady )
            pUISystem->collectWorldCanvases( packet._canvas._listTarget ); // 월드 공간 UI 의 렌더 텍스처
        const bool bSubmitTestPattern = bSceneMain == false && gv_canvasTestPattern && pRHI != nullptr && pRHI->hasDevice();
        if ( bSubmitTestPattern )
        {
            packet._canvas._contentRevision = 0; // 테스트 그림은 내용 번호가 없다 — 늘 올린다
            const uint32 canvasWidth        = packet._viewportWidth > 0 ? packet._viewportWidth : pRHI->getDevice().getBackBufferWidth();
            const uint32 canvasHeight       = packet._viewportHeight > 0 ? packet._viewportHeight : pRHI->getDevice().getBackBufferHeight();
            CanvasTestPattern::paint( packet._canvas._mainOutput, float2{ static_cast<float32>( canvasWidth ), static_cast<float32>( canvasHeight ) }, pFontService,
                                      engine::getSubsystem<FrameProfiler>().getFrameCount() );
        }
        if ( pFontService != nullptr && pFontService->isInitialized() )
            pFontService->getGlyphCache().getAtlas().takeUploads( packet._canvas._listAtlasUpload );
        // 색각 보정 — UI 는 톤맵 뒤라 캔버스 셰이더가 직접 건다(사용자 설정 accessibility.colorVision, 범위 밖은 끔).
        packet._canvas._colorVisionMode = ( 0 <= gv_colorVisionMode && gv_colorVisionMode <= 3 ) ? static_cast<uint32>( gv_colorVisionMode ) : 0u;
    }
} // namespace sw
