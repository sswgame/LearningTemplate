#include "pch.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Environment/Fog/FogComponent.h"
#include "Engine/Environment/Sky/SkyComponent.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFramePacket.h"
#include "Engine/Renderer/Frame/View/RenderViewCollector.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAssetCache.h"
#include "Engine/Renderer/Vfx/VfxGPUSimulation.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_deferred=1` 이면 기본 파이프라인을 디퍼드로 고릅니다(기본은 포워드).
     * @details `initialize` 에 파이프라인 경로를 주지 않을 때 디퍼드를 고르는 방법이다.
     *          디퍼드는 레벨이 나뉘어(레벨 0은 Shadow 와 GBuffer) 병렬 기록이 실제로 도는 유일한 경로이기도 하다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_deferred, false, "기본 파이프라인을 디퍼드로 (기본 포워드)" );
    /**
     * @brief `-gv_renderPipeline=<파이프라인 XML>` 이면 그 파이프라인으로 그립니다(디퍼드 선택 · 팩 설정 · 기본보다 우선합니다).
     * @details 게임은 데이터로 고른다 — 팩 설정의 `PackEngineSettings::_renderPipeline`(2D 게임은 그림자 맵과 톤맵이 없는
     *          `engine/pipeline/forward2dpipeline.xml`. 톤맵은 흰색을 0.5 로 누르기 때문이다). 이 스위치는 측정 · 검증용으로 그것을 덮는다.
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_renderPipeline, "", "이 파이프라인 XML 로 그린다 (비우면 gv_deferred · 팩 설정 · 기본 포워드)" );

    /**
     * @brief `-gv_drawMerge=0` 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
     * @details 묶은 이미지와 묶지 않은 이미지가 같아야 한다. 다르면 배치 테이블(g_SwBatches)이나 드로우 ID 가 틀린 것이다. 기본은 묶는 것이다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_drawMerge, 1, "씬 배치 멀티 드로우 묶기 (0=배치마다 호출, 진단용)" );

    /**
     * @brief `-gv_vertexPool=0` 이면 씬 메시 정점을 한 풀 버퍼에 모으지 않고 메시마다 자기 정점 버퍼로 그립니다.
     * @details 풀을 켠 이미지와 끈 이미지는 같아야 한다. 다르면 간접 인자의 startVertex 나 SV_VertexID 의 API 차이를 잘못 다룬 것이다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_vertexPool, 1, "씬 메시 정점 풀 (0=메시마다 정점 버퍼, 진단용)" );

#if !defined( SW_SHIPPING )
    /**
     * @brief 라이트 컬링 방식입니다. 0 이면 라이트 그리드 없이 픽셀마다 목록 전체를 돕니다(비교 기준), 1 이면 CPU 가 만든 클러스터 그리드, 2 이면 GPU 컴퓨트가 만든 그리드입니다.
     * @details 학습용으로 세 방식을 나란히 보게 둔다(결정 기록 5-12 D2-13). 기본값은 Shipping 과 같은 GPU 다. Shipping 은 변수를 두지 않고 GPU 로 고정한다
     *          (`FrameRenderer::selectLightGridProducer`).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_lightCulling, 2, "라이트 컬링 (0=그리드 없이 목록 전체, 1=CPU 클러스터 그리드, 2=GPU 클러스터 그리드)" );
#endif

    /**
     * @brief `-gv_viewMode=<n>` 은 주 출력(게임 창)의 보기 모드입니다(0 Lit, 1 Unlit, 2 Wireframe, 3 Normals, 4 Depth, 5 Overdraw, 6 LightingOnly, 7 Roughness, 8 Metallic, 9 Mipmaps, 10 LightComplexity, 11 ShadedWireframe).
     * @details 에디터 씬 뷰 툴바와 같은 값을 가리킨다(`RenderViewMode`). 여기 있는 이유는 **검증**이다.
     *          뷰 모드가 정말로 픽셀을 바꾸는지 `-gv_screenshot` 으로 확인하려면 에디터를 띄우지 않고
     *          모드를 고를 수 있어야 한다. 없으면 이 기능은 사람 눈으로만 확인되는 기능이 된다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_viewMode, 0, "주 출력 보기 모드 (0 Lit / 1 Unlit / 2 Wireframe / 3 Normals / 4 Depth / 5 Overdraw / 6 LightingOnly / 7 Roughness / 8 Metallic / 9 Mipmaps / 10 LightComplexity / 11 ShadedWireframe)" );

    FrameRenderer::FrameRenderer()
        : _pDevice{ nullptr }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{ nullptr }
        , _pCmd{ nullptr }
        , _pTaskManager{ nullptr }
        , _gpuScene{}
        , _sceneBuilder{}
        , _pipelineResource{}
        , _graph{}
        , _pipelinePath{}
        , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
        , _mainView{}
        , _listExtraView{}
        , _pActiveView{ &_mainView }
        , _frameContext{}
        , _mainSeedScratch{}
        , _directViewScheduler{}
        , _listDirectViewScratch{}
        , _passCbRing{}
        , _arrView{}
        , _deform{}
#if !defined( SW_SHIPPING )
        , _debugOverrides{}
#endif
        , _indirectDrawCallCount{ 0 }
        , _gpuPassTimeline{}
        , _lastIndirectDrawCallCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _listMaterialFallback{}
        , _mapLayoutMaterialFallback{}
        , _transparentRankPlaceholder{}
        , _occlusionTilesPlaceholder{}
        , _arrOcclusionReadbackMeta{}
        , _occlusionReadbackSerial{ 0 }
        , _psoCache{}
        , _pPSOCache{ &_psoCache }
        , _outputRenderTarget{ 0 }
        , _capture{ _pDevice, _mainView._transientPool, _pipelineResource }
        , _canvasRenderer{}
        , _particleRenderer{}
        , _particlePSOKey{ 0 }
        , _canvasFrame{}
        , _pLastSwapchainWriter{ nullptr }
        , _bUpscalePostChainLogged{ SW_FALSE }
        , _pMainOutputCanvasView{ nullptr }
        , _listScenePreviewRenderer{}
        , _statusMessage{}
        , _graphContext{}
        , _frameLight{}
        , _frameSky{}
        , _frameFog{}
        , _outputWidth{ 0 }
        , _outputHeight{ 0 }
        , _lastRenderedExtraViewCount{ 0 }
        , _directOutputWidth{ 0 }
        , _directOutputHeight{ 0 }
        , _scenePreviewRendererCount{ 0 }
        , _bGPUCullingActive{ SW_FALSE }
        , _status{ FrameRendererStatus::Uninitialized }
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
        , _bScenePreview{ SW_FALSE }
        , _bHasFrameSky{ SW_FALSE }
        , _bLightGridPSOMissingLogged{ SW_FALSE }
        , _bHasFrameFog{ SW_FALSE }
        , _bFogVolumeActive{ SW_FALSE }
        , _viewMode{ static_cast<uint8>( RenderViewMode::Lit ) }
        , _sceneViewMode{ static_cast<uint8>( RenderViewMode::Lit ) }
        , _bOutputPSOMissingLogged{ 0 }
        , _bMaterialFallbackMissingLogged{ 0 }
        , _bMissingColorTargetLogged{ 0 }
        , _bHasExecutedDepthPrepass{ 0 }
    {
    }

    FrameRenderer::~FrameRenderer()
    {
        shutdown();
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, string_view pipelineXMLPath )
    {
        return initialize( pDevice, nullptr, pipelineXMLPath );
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                                    string_view pipelineXMLPath )
    {
        _pDevice = pDevice;
        // 인스턴스 애니메이션 시계는 여기서 한 번 돌린다(GameTimer 는 만들면 멈춘 상태다).
        _deform._animationTimer.resetTimer();
        _deform._animationTimer.startTimer();
        if ( pDevice == nullptr )
        {
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "null IRHIDevice";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        if ( pTaskManager != nullptr )
            bindServices( pTaskManager );
        else if ( engine::areEngineServicesBound() )
            bindServices( &engine::getSubsystem<TaskManager>() );
        // 패스별 GPU 시간을 보고할 프로파일러다. 서비스가 없는 실행(서비스 없이 렌더러만 쓰는 도구)은 계측하지 않는다.
        // 미리보기 씬 렌더러는 계측하지 않는다(타임스탬프 슬롯 · `GPU.<패스>` 구간이 주 렌더러의 것이다).
        const bool bProfile = engine::areEngineServicesBound() && _bScenePreview == SW_FALSE;
        _gpuPassTimeline.bindProfiler( bProfile ? &engine::getSubsystem<FrameProfiler>() : nullptr );

        _renderPipelineAssetCache = make_unique<RenderPipelineAssetCache>();
        if ( _renderPipelineAssetCache->initialize() == false )
        {
            _renderPipelineAssetCache.reset();
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "RenderPipelineAssetCache initialize failed";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        // 2D 정렬 레이어 표는 엔진 필수 데이터다. 서브시스템 Render2DSettings 가 기동 때 읽고(못 읽으면 기동이 멈춘다) 렌더러 서브시스템이 그 뒤에 선다 -
        // 주 렌더러와 미리보기 렌더러는 서비스 표의 같은 표를 읽는다.

        // 커맨드라인이 주 출력의 보기 모드를 정했으면 여기서 받는다(에디터 씬 뷰는 툴바가 따로 정한다).
        // 범위 밖 번호는 Lit 으로 바꾸지 않고 초기화를 멈춘다 — 검증 실행이 다른 모드를 찍고 통과하면 안 된다.
        if ( isRenderViewModeIndex( gv_viewMode ) == false )
        {
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "-gv_viewMode=" + to_string( static_cast<int64>( gv_viewMode ) ) + " is not a view mode (0 .. " +
                             to_string( static_cast<int64>( RenderViewMode::Count ) - 1 ) + ")";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }
        if ( gv_viewMode > 0 )
            setViewMode( static_cast<RenderViewMode>( gv_viewMode ) );

        // 동기 경로(execute)는 렌더러 자신의 빌더가 배치를 만든다. 패킷 경로의 빌더(Engine)에는 Engine 가 같은 값을 준다.
        _sceneBuilder.setMergeBatchesAcrossMaterials( pDevice->supportsNativeBindlessSampling() );

        const EngineDefaultAssets& engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        RenderPipelineAssetCache&  rpm                 = *_renderPipelineAssetCache;
        if ( rpm.findRenderPass( hashed_string( FrameRendererUtil::kDefaultMainPassName ) ) == nullptr )
            rpm.loadRenderPass( engineDefaultAssets._defaultRenderPass.get() );

        // 인자, `-gv_renderPipeline`, `-gv_deferred`, 팩 설정(`PackEngineSettings::_renderPipeline`), EngineDefaultAssets 의 포워드 순서로 고른다.
        // 디퍼드 경로(와 그 위의 조명)를 측정하거나 검증하려면 `-gv_deferred=1` 을 준다.
        const DataPath&                   packPipeline = engine::getSubsystem<PackEngineSettings>()._renderPipeline;
        FrameRendererUtil::PipelineChoice choice{};
        choice._argumentPath               = pipelineXMLPath;
        choice._commandLinePath            = gv_renderPipeline;
        choice._deferredPath               = engineDefaultAssets._defaultDeferredPipeline.get();
        choice._gamePath                   = packPipeline.isSet() ? string_view( packPipeline.get() ) : string_view{};
        choice._forwardPath                = engineDefaultAssets._defaultForwardPipeline.get();
        choice._bDeferred                  = gv_deferred;
        const string_view resolvedPipeline = FrameRendererUtil::computePipelinePath( choice );

        if ( loadPipeline( resolvedPipeline ) == false )
        {
            _status = FrameRendererStatus::Failed;
            if ( _statusMessage.empty() )
                _statusMessage = string( "pipeline load failed: " ) + string( resolvedPipeline );
            SW_LOG_ERROR( "Not ready — %#", _statusMessage );
            return false;
        }

        _status = FrameRendererStatus::Ready;
        _statusMessage.clear();
        SW_LOG_INFO( "Ready with pipeline '%#'", _pipelinePath );
        return true;
    }

    void FrameRenderer::bindServices( TaskManager* pTaskManager )
    {
        _pTaskManager = pTaskManager;
    }

    void FrameRenderer::shutdown()
    {
        const bool bAlreadyShutDown = _status == FrameRendererStatus::Uninitialized && _pDevice == nullptr && _pipelinePath.empty();
        if ( bAlreadyShutDown )
            return;

        // 스냅샷이 가진 머티리얼과 인스턴스의 소유권을 **디바이스가 살아 있을 때** 해제한다. 스냅샷은 소유권을 함께
        // 싣고 있으므로(GPUScene.h) 여기서 비우지 않으면 렌더러가 소멸할 때까지 그것들이 살아 있어, 디바이스가 먼저
        // 사라진 뒤 소멸자가 사라진 디바이스에 GPU 자원을 반환하려 한다(해제 후 사용).
        // 미리보기 씬 렌더러도 같은 디바이스의 자원을 든다 — 디바이스가 살아 있을 때 먼저 내린다. 그 렌더러들은 이 렌더러의 PSO 캐시를 빌려 쓰므로
        // 캐시를 놓는 `releasePassResources` 보다 먼저여야 한다.
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            preview._renderer->shutdown();
        }
        _listScenePreviewRenderer.clear();
        _scenePreviewRendererCount.store( 0 );
        _graph.releaseCommandLists();
        if ( _pDevice != nullptr )
            _gpuScene.releaseGPU( _pDevice );
        _gpuScene.clear();
        _sceneBuilder.clear();

        // 추가 뷰는 통째로 해제한다(빌린 렌더 텍스처, 컬링 상수 버퍼, 리스트). 주 시점의 리스트도 이 디바이스의 것이다.
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            releaseExtraView( *pView );
        }
        _listExtraView.clear();
        _mainView._commandList.reset();
        _directViewScheduler.clear();
        releaseTransientResources();
        releasePassResources();
        _graph.clear();
        // 리소스 상태 추적은 이름에서 상태를 찾는 구조라 디바이스가 바뀌어도 살아남는다. 새 디바이스의 텍스처는 다시 Undefined 다.
        // 지우지 않으면 첫 프레임이 "이미 렌더 타깃 상태" 로 믿고 배리어를 건너뛴다.
        _graphContext.reset();
        _frameCmd.reset();
        // 패스와 파이프라인 에셋은 위의 패스 자원과 그래프를 해제한 **뒤에** 비운다. 그쪽이 이 캐시의 포인터를 가지고 있다.
        if ( _renderPipelineAssetCache != nullptr )
        {
            _renderPipelineAssetCache->shutdown();
            _renderPipelineAssetCache.reset();
        }
        _pCmdOwnerDevice    = nullptr;
        _pCmd               = nullptr;
        _frameContext._pCmd = nullptr;
        _pDevice            = nullptr;
        // 다음 디바이스(백엔드 교체)는 새 GPU 컨텍스트를 연다. 같은 주소에 새 디바이스가 생겨도 옛 시계 기준을 쓰지 않도록 버린다.
        _gpuPassTimeline.forgetDevice();
        _pTaskManager = nullptr;
        _status       = FrameRendererStatus::Uninitialized;
        _statusMessage.clear();
        _bCallbacksBound = SW_FALSE;
        _pipelinePath.clear();
        SW_LOG_INFO( "Shut down." );
    }

    bool FrameRenderer::loadPipeline( string_view pipelineXMLPath )
    {
        _pipelinePath    = pipelineXMLPath;
        _bCallbacksBound = SW_FALSE;
        _graph.clear();
        releaseTransientResources();

        if ( _pipelineResource.loadFromXMLFile( pipelineXMLPath ) == false )
        {
            _statusMessage = string( "failed to load pipeline XML: " ) + string( pipelineXMLPath );
            return false;
        }

        if ( _renderPipelineAssetCache != nullptr )
        {
            RenderPipelineAssetCache& rpm = *_renderPipelineAssetCache;
            rpm.loadPipeline( pipelineXMLPath );
            for ( const string& passRef : _pipelineResource.getDesc()._listRenderPassRef )
            {
                if ( passRef.empty() == false )
                    rpm.loadRenderPass( passRef );
            }
        }

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        if ( listPass.empty() )
        {
            _statusMessage = string( "no graph passes in pipeline: " ) + string( pipelineXMLPath );
            SW_LOG_ERROR( "%#", _statusMessage );
            return false;
        }

#if SW_PROFILE_COMPILED
        // 타임스탬프 슬롯은 앞쪽 패스에만 있다. 넘는 패스는 GPU 시간(과 GPU.Frame 의 끝)이 빠지므로 로드할 때 알린다.
        const uint32 untimedPassCount = FrameRendererUtil::countUntimedGPUPass( listPass.size() );
        if ( untimedPassCount > 0 )
        {
            SW_LOG_WARNING( "Pipeline '%#' has %# passes; GPU timestamps cover the first %#, so %# pass(es) from '%#' on are not timed "
                            "(constant::kMaxGPUTimestampSlot)",
                            _pipelineResource.getDesc()._name, listPass.size(), FrameRendererUtil::kGPUTimedPassCapacity, untimedPassCount,
                            listPass[FrameRendererUtil::kGPUTimedPassCapacity]._name );
        }
#endif

        float4 sceneColorClear;
        if ( tryGetAttachmentClearColor( FrameRendererUtil::Attachment::kSceneColor, sceneColorClear ) )
            _clearColor = sceneColorClear;

        // 파이프라인 패스 설정(셰이더, 진입점, 블렌드, 퍼뮤테이션)으로 PSO 를 다시 만든다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        bindPassCallbacks();

        SW_LOG_INFO( "Built graph '%#' (%# passes, callbacks bound once)",
                     _pipelineResource.getDesc()._name, listPass.size() );
        return true;
    }

    // ---------------------------------------------------------------------------
    // 공통 헬퍼: commandList 준비
    // ---------------------------------------------------------------------------

    bool FrameRenderer::prepareCommandList( IRHIDevice* pDevice, [[maybe_unused]] const utf8* pCallerName )
    {
        if ( _frameCmd && _pCmdOwnerDevice != pDevice )
        {
            // 리스트는 만든 디바이스의 것이다. 뷰마다의 리스트도 함께 버린다.
            _frameCmd.reset();
            _mainView._commandList.reset();
            for ( unique_ptr<ViewTarget>& pView : _listExtraView )
            {
                pView->_commandList.reset();
            }
        }

        if ( _frameCmd == nullptr )
            _frameCmd = pDevice->createCommandList();

        _pCmdOwnerDevice = pDevice;
        _pCmd            = _frameCmd.get();
        // 패스 컨텍스트 시드도 같은 리스트를 가리키게 한다(직렬 경로가 이것을 쓴다).
        _frameContext._pCmd = _pCmd;

        if ( _pCmd == nullptr )
        {
            SW_LOG_ERROR( "%#: createCommandList returned null", pCallerName );
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------------------------
    // 공통 헬퍼: graph 실행과 commandList 제출
    // ---------------------------------------------------------------------------

    bool FrameRenderer::isDrawMergeEnabled() const
    {
#if !defined( SW_SHIPPING )
        if ( _debugOverrides._drawMerge >= 0 )
            return _debugOverrides._drawMerge != 0;
#endif
        return gv_drawMerge != 0;
    }

    bool FrameRenderer::submitGraph( IRHIDevice* pDevice )
    {
        // 패스 컨텍스트 슬롯은 병렬 기록 **전에** 맞춘다. 기록 중에 늘리면 워커끼리 경합한다.
        const size_t passCount = _pipelineResource.getGraphPass().size();
        if ( _listPassContext.size() < passCount + 1 )
            _listPassContext.resize( passCount + 1 );

        _pCmd->beginCommandList();
        _bGPUCullingActive = SW_FALSE;
        _indirectDrawCallCount.store( 0, std::memory_order_relaxed );
        _deform._animationTimer.updateTimer();

#if SW_PROFILE_COMPILED
        // 프레임의 첫 GPU 명령 위치다. 패스 시간 합과 이 값의 차이가 "패스 밖의 GPU 시간"(컴퓨트
        // 프리패스, 업로드, 배리어)이다. 게이트는 패스 타임스탬프와 같다(FrameRendererPassExecute).
        const IRHITimestampProvider* pTimestamps   = pDevice->findTimestampProvider();
        const bool                   bWriteGPUTime = pTimestamps != nullptr && _gpuPassTimeline.isRecording() &&
                                   FrameRendererUtil::kGPUTimestampSlotFrameBegin < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
        {
            _pCmd->writeTimestamp( FrameRendererUtil::kGPUTimestampSlotFrameBegin );
            _pCmd->writeTimestamp( FrameRendererUtil::kGPUTimestampSlotComputeBegin );
        }
#endif

        // GPU 드리븐 프리패스다. **애니메이션이 먼저고 컬링이 나중이다.** 순서가 뒤집히면 컬링이
        // 이번 프레임에 회전하기 전의 바운드로 판정한다. 컬링은 이번 프레임에 그리는 **모든 뷰**의 슬롯을 돈다.
        // 깊이 히스토리(뷰마다 지난 장면 깊이의 사본)가 먼저다 — GPU 입자의 깊이 충돌이 읽는다. 이번 프레임 패스가 장면 깊이를 쓰기 전이라 지난 깊이가 남아 있다.
        recordDepthHistories();
        // GPU 방출기의 입자 시뮬레이션은 인스턴스 애니메이션 · 컬링 **앞**이다 — 메시 입자가 인스턴스 칸에 행렬 · 바운드를 쓰고 컬링이 그 바운드로 거른다.
        // 빌보드 · 띠는 투명 패스가 렌더 입력과 간접 인자를 읽는다(VfxGPUSimulation, vfxsimulate.hlsl · vfxmeshinstance.hlsl).
        _particleRenderer.recordSimulation( *_pCmd, getEnginePSO( RenderPassType::VfxSimulation ), getEnginePSO( RenderPassType::VfxMeshInstance ) );
        const uint32 animationInstanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
        dispatchInstanceAnimation( animationInstanceCount );
        dispatchMeshMorph();
        dispatchMeshSkin();
        dispatchCullAndSort( animationInstanceCount );
        // 라이트 그리드(GPU 생산자) — 조명을 계산하는 패스(포워드 · 디퍼드 · 투명)보다 먼저 끝나야 한다. 이 리스트는 그 패스들의 리스트보다 먼저 큐에 낸다.
        dispatchLightGrid();
        // 볼륨 안개 — 라이트 그리드를 읽으므로 그 디스패치 뒤다. 안개 패스(그래프)가 결과를 읽는다.
        dispatchVolumetricFog();
        // 실시간 프로브 갱신 - 라이트 업로드 뒤, 조명 패스(그래프) 앞이다. 프로브 버퍼를 쓴다.
        dispatchRealtimeProbes();
        // 월드 공간 UI 의 렌더 텍스처다. 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트(같은 큐)로 그린다.
        _canvasRenderer.drawTargets( *_pCmd, *pDevice, _canvasFrame, *_pPSOCache, getEnginePSO( RenderPassType::Canvas ) );
        // 2D 빛 쿠키 아틀라스 — 새로 칸을 받은 쿠키만 그 칸에 그린다. 빛 받는 스프라이트 패스가 읽기 전에 같은 프리패스 리스트에서 끝난다.
        drawLight2DCookieAtlas();

#if SW_PROFILE_COMPILED
        if ( bWriteGPUTime )
            _pCmd->writeTimestamp( FrameRendererUtil::kGPUTimestampSlotComputeEnd );
#endif

        // 프리패스 리스트를 먼저 닫아 큐에 낸다. 뷰마다의 리스트와 병렬 패스 리스트보다 인다이렉트 인자 준비가 GPU 타임라인에서 먼저 끝나게 하려는 것이다
        // (같은 큐의 ExecuteCommandLists 호출 순서가 곧 실행 순서다).
        _pCmd->endCommandList();
        pDevice->executeCommandList( _pCmd );
        _pCmd = nullptr;

        // 주 시점이 읽는 렌더 텍스처(CCTV 모니터)를 먼저 그린다. 주 시점의 패스 시드는 추가 뷰가 덮어쓰므로 보존해 둔다.
        _mainSeedScratch          = _frameContext;
        uint32 renderedExtraCount = renderExtraViewsOfKind( pDevice, RenderViewOutputKind::RenderTexture );
        // 주 시점의 시드와 클리어 기록으로 돌아온다(추가 뷰가 자기 값을 덮어썼다).
        _frameContext = _mainSeedScratch;
        _bHasExecutedDepthPrepass.store( 0 );

        // 주 출력의 캔버스는 주 출력에 마지막으로 그리는 뷰가 그린다. 화면 사각형 뷰는 주 시점 뒤에 겹치므로, 주 시점이 그리면 그 뷰가 UI 를 덮는다.
        _pMainOutputCanvasView = &_mainView;
        for ( const unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE && pView->_outputKind == RenderViewOutputKind::ScreenRect )
                _pMainOutputCanvasView = pView.get();
        }

        const bool bOk = executeMainGraph( pDevice );

        // 화면 사각형 뷰(분할 화면, PiP)는 주 시점 위에 겹친다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::ScreenRect );
        // 호스트 타깃 뷰(에디터 씬 뷰)는 자기 RT 에 그린다. 이번 프레임에 그린 렌더 텍스처(CCTV)를 읽을 수 있도록 마지막에 그린다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::HostTarget );
        _frameContext               = _mainSeedScratch;
        _lastRenderedExtraViewCount = renderedExtraCount;
        // 다음 프레임의 깊이 히스토리가 이번 프레임의 장면 깊이와 짝이 되도록 뷰-투영을 적는다.
        rememberDepthHistoryViews();
        return bOk;
    }

    uint32 FrameRenderer::renderExtraViewsOfKind( IRHIDevice* pDevice, RenderViewOutputKind outputKind )
    {
        uint32 renderedCount = 0;
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_outputKind != outputKind )
                continue;
            renderExtraView( pDevice, *pView );
            ++renderedCount;
        }
        return renderedCount;
    }

    bool FrameRenderer::executeMainGraph( IRHIDevice* pDevice )
    {
        // 병렬 기록이 가능하면(백엔드 capability, TaskManager, 레벨이 나올 만큼 컴파일된 그래프) 레벨마다 여러 리스트로 기록한다.
        // 첫 프레임처럼 그래프가 아직 컴파일되지 않았으면(getExecutionOrder() 가 비어 있으면) 직렬 경로로 간다.
        const bool bCanRunParallel = _pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != SW_FALSE &&
                                     _graph.getExecutionOrder().size() > 1;
        if ( bCanRunParallel )
            return _graph.executeParallel( _graphContext, _pTaskManager, pDevice );

        // 직렬 경로도 그래프가 추론한 배리어를 쓴다. 패스가 기록하는 리스트와 **같은 것**을 넘긴다.
        if ( _mainView._commandList == nullptr )
            _mainView._commandList = pDevice->createCommandList();
        IRHICommandList* pMainCmd = _mainView._commandList.get();
        if ( pMainCmd == nullptr )
            return false;
        // 직렬 경로의 패스는 시드의 리스트에 기록한다(그래프가 패스에 리스트를 주지 않는다). 그래서 시드를 이 리스트로 돌린다.
        _frameContext._pCmd = pMainCmd;
        pMainCmd->beginCommandList();
        const bool bOk = _graph.execute( _graphContext, pMainCmd );
        pMainCmd->endCommandList();
        pDevice->executeCommandList( pMainCmd );
        return bOk;
    }

    // ---------------------------------------------------------------------------

    bool FrameRenderer::execute( IRHIDevice* pDevice, Scene& scene )
    {
        if ( isReady() == false || pDevice == nullptr )
            return false;

        // 주 시점은 씬의 게임 카메라다. 없으면 기본 카메라로 대신 그리지 않고 멈춘다 — 카메라가 없다는 씬 데이터의 결함이 화면으로 가려진다.
        scene.ensureDefaultCameras();
        CameraComponent* pMainCamera = scene.getActiveGameCamera();
        if ( pMainCamera == nullptr )
        {
            SW_LOG_ERROR( "Scene '%#' has no usable camera - nothing is drawn", scene.getName() );
            return false;
        }
        Scene* const pScene = &scene;

        _pDevice            = pDevice;
        _outputRenderTarget = 0;
        // 주 시점의 출력 설정(사각형, 배율, 끌 기능, 컷)은 패킷 경로와 같은 규칙으로 카메라에서 읽는다.
        _mainView._settings           = RenderViewCollector::makeMainSettings( pMainCamera, RenderViewCollector::kDirectPathAutoScale ); // 자동 렌더 배율은 패킷 경로(RenderPacketBuilder)만 쓴다: 직접 경로(테스트 · 초상화 베이크)는 프레임 시계가 없고 출력이 결정적이어야 한다
        _mainView._settings._viewMode = getViewMode();                                                                                   // 직접 경로(테스트)는 주 출력 모드를 쓴다. 패킷 경로는 Engine 가 싣는다.
        // 씬 직접 경로(에디터, 테스트)도 패킷 경로와 **같은 라이트 버퍼**를 쓴다. 경로마다 조명이
        // 다르면 에디터에서 본 이미지와 게임 화면이 달라진다.
        collectSceneLights( pScene, _listScratchLight, &_listScratchShadowLightIndex, &_listScratchLightProbe, &_listScratchLightCookie, &_listScratchRealtimeProbe );
        // 주 광원(그림자 배치, 앰비언트)도 패킷 경로(Engine)와 **같은 규칙**으로
        // 씬에서 읽는다. 주의: 이 경로가 주 광원을 채우지 않으면 테스트가 씬에 둔 방향광이 이미지에 반영되지 않아,
        // 조명이 필요한 픽셀 검증(블룸이 1을 넘는 위치)이 불가능해진다.
        DirectionalLightComponent* pKeyLight    = scene.findActiveDirectionalLight();
        DirectionalLightComponent* pShadowLight = scene.findShadowCastingDirectionalLight();
        if ( pKeyLight != nullptr )
        {
            const float3 lightDir     = pKeyLight->getLightDirection();
            const float3 lightColor   = pKeyLight->getColor();
            _frameLight._dirIntensity = float4{ lightDir._x, lightDir._y, lightDir._z, pKeyLight->getIntensity() };
            _frameLight._colorAmbient = float4{ lightColor._x, lightColor._y, lightColor._z, pKeyLight->getAmbient() };
        }
        else
        {
            _frameLight = FrameLightState{};
        }
        // 하늘도 패킷 경로(RenderPacketBuilder)와 같은 규칙으로 씬에서 읽는다.
        SkyGradient sceneSky;
        const bool  bSceneSky = scene.getObjectManager() != nullptr && SkyComponent::findSky( *scene.getObjectManager(), sceneSky );
        applyFrameSky( bSceneSky, sceneSky );
        // 안개도 같다 — 물속이면 물의 안개가 컴포넌트의 값을 덮는다(FogComponent::findFog).
        FogSettings sceneFog;
        const bool  bSceneFog = scene.getObjectManager() != nullptr && FogComponent::findFog( *scene.getObjectManager(), pMainCamera->getCameraPosition(), sceneFog );
        applyFrameFog( bSceneFog, sceneFog );
        // 출력 크기(인자가 없으면 스왑체인)를 먼저 정한다 — 주 시점의 비율이 그것을 본다.
        ensurePassResources();
        ensureTransientResources( _directOutputWidth, _directOutputHeight );
        // 주 시점 뷰-투영은 하나다. 그림자 맞춤과 그리기가 같은 행렬을 쓰고, 비율은 패킷 경로(`RenderPacketBuilder`)와 같은 규칙(출력 × 사각형)이다.
        // 둘을 따로 측정하면 출력 설정의 비율과 풀의 비율이 어긋나 그림자 볼륨이 그리는 절두체를 덮지 못한다.
        const float4x4 mainCameraViewProj = pMainCamera->getViewProjectionMatrix( RenderViewCollector::computeAspect( _mainView._settings, _outputWidth, _outputHeight ) );
        // 그림자 배치(볼륨 맞춤 · 아틀라스 칸 · 점광 · 스폿 타일)는 패킷 경로(Engine)와 같은 함수로 주 시점 카메라의 뷰-투영에 맞춘다.
        ShadowSetupUtil::populateFrame( pKeyLight != nullptr ? pShadowLight : nullptr, &mainCameraViewProj, getShadowMapResolution(), _listScratchShadowLightIndex, _listScratchLight,
                                        _frameShadow );
        applyFrameShadow();
        // 쿠키를 쓰는 2D 빛에 아틀라스 칸을 나눠 원소에 적는다(업로드 전). 칸에 그리는 것은 기록 중 프리패스(drawLight2DCookieAtlas)다.
        _cookieAtlas.assignCells( pDevice, _listScratchLightCookie, _listScratchLight );
        _listFrameRealtimeProbe = _listScratchRealtimeProbe;
        _lightBuffer.populate( _listScratchLight, _frameShadow._listTileElement, _listScratchLightProbe, _listFrameRealtimeProbe.empty() == false );
        resetPassCbRing();
        setIdentityWorld( _frameContext );
        updatePassConstants( _frameContext, mainCameraViewProj );
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        const float3 cameraPos                 = pMainCamera->getCameraPosition();
        view( RenderViewType::Main )._position = cameraPos; // 정렬 키(카메라까지의 거리)와 컬링이 같은 값을 본다.
        // 추가 뷰(캡처 카메라, 화면 사각형)는 패킷 경로(Engine)와 같은 수집기와 스케줄러로 고른다. 시각은 렌더러 시계를 쓴다(테스트는 고정할 수 있다).
        _listDirectViewScratch.clear();
        const shared_ptr<const OcclusionTileSnapshot> occlusionSnapshot = _occlusionMailbox.getLatest();
        if ( scene.getObjectManager() != nullptr )
        {
            RenderViewCollector::collectExtraViews( *scene.getObjectManager(), pMainCamera, view( RenderViewType::Main )._viewProj, cameraPos, _outputWidth, _outputHeight, RenderViewCollector::kDirectPathAutoScale,
                                                    static_cast<float64>( getAnimationTime() ), RenderViewCollector::getDefaultBudget(), _directViewScheduler,
                                                    occlusionSnapshot.get(), _listDirectViewScratch );
        }
        // 추가 뷰마다 자기 카메라에 맞춘 방향광 그림자 — 패킷 경로(RenderPacketBuilder)와 같은 함수다.
        for ( RenderViewRequest& request : _listDirectViewScratch )
        {
            if ( request._bRender == SW_TRUE )
                request._shadow = ShadowSetupUtil::makeViewShadowFor( pKeyLight != nullptr ? pShadowLight : nullptr, request._viewProj, _frameShadow );
        }
        // 패킷 경로와 **같은 경로**다. 빌더가 스냅샷을 만들고 RT 쪽이 받는다. 렌더 스레드 쪽 GPUScene 에는 씬을 읽는 메서드가 없다.
        // 주 카메라의 정렬 축도 패킷 경로(Engine)처럼 설정한다. 직교 카메라는 시선 축을, 원근 카메라는 거리를 쓴다.
        _sceneBuilder.setTransparentSortAxis( engine::getSubsystem<Render2DSettings>().computeTransparentSortAxis( pMainCamera->isOrthographic(), pMainCamera->getCameraForward() ) );
        _sceneBuilder.populateFromScene( pScene, cameraPos );
        _sceneBuilder.computeViewTransparentOrders( _listDirectViewScratch );
        prepareExtraViews( _listDirectViewScratch );
        {
            // 스크래치 하나를 돌려 쓴다. 맞바꾸기라서 지난 스냅샷의 저장소가 여기로 돌아온다.
            _sceneBuilder.exportCPUSnapshot( _sceneSnapshotScratch );
            _gpuScene.adoptCPUSnapshot( _sceneSnapshotScratch );
        }
        return uploadSceneAndSubmit( pDevice, "execute" );
    }

    bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        const bool bNotExecutable = isReady() == false || pDevice == nullptr || packet._bValid == SW_FALSE;
        if ( bNotExecutable )
            return false;

        // 주 시점이 없는 패킷은 그리지 않는다 — 기본 카메라로 대신 그리면 카메라가 없다는 결함이 화면으로 가려진다. 활성 씬이 없는 프레임은
        // 그릴 3D 내용도 없어 조용히 넘기고, 3D 내용을 실은 채 시점이 빠진 패킷(만든 쪽의 결함)만 한 번 알린다. 씬에 카메라가 없는 것은 게임 스레드가 알린다.
        if ( packet._bHasViewProj == SW_FALSE )
        {
            static std::atomic<bool> s_bMissingViewReported{ false };
            const bool               bCarries3DContent = packet._gpuScene.getInstances().empty() == false || packet._listView.empty() == false;
            if ( bCarries3DContent && s_bMissingViewReported.exchange( true ) == false )
                SW_LOG_ERROR( "Render packet carries scene content but no main view (RenderFramePacket::setMainView) - the frame is not drawn" );
            return false;
        }

        // 게임 스레드가 맡긴 텍스처 구간(지형 스플랫 칠하기)을 기록 전에 올린다. 업로드는 뒤이은 드로우보다 먼저 실행된다(IRHIResourceFactory 계약).
        if ( engine::areEngineServicesBound() )
        {
            SW_PROFILE_SCOPE( "RT.TextureRegionUpdates" );
            TextureCache& textures = engine::getSubsystem<AssetManager>().getAssetCache<TextureCache>();
            (void)textures.flushRegionUpdates( pDevice );
            // 워커가 읽어 둔 비동기 텍스처를 올린다 — 디바이스를 쓰는 스레드는 여기다(DX11 즉시 컨텍스트 · GL). 프레임당 상한이 있다.
            (void)textures.completePendingLoads( pDevice, TextureCache::kMaxUploadsPerFrame );
            // 아래쪽 밉만 올린 텍스처의 위쪽 밉을 프레임 예산 안에서 업로드한다(밉 스트리밍). 옛 자원은 게임 스레드가 해제한다(`Engine`).
            (void)textures.updateStreaming( pDevice, TextureCache::kMaxStreamingUpgradesPerFrame );
        }

        _pDevice            = pDevice;
        _outputRenderTarget = packet._outputRenderTarget;
        _mainView._settings = packet._mainView;
        // _gpuScene 은 FrameRenderer 가 프레임 사이에 계속 소유한다(GPU 버퍼, 핸들, 머티리얼 데이터 버퍼를 보존한다).
        // 패킷에서는 CPU 스냅샷(인스턴스와 배치 목록)만 옮겨 온다. 주의: 통째로 move 하면 직전 프레임에 업로드한
        // GPU 버퍼와 디스크립터를 releaseGPU() 없이 잃어버려 매 프레임 새로 만드는 누수가 된다.
        _gpuScene.adoptCPUSnapshot( packet._gpuScene );
        // 캔버스(화면 2D)도 패킷으로만 온다(렌더 스레드는 위젯을 볼 수 없다). 맞바꾸기라 지난 프레임의 저장소가 패킷으로 돌아간다.
        setCanvasFrame( packet._canvas );

        // 주 광원은 씬이 아니라 패킷으로 온다. 렌더 스레드는 씬을 볼 수 없다.
        if ( packet._bHasLight != SW_FALSE )
        {
            _frameLight._dirIntensity = packet._lightDirIntensity;
            _frameLight._colorAmbient = packet._lightColorAmbient;
        }
        else
        {
            _frameLight = FrameLightState{};
        }
        // 하늘도 패킷으로만 온다.
        applyFrameSky( packet._bHasSky != SW_FALSE, packet._sky );
        applyFrameFog( packet._bHasFog != SW_FALSE, packet._fog );
        // 그림자 배치도 패킷으로만 온다. 맞바꾸기라 지난 프레임의 저장소가 패킷으로 돌아간다.
        std::swap( _frameShadow, packet._shadow );
        applyFrameShadow();
        // 씬의 모든 라이트다. 방향광과 점광이 한 버퍼에 섞여 올라가고 포워드와 디퍼드가 같이 읽는다. 점광 · 스폿 그림자 타일은 그 뒤에 붙는다.
        // 비어 있으면 셰이더는 앰비언트만 더한다 — 라이트 컴포넌트가 없는 씬은 어둡다(키 라이트로 대신 비추지 않는다).
        _cookieAtlas.assignCells( pDevice, packet._listLightCookie, packet._listLight );
        _listFrameRealtimeProbe = packet._listRealtimeProbe;
        _lightBuffer.populate( packet._listLight, _frameShadow._listTileElement, packet._listLightProbe, _listFrameRealtimeProbe.empty() == false );

        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        setIdentityWorld( _frameContext );
        // 시드는 씬 경로와 같은 updatePassConstants 로 채운다. 따로 채우면 라이트, 블룸, 아웃라인 색 상수가
        // 빠지기 쉽다. 뷰-투영은 패킷의 주 시점이다(역행렬과 절두체도 함께 갱신된다).
        updatePassConstants( _frameContext, packet._viewProj );
        view( RenderViewType::Main )._position = packet._cameraPos;
        // 추가 뷰의 풀, 텍스처, 컬링 슬롯을 업로드 전에 맞춘다(기록 중에는 만들 수 없다).
        prepareExtraViews( packet._listView );
        // 값 업로드와 바인딩은 드로우 직전에 ShaderParameterBinder 가 한다. 여기서는 시드만 채운다.
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        return uploadSceneAndSubmit( pDevice, "executePacket" );
    }

    void FrameRenderer::updateLightBuffer( IRHIDevice* pDevice )
    {
        // 그리드 번호는 이 목록의 순서다 — 주 시점이 0 번, 이번 프레임에 그리는 추가 뷰가 그 뒤다. 뷰-투영은 그 뷰의 g_ViewProj 와 같은 값이다.
        _listScratchGridViewProj.clear();
        _mainView._lightGridIndex = invalid_index::kUint32;
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            pView->_lightGridIndex = invalid_index::kUint32;
        }
        const LightGridProducer producer = selectLightGridProducer();
        if ( producer != LightGridProducer::None )
        {
            _mainView._lightGridIndex = static_cast<uint32>( _listScratchGridViewProj.size() );
            _listScratchGridViewProj.push_back( view( RenderViewType::Main )._viewProj );
            for ( unique_ptr<ViewTarget>& pView : _listExtraView )
            {
                if ( pView->_bRenderThisFrame == SW_FALSE )
                    continue;
                pView->_lightGridIndex = static_cast<uint32>( _listScratchGridViewProj.size() );
                _listScratchGridViewProj.push_back( pView->_cullInput._viewProj );
            }
        }
        // GPU 방출기의 파티클 라이트 — 방출기가 요청한 슬롯의 합이 라이트 버퍼의 GPU 구간 크기다(GPU 생산자가 아니면 버퍼가 거절하고 오류를 남긴다).
        // 범위 나누기는 시뮬레이션(prepareFrame)이 같은 함수로 다시 한다.
        const VfxGPULightAssignment lightAssignment = VfxGPUSimulation::assignLightRanges( _gpuScene.getParticleBatches(), _gpuScene.getVfxMeshEmitters(), _listScratchGPULightRange );
        _lightBuffer.update( pDevice, _listScratchGridViewProj, producer, lightAssignment._slotCount );
    }

    LightGridProducer FrameRenderer::selectLightGridProducer()
    {
#if defined( SW_SHIPPING )
        const int32 requested = 2;
#else
        const int32 requested = gv_lightCulling;
#endif
        if ( requested == 0 )
            return LightGridProducer::None;
        if ( requested == 1 )
            return LightGridProducer::CPU;
        // GPU 생산자는 컴퓨트 PSO 가 있어야 한다. 없으면 그리드 버퍼를 채울 쪽이 없다 — 채우지 않은 버퍼를 읽게 두지 않고 그리드 없이 그린다(그림은 같고 비용만 다르다).
        if ( getEnginePSO( RenderPassType::LightGrid ) != 0 )
            return LightGridProducer::GPU;
        if ( _bLightGridPSOMissingLogged == SW_FALSE )
        {
            _bLightGridPSOMissingLogged = SW_TRUE;
            SW_LOG_ERROR( "The light grid compute PSO (%#) is missing - every pixel walks every light", engine::getSubsystem<EngineDefaultAssets>()._shaderLightGrid.c_str() );
        }
        return LightGridProducer::None;
    }

    void FrameRenderer::setCanvasFrame( CanvasFrameData& inoutFrame )
    {
        std::swap( _canvasFrame, inoutFrame );
    }

    bool FrameRenderer::uploadSceneAndSubmit( IRHIDevice* pDevice, const utf8* pCallerName )
    {
        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다. 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 되었는지는 upload 뒤에 areIndirectCountsGPUFilled() 가 알려 준다.
        _gpuScene.setIndirectCountsFilledByGPU( usesGPUGeneratedCommands() );
        // 모프 풀 오프셋은 배치 테이블에 실려 업로드 시점에 완성되어 있어야 한다.
        prepareMeshMorphPool();
        int32 vertexPool = gv_vertexPool;
#if !defined( SW_SHIPPING )
        if ( _debugOverrides._vertexPool >= 0 )
            vertexPool = _debugOverrides._vertexPool;
#endif
        _gpuScene.setVertexPoolEnabled( vertexPool != 0 );
        _gpuScene.upload( pDevice );
        // 라이트 버퍼는 뷰가 정해진 뒤에 올린다 — 뷰마다 라이트 그리드가 붙는다(주 시점과 이번 프레임에 그리는 추가 뷰).
        updateLightBuffer( pDevice );
        // 볼륨 안개의 버퍼와 입력은 라이트 그리드(주 시점의 그리드 번호)가 정해진 뒤, 기록 전에 갖춘다.
        prepareVolumetricFog( pDevice );
        // 실시간 라이트 프로브 갱신도 라이트 버퍼(프로브 버퍼의 UAV)가 올라간 뒤, 기록 전에 갖춘다.
        prepareRealtimeProbes( pDevice );
        // 추가 뷰의 투명 순서(스냅샷)를 뷰 자원에 옮긴다. 컬링과 정렬 디스패치, 기록보다 먼저 한다.
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE )
                applyViewTransparentOrder( *pView );
        }

        if ( _bCallbacksBound == SW_FALSE )
            bindPassCallbacks();

        // 상수 버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다.
        ensurePassCbCapacityForFrame();

        // 머티리얼 퍼뮤테이션 PSO 도 같은 이유로 여기서 만든다. 기록 중에는 만들 수 없고, 패스들은 병렬로 기록된다.
        ensureMaterialPSOs();
        // 아직 씬에 없는 고정 머티리얼(프리로드)의 PSO 도 같은 자리에서 미리 만든다.
        warmPinnedMaterialPSOs();

        // 캔버스의 아틀라스 텍스처, 사각형 버퍼, 렌더 텍스처 대상도 기록 전에 갖춘다(Canvas 패스와 대상 그리기는 그리기만 한다).
        _canvasRenderer.prepareFrame( *pDevice, _canvasFrame );
        // 입자 버퍼 · 입자 텍스처 · 입자 PSO 도 같다(투명 패스는 그리기만 한다).
        ensureParticlePSO();
        _particleRenderer.prepareFrame( *pDevice, _gpuScene.getParticleBatches(), _gpuScene.getVfxMeshEmitters(), makeVfxSimulationInput() );

        if ( prepareCommandList( pDevice, pCallerName ) == false )
            return false;

        const bool bOk             = submitGraph( pDevice );
        _lastIndirectDrawCallCount = _indirectDrawCallCount.load( std::memory_order_relaxed );
        // 타임스탬프 슬롯은 디바이스에 하나다 — 미리보기 렌더러가 읽고 켜고 끄면 주 렌더러의 GPU 시간이 흔들린다.
        if ( _bScenePreview == SW_FALSE )
            _gpuPassTimeline.readAndReport( pDevice, _pipelineResource.getGraphPass() );
        return bOk;
    }
} // namespace sw
