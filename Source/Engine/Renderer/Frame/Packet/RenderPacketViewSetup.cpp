#include "pch.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketViewSetup.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHI.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderScaleAdjuster.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Renderer/Frame/View/RenderViewScheduler.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_GLOBAL_VARIABLE( bool, gv_autoRenderScale, false, "자동 렌더 배율 — 프레임 시간이 예산을 깨면 주 시점 렌더 배율을 한 단계씩 내리고 여유가 오래 가면 올린다 (gv_renderScale 에 곱해진다)" );
    SW_GLOBAL_VARIABLE( float32, gv_autoRenderScaleBudgetMs, 16.7f, "자동 렌더 배율의 프레임 시간 예산(ms) — 0 초과" );
    SW_GLOBAL_VARIABLE( float32, gv_autoRenderScaleMin, 0.5f, "자동 렌더 배율이 내려가는 아래 한계 — 0 초과 1 이하" );
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "RenderPacketBuilder" );

    RenderPacketViewSetup::RenderPacketViewSetup()
        : _renderViewScheduler{ nullptr }
        , _renderViewClock{ 0.0 }
        , _renderScaleAdjuster{ nullptr }
        , _bAutoRenderScaleActive{ false }
        , _bMissingCameraReported{ false }
    {
    }

    RenderPacketViewSetup::~RenderPacketViewSetup() = default;

    void RenderPacketViewSetup::initialize()
    {
        if ( _renderViewScheduler == nullptr )
            _renderViewScheduler = make_unique<RenderViewScheduler>();
    }

    void RenderPacketViewSetup::destroy()
    {
        _renderViewScheduler.reset();
    }

    float32 RenderPacketViewSetup::updateAutoRenderScale( float32 deltaTime )
    {
        if ( gv_autoRenderScale == false )
        {
            if ( _bAutoRenderScaleActive )
            {
                _renderScaleAdjuster->reset();
                _bAutoRenderScaleActive = false;
            }
            return 1.0f;
        }
        if ( _bAutoRenderScaleActive == false )
        {
            // 켤 때마다 변수에서 설정을 새로 읽는다. 틀린 값은 기본값으로 대신하지 않고 오류로 알린 뒤 조정을 하지 않는다(배율 1).
            RenderScaleAdjusterConfig config;
            config._budgetMs = gv_autoRenderScaleBudgetMs;
            config._minScale = gv_autoRenderScaleMin;
            if ( _renderScaleAdjuster == nullptr )
                _renderScaleAdjuster = make_unique<RenderScaleAdjuster>();
            const string reason = RenderScaleAdjuster::validate( config );
            if ( reason.empty() == false )
            {
                SW_LOG_ERROR( "gv_autoRenderScale is on but its settings are invalid: %#", reason );
                gv_autoRenderScale = false;
                return 1.0f;
            }
            if ( _renderScaleAdjuster->configure( config ) == false )
                return 1.0f; // validate 가 통과했으니 오지 않는 길
            _bAutoRenderScaleActive = true;
        }
        const float32 before = _renderScaleAdjuster->getScale();
        const float32 scale  = _renderScaleAdjuster->update( deltaTime * 1000.0f );
        if ( scale != before )
            SW_LOG_INFO( "Auto render scale %# -> %#", before, scale );
        return scale;
    }

    RenderPacketMainView RenderPacketViewSetup::setupMainView( RenderFramePacket& packet, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider,
                                                               Scene& activeScene, float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer )
    {
        const bool           bSceneMain = views.isSceneViewMain();
        RenderPacketMainView mainView;
        // 고르지 못하면 아래 게임 카메라 조회가 nullptr 이고, 주 시점 없는 프레임으로 한 번 알린다(`_bMissingCameraReported`).
        activeScene.pickActiveGameCamera();
        // 핫 리로드 · 씬 전환 · 씬 틱이 GameObject 를 파괴했을 수 있으므로 여기서 조회한다. 씬 뷰 카메라는 씬 뷰가 있을 때만 묻는다.
        CameraComponent* pSceneViewCamera = ( views._scene.isValid() && sceneViewCameraProvider.isBound() ) ? sceneViewCameraProvider() : nullptr;
        if ( pSceneViewCamera != nullptr && pSceneViewCamera->isActive() == false )
            pSceneViewCamera = nullptr;
        CameraComponent* pCam = bSceneMain ? pSceneViewCamera : nullptr;
        if ( pCam == nullptr || pCam->isActive() == false )
            pCam = activeScene.getActiveGameCamera();
        // 주 출력의 크기 — 게임 뷰 RT 면 그 크기, 백버퍼 경로면 스왑체인 크기다(화면 사각형 뷰 · 주 시점 사각형의 비율이 이것을 본다).
        const uint32 outputWidth  = packet._viewportWidth > 0 ? packet._viewportWidth : rhi.getDevice().getBackBufferWidth();
        const uint32 outputHeight = packet._viewportHeight > 0 ? packet._viewportHeight : rhi.getDevice().getBackBufferHeight();
        // 화면에 나가는 뷰(주 시점 · 화면 사각형)가 같은 값을 쓴다. 주 시점이 없는 프레임에도 화면 사각형 뷰는 실리므로 카메라와 상관없이 프레임마다 한 번 갱신한다.
        const float32 autoRenderScale = updateAutoRenderScale( deltaTime );
        if ( pCam != nullptr )
        {
            // 주 시점의 출력 설정(사각형 · 배율 · 끌 기능)과 컷 표시. 비율은 사각형의 것이다(분할 화면의 한 칸).
            packet._mainView = RenderViewCollector::makeMainSettings( pCam, autoRenderScale );
            packet.setMainView( pCam->getViewProjectionMatrix( RenderViewCollector::computeAspect( packet._mainView, outputWidth, outputHeight ) ), pCam->getCameraPosition() );
            _bMissingCameraReported = false;
        }
        else if ( _bMissingCameraReported == false )
        {
            // 주 시점이 없는 패킷은 렌더러가 그리지 않는다(기본 카메라로 대신 그리지 않는다). 씬 데이터의 결함이므로 한 번 알린다.
            SW_LOG_ERROR( "Active scene '%#' has no usable camera - the main view is not drawn", activeScene.getName() );
            _bMissingCameraReported = true;
        }
        // 보기 모드는 뷰마다다 — 씬 뷰(호스트 타깃 뷰든 주 출력이든)는 씬 뷰 툴바의 모드, 게임 화면은 주 출력 모드(-gv_viewMode, 기본 Lit).
        const RenderViewMode sceneViewMode = pFrameRenderer != nullptr ? pFrameRenderer->getSceneViewMode() : RenderViewMode::Lit;
        packet._mainView._viewMode         = bSceneMain ? sceneViewMode : ( pFrameRenderer != nullptr ? pFrameRenderer->getViewMode() : RenderViewMode::Lit );

        mainView._pCamera          = pCam;
        mainView._pSceneViewCamera = pSceneViewCamera;
        mainView._outputWidth      = outputWidth;
        mainView._outputHeight     = outputHeight;
        mainView._autoRenderScale  = autoRenderScale;
        mainView._sceneViewMode    = sceneViewMode;
        return mainView;
    }

    void RenderPacketViewSetup::collectExtraViews( RenderFramePacket& packet, const HostViewTargets& views, Scene& activeScene, const RenderPacketMainView& mainView, float32 deltaTime,
                                                   const FrameRenderer* pFrameRenderer )
    {
        // 추가 뷰(캡처 카메라 · 화면 사각형) — 갱신 주기 · 보이는가 · 예산으로 이번 프레임에 그릴 것을 고른다. 쉬는 뷰도 실린다.
        _renderViewClock += static_cast<float64>( MathUtil::max( 0.0f, deltaTime ) );
        if ( _renderViewScheduler != nullptr && activeScene.getObjectManager() != nullptr )
        {
            // 주 시점 가림 타일 사본 — 렌더러가 몇 프레임 늦게 되읽은 것이다(가림 컬링이 꺼졌으면 nullptr).
            const shared_ptr<const OcclusionTileSnapshot> pOcclusion = pFrameRenderer != nullptr ? pFrameRenderer->getOcclusionSnapshot() : nullptr;
            RenderViewCollector::collectExtraViews( *activeScene.getObjectManager(), mainView._pCamera, packet._viewProj, packet._cameraPos, mainView._outputWidth, mainView._outputHeight,
                                                    mainView._autoRenderScale, _renderViewClock, RenderViewCollector::getDefaultBudget(), *_renderViewScheduler, pOcclusion.get(),
                                                    packet._listView );
        }
        // 씬 뷰 — 주 출력이면 게임 화면의 일부(화면 사각형 뷰)를 빼고, 게임 뷰와 함께면 자기 RT 에 그리는 추가 뷰로 싣는다.
        if ( views.isSceneViewMain() )
            RenderViewCollector::removeScreenRectViews( packet._listView );
        else if ( mainView._pSceneViewCamera != nullptr && mainView._pSceneViewCamera != mainView._pCamera )
            RenderViewCollector::appendHostView( *mainView._pSceneViewCamera, views._scene, mainView._sceneViewMode, packet._listView );
    }
} // namespace sw
