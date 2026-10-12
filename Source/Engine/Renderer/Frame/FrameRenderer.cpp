#include "pch.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/2D/Render2DSettings.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Renderer/Frame/RenderViewCollector.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAssetCache.h"

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
     * @brief `-gv_renderPipeline=<파이프라인 XML>` 이면 그 파이프라인으로 그립니다(디퍼드 선택 · 게임 프리셋 · 기본보다 우선합니다).
     * @details 게임은 데이터로 고른다 — 게임 프리셋의 `GameConfig::_renderPipeline`(2D 게임은 그림자 맵과 톤맵이 없는
     *          `engine/pipeline/forward2dpipeline.xml`. 톤맵은 흰색을 0.5 로 누르기 때문이다). 이 스위치는 측정 · 검증용으로 그것을 덮는다.
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_renderPipeline, "", "이 파이프라인 XML 로 그린다 (비우면 gv_deferred · 게임 프리셋 · 기본 포워드)" );

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

    /**
     * @brief `-gv_viewMode=<n>` 은 주 출력(게임 창)의 보기 모드입니다(0 Lit, 1 Unlit, 2 Wireframe, 3 Normals, 4 Depth, 5 Overdraw).
     * @details 에디터 씬 뷰 툴바와 같은 값을 가리킨다(`RenderViewMode`). 여기 있는 이유는 **검증**이다.
     *          뷰 모드가 정말로 픽셀을 바꾸는지 `-gv_screenshot` 으로 확인하려면 에디터를 띄우지 않고
     *          모드를 고를 수 있어야 한다. 없으면 이 기능은 사람 눈으로만 확인되는 기능이 된다.
     */
    SW_GLOBAL_VARIABLE( int32, gv_viewMode, 0, "주 출력 보기 모드 (0 Lit / 1 Unlit / 2 Wireframe / 3 Normals / 4 Depth / 5 Overdraw)" );

    FrameRenderer::FrameRenderer()
        : _pDevice{ nullptr }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{ nullptr }
        , _pCmd{ nullptr }
        , _pScene{ nullptr }
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
        , _drawMergeOverride{ -1 }
        , _vertexPoolOverride{ -1 }
        , _indirectDrawCallCount{ 0 }
        , _gpuPassTimeline{}
        , _lastIndirectDrawCallCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _listMaterialFallback{}
        , _mapLayoutMaterialFallback{}
        , _transparentRankPlaceholder{}
        , _psoCache{}
        , _outputRenderTarget{ 0 }
        , _presentCapture{}
        , _canvasRenderer{}
        , _canvasFrame{}
        , _listCanvasTarget{}
        , _lastDrawnCanvasTargetCount{ 0 }
        , _pLastSwapchainWriter{ nullptr }
        , _pMainOutputCanvasView{ nullptr }
        , _statusMessage{}
        , _graphContext{}
        , _frameLight{}
        , _outputWidth{ 0 }
        , _outputHeight{ 0 }
        , _lastRenderedExtraViewCount{ 0 }
        , _directOutputWidth{ 0 }
        , _directOutputHeight{ 0 }
        , _bGPUCullingActive{ SW_FALSE }
        , _status{ FrameRendererStatus::Uninitialized }
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
        , _reservedFlags{ 0 }
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
            bindServices( &engine::getTaskManager() );
        // 패스별 GPU 시간을 보고할 프로파일러다. 서비스가 없는 실행(서비스 없이 렌더러만 쓰는 도구)은 계측하지 않는다.
        _gpuPassTimeline.bindProfiler( engine::areEngineServicesBound() ? &engine::getFrameProfiler() : nullptr );

        _renderPipelineAssetCache = make_unique<RenderPipelineAssetCache>();
        if ( _renderPipelineAssetCache->initialize() == false )
        {
            _renderPipelineAssetCache.reset();
            _status        = FrameRendererStatus::Failed;
            _statusMessage = "RenderPipelineAssetCache initialize failed";
            SW_LOG_ERROR( "initialize: %#", _statusMessage );
            return false;
        }

        // 커맨드라인이 주 출력의 보기 모드를 정했으면 여기서 받는다(에디터 씬 뷰는 툴바가 따로 정한다).
        if ( gv_viewMode > 0 )
            setViewMode( static_cast<RenderViewMode>( gv_viewMode ) );

        // 동기 경로(execute)는 렌더러 자신의 빌더가 배치를 만든다. 패킷 경로의 빌더(EngineLoop)에는 EngineLoop 가 같은 값을 준다.
        _sceneBuilder.setMergeBatchesAcrossMaterials( pDevice->supportsNativeBindlessSampling() );

        const EngineDefaultAssets& engineDefaultAssets = engine::getEngineDefaultAssets();
        RenderPipelineAssetCache&  rpm                 = *_renderPipelineAssetCache;
        if ( rpm.findRenderPass( hashed_string( FrameRendererUtil::kDefaultMainPassName ) ) == nullptr )
            rpm.loadRenderPass( engineDefaultAssets._defaultRenderPass );

        // 인자, `-gv_renderPipeline`, `-gv_deferred`, 게임 프리셋(GameConfig::_renderPipeline), EngineDefaultAssets 의 포워드 순서로 고른다.
        // 디퍼드 경로(와 그 위의 조명)를 측정하거나 검증하려면 `-gv_deferred=1` 을 준다.
        FrameRendererUtil::PipelineChoice choice{};
        choice._argumentPath               = pipelineXMLPath;
        choice._commandLinePath            = gv_renderPipeline;
        choice._deferredPath               = engineDefaultAssets._defaultDeferredPipeline;
        choice._gamePath                   = GameConfig::getActive()._renderPipeline;
        choice._forwardPath                = engineDefaultAssets._defaultForwardPipeline;
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
        if ( _status == FrameRendererStatus::Uninitialized && _pDevice == nullptr && _pipelinePath.empty() )
            return;

        // 스냅샷이 가진 머티리얼과 인스턴스의 소유권을 **디바이스가 살아 있을 때** 해제한다. 스냅샷은 소유권을 함께
        // 싣고 있으므로(GPUScene.h) 여기서 비우지 않으면 렌더러가 소멸할 때까지 그것들이 살아 있어, 디바이스가 먼저
        // 사라진 뒤 소멸자가 사라진 디바이스에 GPU 자원을 반환하려 한다(해제 후 사용).
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
        return ( ( _drawMergeOverride >= 0 ) ? _drawMergeOverride : gv_drawMerge ) != 0;
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
        const uint32 animationInstanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
        dispatchInstanceAnimation( animationInstanceCount );
        dispatchMeshMorph();
        dispatchMeshSkin();
        dispatchCullAndSort( animationInstanceCount );
        // 월드 공간 UI 의 렌더 텍스처다. 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트(같은 큐)로 그린다.
        drawCanvasTargets();

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
        uint32 renderedExtraCount = 0;
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_outputKind != RenderViewOutputKind::RenderTexture )
                continue;
            renderExtraView( pDevice, *pView );
            ++renderedExtraCount;
        }
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

        // 병렬 기록이 가능하면(백엔드 capability, TaskManager, 레벨이 나올 만큼 컴파일된 그래프) 레벨마다 여러 리스트로 기록한다.
        // 첫 프레임처럼 그래프가 아직 컴파일되지 않았으면(getExecutionOrder() 가 비어 있으면) 직렬 경로로 간다.
        const bool bCanRunParallel = _pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != SW_FALSE &&
                                     _graph.getExecutionOrder().size() > 1;
        bool bOk = false;
        if ( bCanRunParallel )
        {
            bOk = _graph.executeParallel( _graphContext, _pTaskManager, pDevice );
        }
        else
        {
            // 직렬 경로도 그래프가 추론한 배리어를 쓴다. 패스가 기록하는 리스트와 **같은 것**을 넘긴다.
            if ( _mainView._commandList == nullptr )
                _mainView._commandList = pDevice->createCommandList();
            IRHICommandList* pMainCmd = _mainView._commandList.get();
            if ( pMainCmd != nullptr )
            {
                // 직렬 경로의 패스는 시드의 리스트에 기록한다(그래프가 패스에 리스트를 주지 않는다). 그래서 시드를 이 리스트로 돌린다.
                _frameContext._pCmd = pMainCmd;
                pMainCmd->beginCommandList();
                bOk = _graph.execute( _graphContext, pMainCmd );
                pMainCmd->endCommandList();
                pDevice->executeCommandList( pMainCmd );
            }
        }

        // 화면 사각형 뷰(분할 화면, PiP)는 주 시점 위에 겹친다.
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_outputKind != RenderViewOutputKind::ScreenRect )
                continue;
            renderExtraView( pDevice, *pView );
            ++renderedExtraCount;
        }
        // 호스트 타깃 뷰(에디터 씬 뷰)는 자기 RT 에 그린다. 이번 프레임에 그린 렌더 텍스처(CCTV)를 읽을 수 있도록 마지막에 그린다.
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_outputKind != RenderViewOutputKind::HostTarget )
                continue;
            renderExtraView( pDevice, *pView );
            ++renderedExtraCount;
        }
        _frameContext               = _mainSeedScratch;
        _lastRenderedExtraViewCount = renderedExtraCount;
        return bOk;
    }

    // ---------------------------------------------------------------------------

    bool FrameRenderer::execute( IRHIDevice* pDevice, Scene* pScene )
    {
        if ( isReady() == false || pDevice == nullptr )
            return false;

        _pDevice            = pDevice;
        _pScene             = pScene;
        _outputRenderTarget = 0;
        // 주 시점의 출력 설정(사각형, 배율, 끌 기능, 컷)은 패킷 경로와 같은 규칙으로 카메라에서 읽는다.
        CameraComponent* pMainCamera = nullptr;
        if ( pScene != nullptr )
        {
            pScene->ensureDefaultCameras();
            pMainCamera = pScene->getActiveGameCamera();
        }
        _mainView._settings           = RenderViewCollector::makeMainSettings( pMainCamera );
        _mainView._settings._viewMode = getViewMode(); // 직접 경로(테스트)는 주 출력 모드를 쓴다. 패킷 경로는 EngineLoop 가 싣는다.
        // 씬 직접 경로(에디터, 테스트)도 패킷 경로와 **같은 라이트 버퍼**를 쓴다. 경로마다 조명이
        // 다르면 에디터에서 본 이미지와 게임 화면이 달라진다.
        collectSceneLights( pScene, _listScratchLight );
        _lightBuffer.update( pDevice, _listScratchLight );
        // 주 광원(그림자 행렬, 앰비언트, 목록이 비었을 때의 폴백)도 패킷 경로(EngineLoop)와 **같은 규칙**으로
        // 씬에서 읽는다. 주의: 이 경로가 주 광원을 채우지 않으면 테스트가 씬에 둔 방향광이 이미지에 반영되지 않아,
        // 조명이 필요한 픽셀 검증(블룸이 1을 넘는 위치)이 불가능해진다.
        DirectionalLightComponent* pKeyLight    = ( pScene != nullptr ) ? pScene->findActiveDirectionalLight() : nullptr;
        DirectionalLightComponent* pShadowLight = ( pScene != nullptr ) ? pScene->findShadowCastingDirectionalLight() : nullptr;
        if ( pKeyLight != nullptr )
        {
            const float3 lightDir     = pKeyLight->getLightDirection();
            const float3 lightColor   = pKeyLight->getColor();
            _frameLight._dirIntensity = float4{ lightDir._x, lightDir._y, lightDir._z, pKeyLight->getIntensity() };
            _frameLight._colorAmbient = float4{ lightColor._x, lightColor._y, lightColor._z, pKeyLight->getAmbient() };
            if ( pShadowLight != nullptr )
            {
                // 볼륨 맞춤은 패킷 경로(EngineLoop)와 같은 규칙으로 주 시점 카메라의 뷰-투영을 쓴다.
                const uint32                      shadowResolution = getShadowMapResolution();
                const DirectionalShadowProjection shadow =
                    pMainCamera != nullptr
                        ? pShadowLight->computeShadowProjectionForView(
                              pMainCamera->getViewProjectionMatrix( RenderViewCollector::computeAspect( _mainView._settings, _directOutputWidth, _directOutputHeight ) ),
                              shadowResolution )
                        : pShadowLight->computeShadowProjection( shadowResolution );
                _frameLight._shadowViewProj = shadow._viewProj;
                _frameLight._shadowParams   = shadow.computeShaderParams();
            }
            else
            {
                _frameLight._shadowViewProj = float4x4{};
                _frameLight._shadowParams   = float4{};
            }
            _frameLight._bHasShadowViewProj = SW_TRUE;
        }
        else
        {
            _frameLight = FrameLightState{};
        }
        ensurePassResources();
        ensureTransientResources( _directOutputWidth, _directOutputHeight );
        resetPassCbRing();
        setIdentityWorld( _frameContext );

        updatePassConstants( _frameContext );
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        float3 cameraPos{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] };
        if ( pMainCamera != nullptr )
            cameraPos = pMainCamera->getCameraPosition();
        view( RenderViewType::Main )._position = cameraPos; // 정렬 키(카메라까지의 거리)와 컬링이 같은 값을 본다.
        // 추가 뷰(캡처 카메라, 화면 사각형)는 패킷 경로(EngineLoop)와 같은 수집기와 스케줄러로 고른다. 시각은 렌더러 시계를 쓴다(테스트는 고정할 수 있다).
        _listDirectViewScratch.clear();
        if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
        {
            RenderViewCollector::collectExtraViews( *pScene->getObjectManager(), pMainCamera, view( RenderViewType::Main )._viewProj, _outputWidth, _outputHeight,
                                                    static_cast<float64>( getAnimationTime() ), RenderViewCollector::getDefaultBudget(), _directViewScheduler,
                                                    _listDirectViewScratch );
        }
        // 패킷 경로와 **같은 경로**다. 빌더가 스냅샷을 만들고 RT 쪽이 받는다. 렌더 스레드 쪽 GPUScene 에는 씬을 읽는 메서드가 없다.
        // 주 카메라의 정렬 축도 패킷 경로(EngineLoop)처럼 설정한다. 직교 카메라는 시선 축을, 원근 카메라는 거리를 쓴다.
        if ( pMainCamera != nullptr )
            _sceneBuilder.setTransparentSortAxis( Render2DSettings::getActive().computeTransparentSortAxis( pMainCamera->isOrthographic(), pMainCamera->getCameraForward() ) );
        _sceneBuilder.populateFromScene( pScene, cameraPos );
        _sceneBuilder.computeViewTransparentOrders( _listDirectViewScratch );
        prepareExtraViews( _listDirectViewScratch );
        {
            // 스크래치 하나를 돌려 쓴다. 맞바꾸기라서 지난 스냅샷의 저장소가 여기로 돌아온다.
            _sceneBuilder.exportCPUSnapshot( _sceneSnapshotScratch );
            _gpuScene.adoptCPUSnapshot( _sceneSnapshotScratch );
        }
        const bool bOk = uploadSceneAndSubmit( pDevice, "execute" );
        _pScene        = nullptr;
        return bOk;
    }

    bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        if ( isReady() == false || pDevice == nullptr || packet._bValid == SW_FALSE )
            return false;

        _pDevice            = pDevice;
        _pScene             = nullptr;
        _outputRenderTarget = packet._outputRenderTarget;
        _mainView._settings = packet._mainView;
        // _gpuScene 은 FrameRenderer 가 프레임 사이에 계속 소유한다(GPU 버퍼, 핸들, 머티리얼 데이터 버퍼를 보존한다).
        // 패킷에서는 CPU 스냅샷(인스턴스와 배치 목록)만 옮겨 온다. 주의: 통째로 move 하면 직전 프레임에 업로드한
        // GPU 버퍼와 디스크립터를 releaseGPU() 없이 잃어버려 매 프레임 새로 만드는 누수가 된다.
        _gpuScene.adoptCPUSnapshot( packet._gpuScene );
        // 캔버스(화면 2D)도 패킷으로만 온다(렌더 스레드는 위젯을 볼 수 없다). 맞바꾸기라 지난 프레임의 저장소가 패킷으로 돌아간다.
        setCanvasFrame( packet._canvas );

        // 주 광원은 씬이 아니라 패킷으로 온다. 렌더 스레드는 씬을 볼 수 없다(_pScene = nullptr).
        if ( packet._bHasLight != SW_FALSE )
        {
            _frameLight._dirIntensity       = packet._lightDirIntensity;
            _frameLight._colorAmbient       = packet._lightColorAmbient;
            _frameLight._shadowViewProj     = packet._lightViewProj;
            _frameLight._shadowParams       = packet._shadowParams;
            _frameLight._bHasShadowViewProj = SW_TRUE;
        }
        else
        {
            _frameLight = FrameLightState{};
        }
        // 씬의 모든 라이트다. 방향광과 점광이 한 버퍼에 섞여 올라가고 포워드와 디퍼드가 같이 읽는다.
        // 비어 있으면 셰이더가 위의 키 라이트로 폴백하므로 라이트 컴포넌트가 없는 씬도 그대로 그려진다.
        _lightBuffer.update( pDevice, packet._listLight );

        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        setIdentityWorld( _frameContext );
        // 시드는 씬 경로와 같은 updatePassConstants 로 채운다. 따로 채우면 라이트, 블룸, 아웃라인 색 상수가
        // 빠지기 쉽다. 패킷이 자기 뷰 행렬을 가지고 있을 때만 그 위에 덮어쓴다.
        // _pScene 이 null 이라 updatePassConstants 는 폴백 뷰를 만든다.
        updatePassConstants( _frameContext );
        if ( packet._bHasViewProj != SW_FALSE )
            applyViewProjection( _frameContext, packet._viewProj ); // 역행렬과 절두체도 함께 갱신된다.
        view( RenderViewType::Main )._position = packet._cameraPos;
        // 추가 뷰의 풀, 텍스처, 컬링 슬롯을 업로드 전에 맞춘다(기록 중에는 만들 수 없다).
        prepareExtraViews( packet._listView );
        // 값 업로드와 바인딩은 드로우 직전에 ShaderParameterBinder 가 한다. 여기서는 시드만 채운다.
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        return uploadSceneAndSubmit( pDevice, "executePacket" );
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
        _gpuScene.setVertexPoolEnabled( ( ( _vertexPoolOverride >= 0 ) ? _vertexPoolOverride : gv_vertexPool ) != 0 );
        _gpuScene.upload( pDevice );
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

        // 캔버스의 아틀라스 텍스처, 사각형 버퍼, 렌더 텍스처 대상도 기록 전에 갖춘다(Canvas 패스와 대상 그리기는 그리기만 한다).
        prepareCanvasTargets();
        _canvasRenderer.prepareFrame( *pDevice, _canvasFrame );

        if ( prepareCommandList( pDevice, pCallerName ) == false )
            return false;

        const bool bOk             = submitGraph( pDevice );
        _lastIndirectDrawCallCount = _indirectDrawCallCount.load( std::memory_order_relaxed );
        _gpuPassTimeline.readAndReport( pDevice, _pipelineResource.getGraphPass() );
        return bOk;
    }
} // namespace sw
