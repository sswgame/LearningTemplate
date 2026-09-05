#include "pch.h"

#include "Engine/Graphics/Renderer/FrameRenderer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/RenderPassManager.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    FrameRenderer::FrameRenderer()
        : _pDevice{ nullptr }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{ nullptr }
        , _pCmd{ nullptr }
        , _pScene{ nullptr }
        , _pTaskManager{ nullptr }
        , _gpuScene{}
        , _pipelineResource{}
        , _graph{}
        , _pipelinePath{}
        , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
        , _mapTransient{}
        , _mapTransientSrv{}
        , _listClearedThisFrame{}
        , _frameCtx{}
        , _gpuCullCb{ 0 }
        , _gpuCullCbIndex{ kInvalidDescriptorIndex }
        , _mapEnginePso{}
        , _mapMaterialPassPso{}
        , _transientWidth{ 0 }
        , _transientHeight{ 0 }
        , _outputRenderTarget{ 0 }
        , _taaHistory{ 0 }
        , _taaHistorySrv{ kInvalidDescriptorIndex }
        , _status{ FrameRendererStatus::Uninitialized }
        , _statusMessage{}
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
        , _bSceneTransformsFlushed{ SW_FALSE }
        , _bHasExecutedDepthPrepass{ SW_FALSE }
        , _bUseGpuDriven{ SW_FALSE }
        , _reservedFlags{ 0 }
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

        const EngineData&  engineData = engine::getEngineData();
        RenderPassManager& rpm        = pDevice->getRenderPassManager();
        if ( rpm.findRenderPass( hashed_string( FrameRendererUtil::kDefaultMainPassName ) ) == nullptr )
            rpm.loadRenderPass( engineData._defaultRenderPass );

        const string_view resolvedPipeline =
            pipelineXmlPath.empty() ? string_view( engineData._defaultForwardPipeline ) : pipelineXmlPath;

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

        releaseTransientResources();
        releasePassResources();
        _graph.clear();
        _frameCmd.reset();
        _pCmdOwnerDevice          = nullptr;
        _pCmd                     = nullptr;
        _frameCtx._pCmd           = nullptr;
        _pDevice                  = nullptr;
        _frameCtx._pBoundMaterial = nullptr;
        _pTaskManager             = nullptr;
        _status                   = FrameRendererStatus::Uninitialized;
        _statusMessage.clear();
        _bCallbacksBound = 0;
        _pipelinePath.clear();
        SW_LOG_INFO( "Shut down." );
    }

    bool FrameRenderer::loadPipeline( string_view pipelineXmlPath )
    {
        _pipelinePath    = pipelineXmlPath;
        _bCallbacksBound = 0;
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

    bool FrameRenderer::submitGraph( IRHIDevice* pDevice )
    {
        _pCmd->beginCommandList();

        if ( _bUseGpuDriven == SW_TRUE && _gpuScene.isUploaded() )
        {
            const RHIPipelineStateHandle cullPso = getEnginePso( "GpuCull" );
            if ( cullPso != 0 && _gpuCullCb != 0 && _gpuCullCbIndex != kInvalidDescriptorIndex &&
                 _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex &&
                 _gpuScene.getIndirectArgsUav() != kInvalidDescriptorIndex )
            {
                struct GpuCullParams
                {
                    float32 _planes[6][4]{};
                    uint32  _instanceCount{ 0 };
                    uint32  _batchCount{ 0 };
                    uint32  _pad[2]{};
                } cullParams{};
                cullParams._instanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
                cullParams._batchCount    = _gpuScene.getIndirectCommandCount();
                _pDevice->getResource()->updateConstantBuffer( _gpuCullCb, &cullParams, sizeof( cullParams ) );

                _pCmd->setComputePipelineState( cullPso );
                // CullParams(b0) / g_Instances(t0, 읽기전용) / g_IndirectArgs(u0, RW) — gpucull.hlsl 레지스터와 1:1 대응.
                _pCmd->bindComputeConstantBuffer( _gpuCullCbIndex, 0 );
                _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
                _pCmd->bindComputeUAV( _gpuScene.getIndirectArgsUav(), 0 );
                const uint32 groups = ( cullParams._batchCount + 63u ) / 64u;
                if ( groups > 0 )
                    _pCmd->dispatchCompute( groups, 1, 1 );
                _pCmd->transitionBuffer( _gpuScene.getIndirectArgsBuffer(), RHIBufferState::IndirectArgument );
            }
        }

        // 병렬 기록 가능(백엔드 capability + TaskManager + 웨이브가 나올 만큼 컴파일된 그래프)이면
        // 컬링 디스패치(위에서 _pCmd에 이미 기록됨)를 먼저 닫아 GPU 큐에 제출해서, 각 패스의 독립
        // 커맨드리스트보다 인다이렉트 인자 준비가 GPU 타임라인상 먼저 끝나도록 순서를 보장한다
        // (같은 큐에 대한 ExecuteCommandLists 호출 순서 = 실행 순서). 첫 프레임처럼 그래프가 아직
        // 컴파일 안 됐으면(getExecutionOrder()가 비어 있으면) 안전하게 기존 직렬 경로로 폴백한다 —
        // executeParallel 안에서 compile()이 그때 한 번 일어난다.
        const bool bCanRunParallel = _pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != 0 &&
                                     _graph.getExecutionOrder().size() > 1;
        if ( bCanRunParallel )
        {
            _pCmd->endCommandList();
            pDevice->executeCommandList( _pCmd );
            _pCmd = nullptr;
            return _graph.executeParallel( _graphContext, _pTaskManager, pDevice );
        }

        const bool bOk = _graph.execute( _graphContext );
        _pCmd->endCommandList();
        pDevice->executeCommandList( _pCmd );
        // _frameCmd 는 다음 프레임 prepareCommandList 에서 재사용. _pCmd 만 비움.
        _pCmd = nullptr;
        return bOk;
    }

    // ---------------------------------------------------------------------------

    bool FrameRenderer::execute( IRHIDevice* pDevice, Material* pMaterial, Scene* pScene )
    {
        if ( isReady() == false || pDevice == nullptr )
            return false;

        _pDevice                  = pDevice;
        _pScene                   = pScene;
        _frameCtx._pBoundMaterial = pMaterial;
        _outputRenderTarget       = 0;
        ensurePassResources();
        ensureTransientResources();
        resetPassCbRing();
        setIdentityWorld( _frameCtx );
        updatePassConstants( _frameCtx );
        _listClearedThisFrame.clear();
        _bSceneTransformsFlushed  = 0;
        _bHasExecutedDepthPrepass = 0;

        float3 cameraPos{ FrameRendererUtil::kDefaultCameraPos[0], FrameRendererUtil::kDefaultCameraPos[1], FrameRendererUtil::kDefaultCameraPos[2] };
        if ( pScene != nullptr )
        {
            pScene->ensureDefaultCameras();
            CameraComponent* pCam = pScene->getActiveGameCamera();
            if ( pCam != nullptr )
                cameraPos = pCam->getCameraPosition();
        }
        _gpuScene.buildFromScene( pScene, cameraPos, _pTaskManager );
        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == 0 )
            bindPassCallbacks();

        if ( prepareCommandList( pDevice, "execute" ) == false )
        {
            _pScene = nullptr;
            return false;
        }

        const bool bOk = submitGraph( pDevice );
        _gpuScene.advanceMaterialRetireFrame();
        _pScene = nullptr;
        return bOk;
    }

    bool FrameRenderer::executePacket( IRHIDevice* pDevice, RenderFramePacket& packet )
    {
        if ( isReady() == false || pDevice == nullptr || packet._bValid == 0 )
            return false;

        _pDevice                  = pDevice;
        _pScene                   = nullptr;
        _frameCtx._pBoundMaterial = packet._pSceneMaterial;
        _outputRenderTarget       = packet._gameRenderTarget;
        // _gpuScene는 FrameRenderer가 프레임 간 영속 소유(GPU 버퍼/핸들/MaterialRetireQueue 보존) —
        // 패킷에서는 CPU 스냅샷(인스턴스/배치 목록)만 옮겨온다. 통째로 move하면 직전 프레임에 업로드한
        // GPU 버퍼/디스크립터를 releaseGpu() 없이 잃어버려 매 프레임 새로 생성하는 리크가 됐었다.
        _gpuScene.adoptCpuSnapshot( std::move( packet._gpuScene ) );
        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        setIdentityWorld( _frameCtx );
        {
            float4x4 lightViewProj{};
            buildLightViewProj( _frameCtx, lightViewProj );
            _frameCtx._passValues.setMatrix( hashed_string( "g_LightViewProj" ), lightViewProj );

            float4x4 viewProj = packet._bHasViewProj != 0 ? packet._viewProj : float4x4::Identity;
            if ( packet._bHasViewProj == 0 )
                buildViewProj( viewProj );
            _frameCtx._passValues.setMatrix( hashed_string( "g_ViewProj" ), viewProj );
        }
        const float32 outlineY = _transientWidth > 0 ? ( 1.0f / static_cast<float32>( _transientWidth ) ) : 0.001f;
        const float32 outlineZ = _transientHeight > 0 ? ( 1.0f / static_cast<float32>( _transientHeight ) ) : 0.001f;
        _frameCtx._passValues.setFloat4( hashed_string( "g_OutlineParams" ), float4{ 0.02f, outlineY, outlineZ, 0.0f } );
        _frameCtx._passValues.setUint( hashed_string( "g_Flags" ),
                                       ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? 1u : 0u );
        // 값 업로드/바인딩은 드로우 직전 ShaderBindingBinder 가 한다 — 여기서는 시드만 채운다.
        // Skip updatePassConstants() — view already applied from packet.
        _listClearedThisFrame.clear();
        _bSceneTransformsFlushed  = 0;
        _bHasExecutedDepthPrepass = 0;

        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == 0 )
            bindPassCallbacks();

        if ( prepareCommandList( pDevice, "executePacket" ) == false )
            return false;

        const bool bOk = submitGraph( pDevice );
        _gpuScene.advanceMaterialRetireFrame();
        return bOk;
    }
} // namespace sw
