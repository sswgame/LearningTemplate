#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_deferred=1` — 기본 파이프라인을 디퍼드로 고릅니다 (기본은 포워드).
     * @details 예전에는 고를 길이 자체가 없었다 — `initialize` 의 인자를 주는 호출부가 없어 늘 포워드였다.
     *          디퍼드는 웨이브가 갈려(웨이브0 = Shadow + GBuffer) 병렬 기록이 실제로 도는 유일한 경로이기도 하다.
     */
    SW_GLOBAL_VARIABLE_BOOL( gv_deferred, false, "기본 파이프라인을 디퍼드로 (기본 포워드)" );

    /**
     * @brief `-gv_drawMerge=0` — 같은 PSO·머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
     * @details 묶은 그림과 안 묶은 그림이 같아야 한다 — 다르면 배치 표(g_SwBatches)나 드로우 ID 가 틀린 것이다. 기본은 묶음.
     */
    SW_GLOBAL_VARIABLE_INT( gv_drawMerge, 1, "씬 배치 멀티 드로우 묶기 (0=배치마다 호출, 진단용)" );

    /**
     * @brief `-gv_vertexPool=0` — 씬 메시 정점을 한 풀 버퍼에 모으지 않고 메시마다 자기 정점 버퍼로 그립니다.
     * @details 풀을 켠 그림과 끈 그림은 같아야 한다 — 다르면 간접 인자의 startVertex 나 SV_VertexID 의 API 차이가 잘못 다뤄진 것이다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_vertexPool, 1, "씬 메시 정점 풀 (0=메시마다 정점 버퍼, 진단용)" );

    /**
     * @brief `-gv_viewMode=<0|1|2>` — 씬 지오메트리 보기 방식 (0 Lit / 1 Unlit / 2 Wireframe).
     * @details 에디터 뷰포트 콤보와 같은 값을 가리킨다(`RenderViewMode`). 여기 있는 이유는 **검증**이다 —
     *          뷰 모드가 정말로 픽셀을 바꾸는지 `-gv_screenshot` 으로 확인하려면 에디터를 띄우지 않고
     *          모드를 고를 수 있어야 한다. 없으면 이 기능은 사람 눈으로만 확인되는 기능이 된다.
     */
    SW_GLOBAL_VARIABLE_INT( gv_viewMode, 0, "씬 보기 방식 (0 Lit / 1 Unlit / 2 Wireframe)" );

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
        , _transientPool{}
        , _frameCtx{}
        , _passCbRing{}
        , _arrView{}
        , _instanceAnimCb{}
        , _meshMorphCb{}
        , _bMorphBindsRest{ SW_FALSE }
        , _meshMorphDiagOverride{ -1 }
        , _drawMergeOverride{ -1 }
        , _vertexPoolOverride{ -1 }
        , _indirectDrawCallCount{ 0 }
        , _lastIndirectDrawCallCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _instanceSortCb{}
        , _bGpuCullingActive{ SW_FALSE }
        , _mapMaterialFallback{}
        , _psoCache{}
        , _viewMode{ static_cast<uint8>( RenderViewMode::Lit ) }
        , _bPresentPsoMissingLogged{ 0 }
        , _bMaterialFallbackMissingLogged{ 0 }
        , _outputRenderTarget{ 0 }
        , _taaHistory{ 0 }
        , _taaHistorySrv{ kInvalidDescriptorIndex }
        , _presentCapture{ 0 }
        , _bPresentCaptureEnabled{ SW_FALSE }
        , _status{ FrameRendererStatus::Uninitialized }
        , _statusMessage{}
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _bHasExecutedDepthPrepass{ 0 }
        , _graphContext{}
    {
    }

    FrameRenderer::~FrameRenderer()
    {
        shutdown();
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, string_view pipelineXmlPath )
    {
        return initialize( pDevice, nullptr, pipelineXmlPath );
    }

    bool FrameRenderer::initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                                    string_view pipelineXmlPath )
    {
        _pDevice = pDevice;
        // 인스턴스 애니메이션 시계는 여기서 한 번 돌린다 (CpuTimer 는 만들면 중지 상태다).
        _animTimer.resetTimer();
        _animTimer.startTimer();
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

        // 커맨드라인이 뷰 모드를 정했으면 여기서 받는다. 에디터가 있으면 툴바가 다시 덮어쓴다 —
        // 초기값이므로 순서가 맞다.
        if ( gv_viewMode > 0 )
            setViewMode( static_cast<RenderViewMode>( gv_viewMode ) );

        // 동기 경로(execute)는 렌더러 자신의 빌더가 배치를 만든다 — 패킷 경로의 빌더(EngineLoop)에는 EngineLoop 가 같은 값을 준다.
        _sceneBuilder.setMergeBatchesAcrossMaterials( pDevice->supportsNativeBindlessSampling() );

        const EngineData&  engineData = engine::getEngineData();
        RenderPassManager& rpm        = pDevice->getRenderPassManager();
        if ( rpm.findRenderPass( hashed_string( FrameRendererUtil::kDefaultMainPassName ) ) == nullptr )
            rpm.loadRenderPass( engineData._defaultRenderPass );

        // 파이프라인을 실행 중에 고를 길이 없었다 — 인자를 주는 호출부가 하나도 없어서 **언제나 포워드**였다.
        // 그래서 디퍼드 경로(그리고 그 위의 조명)는 돌려 보려면 EngineData 를 고쳐야 했고, 실제로 거의
        // 돌지 않았다. 측정도 검증도 스위치 하나가 없어서 막혀 있던 자리다.
        string_view resolvedPipeline = pipelineXmlPath;
        if ( resolvedPipeline.empty() && gv_deferred )
            resolvedPipeline = engineData._defaultDeferredPipeline;
        if ( resolvedPipeline.empty() )
            resolvedPipeline = engineData._defaultForwardPipeline;

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

    const utf8* FrameRenderer::gpuScopeNameFor( const string& passName )
    {
        // 프로파일러는 이름 포인터를 들고 있으므로 **수명이 프레임을 넘겨야 한다** — 맵에 담아 둔다.
        const hashed_string key{ passName.c_str() };
        const auto          iter = _mapGpuScopeName.find( key );
        if ( iter != _mapGpuScopeName.end() )
            return iter->second.c_str();
        return _mapGpuScopeName.emplace( key, string( "GPU." ) + passName ).first->second.c_str();
    }

    void FrameRenderer::reportGpuPassTimes( [[maybe_unused]] IRHIDevice* pDevice )
    {
#if SW_LOG_LEVEL_COMPILED( 2 )
        // **몇 프레임 늦은 값이다.** 기다려서 최신 값을 받으면 재려던 그 파이프라인을 멈춰 세워
        // 숫자가 거짓이 된다 — 늦은 대신 정확한 쪽을 고른다.
        if ( pDevice == nullptr )
            return;

        // **여기가 계측 스위치다.** 프로파일러가 꺼져 있으면 백엔드는 쿼리 자원조차 만들지 않는다 —
        // Shipping 에서는 이 블록이 통째로 사라지므로 스위치가 켜질 일도 없다.
        pDevice->setTimestampEnabled( engine::getFrameProfiler().isEnabled() );
        if ( pDevice->getTimestampSlotCount() == 0 )
            return;
        if ( pDevice->readTimestampsMicros( _listGpuTimestampMicro ) == false )
            return;

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        for ( size_t passIndex = 0; passIndex < listPass.size(); ++passIndex )
        {
            const size_t beginSlot = passIndex * 2;
            if ( beginSlot + 1 >= _listGpuTimestampMicro.size() )
                break;

            const float32 beginMicro = _listGpuTimestampMicro[beginSlot];
            const float32 endMicro   = _listGpuTimestampMicro[beginSlot + 1];
            // 음수는 그 칸이 이번 프레임에 안 적혔다는 표시다(패스를 건너뛰었거나 첫 사이클) — 버린다.
            // 한쪽만 음수여도 구간이 성립하지 않으므로 둘 다 본다.
            if ( beginMicro < 0.0f || endMicro < 0.0f || endMicro < beginMicro )
                continue;

            const float32 micro = endMicro - beginMicro;
            const uint32  slot  = engine::getFrameProfiler().registerScope( gpuScopeNameFor( listPass[passIndex]._name ) );
            engine::getFrameProfiler().addSample( slot, static_cast<uint64>( micro * 1000.0f ) );
        }
#endif
    }

    void FrameRenderer::shutdown()
    {
        if ( _status == FrameRendererStatus::Uninitialized && _pDevice == nullptr && _pipelinePath.empty() )
            return;

        // 스냅샷이 든 머티리얼·인스턴스의 소유를 **디바이스가 살아 있을 때** 놓는다. 스냅샷은 소유를 함께
        // 실으므로(GpuScene.h) 여기서 비우지 않으면 렌더러가 죽을 때까지 그것들이 살아, 디바이스가 먼저
        // 사라진 뒤 소멸자가 죽은 디바이스에 GPU 자원을 돌려주려 한다(ASAN 이 잡았다).
        if ( _pDevice != nullptr )
            _gpuScene.releaseGpu( _pDevice );
        _gpuScene.clear();
        _sceneBuilder.clear();

        releaseTransientResources();
        releasePassResources();
        _graph.clear();
        // 리소스 상태 추적은 이름→상태라 디바이스가 바뀌어도 살아남는다 — 새 디바이스의 텍스처는 다시 Undefined 다.
        // 지우지 않으면 첫 프레임이 "이미 렌더타깃 상태" 로 믿고 배리어를 건너뛴다.
        _graphContext.reset();
        _frameCmd.reset();
        _pCmdOwnerDevice = nullptr;
        _pCmd            = nullptr;
        _frameCtx._pCmd  = nullptr;
        _pDevice         = nullptr;
        _pTaskManager    = nullptr;
        _status          = FrameRendererStatus::Uninitialized;
        _statusMessage.clear();
        _bCallbacksBound = SW_FALSE;
        _pipelinePath.clear();
        SW_LOG_INFO( "Shut down." );
    }

    bool FrameRenderer::loadPipeline( string_view pipelineXmlPath )
    {
        _pipelinePath    = pipelineXmlPath;
        _bCallbacksBound = SW_FALSE;
        _graph.clear();
        releaseTransientResources();

        if ( _pipelineResource.loadFromXmlFile( pipelineXmlPath ) == false )
        {
            _statusMessage = string( "failed to load pipeline XML: " ) + string( pipelineXmlPath );
            return false;
        }

        if ( _pDevice != nullptr )
        {
            RenderPassManager& rpm = _pDevice->getRenderPassManager();
            rpm.loadPipeline( pipelineXmlPath );
            for ( const string& passRef : _pipelineResource.getDesc()._listRenderPassRef )
            {
                if ( passRef.empty() == false )
                    rpm.loadRenderPass( passRef );
            }
        }

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        if ( listPass.empty() )
        {
            _statusMessage = string( "no graph passes in pipeline: " ) + string( pipelineXmlPath );
            SW_LOG_ERROR( "%#", _statusMessage );
            return false;
        }

        float4 sceneColorClear;
        if ( tryGetAttachmentClearColor( FrameRendererUtil::Attachment::kSceneColor, sceneColorClear ) )
            _clearColor = sceneColorClear;

        // Rebuild PSOs from pipeline pass recipes (shader / entry / blend / permutations).
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
            _frameCmd.reset();

        if ( _frameCmd == nullptr )
            _frameCmd = pDevice->createCommandList();

        _pCmdOwnerDevice = pDevice;
        _pCmd            = _frameCmd.get();
        // 패스 컨텍스트 시드도 같은 리스트를 가리키게 한다(직렬 경로가 이걸 쓴다).
        _frameCtx._pCmd = _pCmd;

        if ( _pCmd == nullptr )
        {
            SW_LOG_ERROR( "%#: createCommandList returned null", pCallerName );
            return false;
        }

        return true;
    }

    // ---------------------------------------------------------------------------
    // 공통 헬퍼: graph 실행 및 commandList 제출
    // ---------------------------------------------------------------------------

    bool FrameRenderer::isDrawMergeEnabled() const
    {
        return ( ( _drawMergeOverride >= 0 ) ? _drawMergeOverride : gv_drawMerge ) != 0;
    }

    bool FrameRenderer::submitGraph( IRHIDevice* pDevice )
    {
        _pCmd->beginCommandList();
        _bGpuCullingActive = SW_FALSE;
        _indirectDrawCallCount.store( 0, std::memory_order_relaxed );
        _animTimer.updateTimer();

        // GPU 드리븐 프리패스 — **애니메이션이 먼저고 컬링이 나중이다.** 순서가 뒤집히면 컬링이
        // 이번 프레임에 회전하기 전의 바운드로 판정한다.
        const uint32 animInstanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
        dispatchInstanceAnimation( animInstanceCount );
        dispatchMeshMorph();
        dispatchCullAndSort( animInstanceCount );

        // 병렬 기록 가능(백엔드 capability + TaskManager + 웨이브가 나올 만큼 컴파일된 그래프)이면
        // 컬링 디스패치(위에서 _pCmd에 이미 기록됨)를 먼저 닫아 GPU 큐에 제출해서, 각 패스의 독립
        // 커맨드리스트보다 인다이렉트 인자 준비가 GPU 타임라인상 먼저 끝나도록 순서를 보장한다
        // (같은 큐에 대한 ExecuteCommandLists 호출 순서 = 실행 순서). 첫 프레임처럼 그래프가 아직
        // 컴파일 안 됐으면(getExecutionOrder()가 비어 있으면) 안전하게 기존 직렬 경로로 폴백한다 —
        // executeParallel 안에서 compile()이 그때 한 번 일어난다.
        const bool bCanRunParallel = _pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != SW_FALSE &&
                                     _graph.getExecutionOrder().size() > 1;
        if ( bCanRunParallel )
        {
            _pCmd->endCommandList();
            pDevice->executeCommandList( _pCmd );
            _pCmd = nullptr;
            return _graph.executeParallel( _graphContext, _pTaskManager, pDevice );
        }

        // 직렬 경로도 그래프가 추론한 배리어를 쓴다. 패스가 기록하는 리스트와 **같은 것**을 넘긴다 —
        // 병렬 경로처럼 프레임 스트림으로 앞당길 수 없다(직렬은 순서가 곧 리스트 안의 위치다).
        const bool bOk = _graph.execute( _graphContext, _pCmd );
        _pCmd->endCommandList();
        pDevice->executeCommandList( _pCmd );
        // _frameCmd 는 다음 프레임 prepareCommandList 에서 재사용. _pCmd 만 비움.
        _pCmd = nullptr;
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
        // 씬 직접 경로(에디터·테스트)도 패킷 경로와 **같은 라이트 버퍼**를 쓴다 — 경로마다 조명이
        // 다르면 에디터에서 본 그림과 게임 화면이 갈린다.
        collectSceneLights( pScene, _listScratchLight );
        _lightBuffer.update( pDevice, _listScratchLight );
        ensurePassResources();
        ensureTransientResources();
        resetPassCbRing();
        setIdentityWorld( _frameCtx );

        updatePassConstants( _frameCtx );
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        float3 cameraPos{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] };
        if ( pScene != nullptr )
        {
            pScene->ensureDefaultCameras();
            CameraComponent* pCam = pScene->getActiveGameCamera();
            if ( pCam != nullptr )
                cameraPos = pCam->getCameraPosition();
        }
        view( RenderViewType::Main )._position = cameraPos; // 정렬 키(카메라까지의 거리)와 컬링이 같은 값을 본다
        // 패킷 경로와 **같은 길**이다 — 빌더가 스냅샷을 만들고 RT 쪽이 받는다. 렌더 스레드 쪽 GpuScene 에는 씬을 읽는 메서드가 없다.
        _sceneBuilder.buildFromScene( pScene, cameraPos );
        {
            GpuSceneSnapshot snapshot{};
            _sceneBuilder.exportCpuSnapshot( snapshot );
            _gpuScene.adoptCpuSnapshot( std::move( snapshot ) );
        }
        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다 — 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 됐는지는 upload 뒤에 areIndirectCountsGpuFilled() 가 답한다.
        _gpuScene.setIndirectCountsFilledByGpu( wantsGpuGeneratedCommands() );
        // 모프 풀 오프셋은 배치 표에 실려 업로드 시점에 완성돼야 한다.
        prepareMeshMorphPool();
        _gpuScene.setVertexPoolEnabled( ( ( _vertexPoolOverride >= 0 ) ? _vertexPoolOverride : gv_vertexPool ) != 0 );
        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == SW_FALSE )
            bindPassCallbacks();

        // 상수버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다.
        ensurePassCbCapacityForFrame();

        // 머티리얼 퍼뮤테이션 PSO 도 같은 이유로 여기서 만든다 — 기록 중에는 만들 수 없고, 패스들은 병렬로 기록된다.
        ensureMaterialPsos();

        if ( prepareCommandList( pDevice, "execute" ) == false )
        {
            _pScene = nullptr;
            return false;
        }

        const bool bOk             = submitGraph( pDevice );
        _pScene                    = nullptr;
        _lastIndirectDrawCallCount = _indirectDrawCallCount.load( std::memory_order_relaxed );
        reportGpuPassTimes( pDevice );
        return bOk;
    }

    bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        if ( isReady() == false || pDevice == nullptr || packet._bValid == SW_FALSE )
            return false;

        _pDevice            = pDevice;
        _pScene             = nullptr;
        _outputRenderTarget = packet._gameRenderTarget;
        // _gpuScene는 FrameRenderer가 프레임 간 영속 소유(GPU 버퍼/핸들/MaterialRetireQueue 보존) —
        // 패킷에서는 CPU 스냅샷(인스턴스/배치 목록)만 옮겨온다. 통째로 move하면 직전 프레임에 업로드한
        // GPU 버퍼/디스크립터를 releaseGpu() 없이 잃어버려 매 프레임 새로 생성하는 리크가 됐었다.
        _gpuScene.adoptCpuSnapshot( std::move( packet._gpuScene ) );

        // 주광은 씬이 아니라 패킷으로 온다 — 렌더 스레드는 씬을 볼 수 없다(_pScene = nullptr).
        if ( packet._bHasLight != SW_FALSE )
        {
            _frameLight._dirIntensity       = packet._lightDirIntensity;
            _frameLight._colorAmbient       = packet._lightColorAmbient;
            _frameLight._shadowViewProj     = packet._lightViewProj;
            _frameLight._bHasShadowViewProj = SW_TRUE;
        }
        else
        {
            _frameLight = FrameLightState{};
        }
        // 씬의 모든 라이트 — 방향광·점광이 한 버퍼에 섞여 올라가고 포워드·디퍼드가 같이 읽는다.
        // 비어 있으면 셰이더가 위의 키라이트로 폴백하므로 라이트 컴포넌트가 없는 씬도 그대로 그려진다.
        _lightBuffer.update( pDevice, packet._listLight );

        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        setIdentityWorld( _frameCtx );
        // 예전엔 여기서 시드를 따로 채웠고, updatePassConstants 와 겹치면서도 라이트/블룸/아웃라인
        // 색 상수는 빠져 있었다(그건 드로우 경로가 updatePassConstants 를 다시 부르며 가려주고
        // 있었다). 같은 함수를 쓰고, 패킷이 자기 뷰 행렬을 갖고 있을 때만 그 위에 덮어쓴다.
        // _pScene 이 null 이라 updatePassConstants 는 폴백 뷰를 세운다.
        updatePassConstants( _frameCtx );
        if ( packet._bHasViewProj != SW_FALSE )
            applyViewProjection( _frameCtx, packet._viewProj ); // 역행렬·절두체도 함께 갱신된다
        // 값 업로드/바인딩은 드로우 직전 ShaderBindingBinder 가 한다 — 여기서는 시드만 채운다.
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다 — 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 됐는지는 upload 뒤에 areIndirectCountsGpuFilled() 가 답한다.
        _gpuScene.setIndirectCountsFilledByGpu( wantsGpuGeneratedCommands() );
        // 모프 풀 오프셋은 배치 표에 실려 업로드 시점에 완성돼야 한다.
        prepareMeshMorphPool();
        _gpuScene.setVertexPoolEnabled( ( ( _vertexPoolOverride >= 0 ) ? _vertexPoolOverride : gv_vertexPool ) != 0 );
        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == SW_FALSE )
            bindPassCallbacks();

        // 상수버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다.
        ensurePassCbCapacityForFrame();

        // 머티리얼 퍼뮤테이션 PSO 도 같은 이유로 여기서 만든다 — 기록 중에는 만들 수 없고, 패스들은 병렬로 기록된다.
        ensureMaterialPsos();

        if ( prepareCommandList( pDevice, "executePacket" ) == false )
            return false;

        const bool bOk             = submitGraph( pDevice );
        _lastIndirectDrawCallCount = _indirectDrawCallCount.load( std::memory_order_relaxed );
        reportGpuPassTimes( pDevice );
        return bOk;
    }
} // namespace sw
