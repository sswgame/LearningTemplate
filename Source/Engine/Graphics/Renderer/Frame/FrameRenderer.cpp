#include "pch.h"

#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/RenderFramePacket.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassManager.h"
#include "Engine/Object/Component/CameraComponent.h"

namespace sw
{
    extern int32 gv_gpuCulling;

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
        , _instanceAnimCb{ 0 }
        , _instanceAnimCbIndex{ kInvalidDescriptorIndex }
        , _instanceSortCb{ 0 }
        , _instanceSortCbIndex{ kInvalidDescriptorIndex }
        , _bGpuCullingActive{ 0 }
        , _mapMaterialFallback{}
        , _mapEnginePso{}
        , _mapPresentPso{}
        , _transientWidth{ 0 }
        , _transientHeight{ 0 }
        , _outputRenderTarget{ 0 }
        , _taaHistory{ 0 }
        , _taaHistorySrv{ kInvalidDescriptorIndex }
        , _status{ FrameRendererStatus::Uninitialized }
        , _statusMessage{}
        , _bCallbacksBound{ SW_FALSE }
        , _bPassResourcesReady{ SW_FALSE }
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

        // 동기 경로(execute)는 이 GpuScene 이 직접 배치를 만든다 — 패킷 경로의 GT GpuScene 은 EngineLoop 가 같은 값을 준다.
        _gpuScene.setMergeBatchesAcrossMaterials( pDevice->supportsNativeBindlessSampling() );

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

    bool FrameRenderer::wantsGpuGeneratedCommands() const
    {
        // 컴퓨트가 드로우 커맨드를 만드는 경로를 쓰려면 인다이렉트 드로우와 컬링 디스패치가 둘 다 켜져
        // 있고 컬링 PSO 가 실제로 만들어져 있어야 한다. 셋 중 하나라도 없으면 CPU 가 채운 개수로 그린다.
        return gv_gpuCulling != 0 && getEnginePso( RenderPassType::GpuCull ) != 0;
    }

    void FrameRenderer::dispatchInstanceAnimation( uint32 instanceCount )
    {
        // 컴퓨트 프리패스 둘 — 인스턴스 애니메이션이 먼저고 컬링이 나중이다. 순서가 뒤집히면 컬링이
        // **이번 프레임에 회전하기 전의** 바운드로 판정한다. 애니메이션이 회전만 바꾸므로 지금 씬에서는
        // 결과가 같지만, 이동을 넣는 순간 한 프레임 늦은 컬링이 된다.
        // 회전을 요청한 인스턴스가 하나도 없으면 디스패치 자체를 건너뛴다 — 안 그러면 회전을 안 쓰는 씬도
        // 매 프레임 인스턴스당 96 바이트를 읽고 아무 일도 하지 않는다. 컬링 능력과는 무관하다(DX11 도 돈다).
        if ( _gpuScene.isUploaded() && instanceCount > 0 && _gpuScene.getSpinInstanceCount() > 0 )
        {
            const RHIPipelineStateHandle animPso = getEnginePso( RenderPassType::InstanceAnim );
            if ( animPso != 0 && _instanceAnimCb != 0 && _instanceAnimCbIndex != kInvalidDescriptorIndex &&
                 _gpuScene.getInstanceUav() != kInvalidDescriptorIndex )
            {
                struct GpuAnimParams
                {
                    float32 _time{ 0.0f };
                    float32 _baseSpeed{ 0.0f };
                    float32 _speedRange{ 0.0f };
                    uint32  _instanceCount{ 0 };
                } animParams{};
                animParams._time = _animTimer.getTotalTime();
                // 기준 각속도와 편차 폭(라디안/초). 편차가 기준보다 커야 "다 같은 속도"로 보이지 않는다.
                animParams._baseSpeed     = FrameRendererUtil::kGpuSpinBaseSpeed;
                animParams._speedRange    = FrameRendererUtil::kGpuSpinSpeedRange;
                animParams._instanceCount = instanceCount;
                _pDevice->getResource()->updateConstantBuffer( _instanceAnimCb, &animParams, sizeof( animParams ) );

                // **쓰기 전에** UAV 상태로 옮긴다. 인스턴스 버퍼는 직전 프레임에 정점 셰이더가 읽던
                // (그리고 방금 CPU 업로드가 쓴) 상태라, 이 전이 없이 UAV 로 쓰면 DX12/Vulkan 에서 쓰기가
                // 유효하지 않다 — 화면은 조용히 예전 값 그대로다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::UnorderedAccess );
                _pCmd->setComputePipelineState( animPso );
                // AnimParams(b0) / g_InstancesRW(u0) — instanceanim.hlsl 레지스터와 1:1 대응.
                _pCmd->bindComputeConstantBuffer( _instanceAnimCbIndex, 0 );
                _pCmd->bindComputeUAV( _gpuScene.getInstanceUav(), 0 );
                const uint32 animGroups = ( instanceCount + 63u ) / 64u;
                if ( animGroups > 0 )
                    _pCmd->dispatchCompute( animGroups, 1, 1 );
                // 다음 디스패치(컬링)와 정점 셰이더가 이 결과를 읽는다 — 쓰기가 끝났음을 알린다.
                _pCmd->transitionBuffer( _gpuScene.getInstanceBuffer(), RHIBufferState::ShaderResource );
            }
        }
    }

    void FrameRenderer::dispatchCullAndSort( uint32 instanceCount )
    {
        // 컬링은 GpuScene 이 "개수를 컴퓨트에 맡겼다"고 답할 때만 돈다. 그래야 인자의 초기 개수(0)와
        // 디스패치 여부가 절대 어긋나지 않는다 — 어긋나면 한쪽은 빈 화면, 다른 쪽은 낡은 목록이다.
        //
        // **뷰마다 한 번씩** 돈다. 컬링 결과는 절두체에 종속이라, 메인 카메라로 거른 목록을 그림자 패스가
        // 쓰면 화면 밖에서 화면 안으로 그림자를 드리우는 물체가 사라진다. 언리얼이 뷰마다
        // FInstanceCullingContext 를 두는 것과 같은 이유다.
        if ( _gpuScene.isUploaded() && _gpuScene.areIndirectCountsGpuFilled() )
        {
            const RHIPipelineStateHandle cullPso = getEnginePso( RenderPassType::GpuCull );
            if ( cullPso != 0 && _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex &&
                 _gpuScene.getBatchInfoSrv() != kInvalidDescriptorIndex )
            {
                bool bAllViewsCulled = true;
                for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( GpuCullView::Count ); ++viewIndex )
                {
                    const GpuCullViewResources& view        = _gpuScene.getCullView( static_cast<GpuCullView>( viewIndex ) );
                    const RHIBufferHandle       cullCb      = _arrGpuCullCb[viewIndex];
                    const RHIDescriptorIndex    cullCbIndex = _arrGpuCullCbIndex[viewIndex];
                    if ( view._indirectArgs._uav == kInvalidDescriptorIndex || view._visibleInstances._uav == kInvalidDescriptorIndex ||
                         cullCb == 0 || cullCbIndex == kInvalidDescriptorIndex )
                    {
                        bAllViewsCulled = false;
                        continue;
                    }

                    struct GpuCullParams
                    {
                        float32 _planes[6][4]{};
                        uint32  _instanceCount{ 0 };
                        uint32  _batchCount{ 0 };
                        uint32  _pad[2]{};
                    } cullParams{};
                    // 절두체 평면은 그 뷰의 viewProj 에서 뽑는다. 예전엔 이 배열이 **0 인 채로 나갔고**,
                    // 그러면 셰이더의 `dot(0, center) + 0 < -radius` 가 항상 거짓이라 컬링이 아무것도
                    // 거르지 않았다(켜 놓고도 한 번도 걸러진 적이 없었다).
                    const float4x4& cullViewProj =
                        ( static_cast<GpuCullView>( viewIndex ) == GpuCullView::Shadow ) ? _cullShadowViewProj : _cullMainViewProj;
                    FrameRendererUtil::extractFrustumPlanes( cullViewProj, cullParams._planes );
                    cullParams._instanceCount = instanceCount;
                    cullParams._batchCount    = _gpuScene.getIndirectCommandCount();
                    _pDevice->getResource()->updateConstantBuffer( cullCb, &cullParams, sizeof( cullParams ) );

                    // 컬링이 쓰는 두 버퍼는 UAV 상태여야 한다. 간접 인자는 직전 프레임에 IndirectArgument 로,
                    // 가시 목록은 ShaderResource 로 두고 끝냈다.
                    _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::UnorderedAccess );
                    _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::UnorderedAccess );
                    // PSO 와 바인딩은 **루프 안에서** 다시 건다. 아래 정렬 패스가 둘 다 갈아 끼우므로
                    // 다음 뷰가 정렬 PSO 로 컬링을 돌면 안 된다.
                    _pCmd->setComputePipelineState( cullPso );
                    // CullParams(b0) / g_Instances(t0) / g_BatchInfo(t1) / g_IndirectArgs(u0) / g_VisibleInstanceIds(u1)
                    // — gpucull.hlsl 레지스터와 1:1 대응. 인스턴스·배치 구간은 뷰가 공유한다(절두체만 다르다).
                    _pCmd->bindComputeConstantBuffer( cullCbIndex, 0 );
                    _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
                    _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
                    _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
                    _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
                    // **인스턴스마다** 스레드 하나다(예전엔 배치마다 하나였다) — 그래야 보이는 것을 골라 압축할 수 있다.
                    const uint32 groups = ( cullParams._instanceCount + 63u ) / 64u;
                    if ( groups > 0 )
                        _pCmd->dispatchCompute( groups, 1, 1 );
                    // 압축이 끝났으면 투명 배치의 순서를 깊이순으로 되돌린다. 원자 연산이 준 자리 번호는
                    // 완료 순서라 그대로 두면 블렌딩이 틀린다 — 예전엔 그래서 투명이 압축을 통째로
                    // 포기했다. 정렬을 GPU 로 옮기면 투명도 컬링 이득을 받는다.
                    // 컬링이 채운 목록을 정렬이 바로 읽는다 — 상태가 그대로라 transitionBuffer 는
                    // 아무것도 하지 않으므로 **UAV 배리어**를 따로 걸어야 한다. 없으면 정렬이 아직
                    // 안 채워진 목록을 읽는다(백엔드마다 결과가 달라 재현이 어렵다).
                    _pCmd->uavBarrier( view._indirectArgs._buffer );
                    _pCmd->uavBarrier( view._visibleInstances._buffer );

                    const RHIPipelineStateHandle sortPso = getEnginePso( RenderPassType::InstanceSort );
                    if ( sortPso != 0 && _instanceSortCb != 0 && _instanceSortCbIndex != kInvalidDescriptorIndex &&
                         cullParams._batchCount > 0 )
                    {
                        struct GpuSortParams
                        {
                            float32 _cameraPos[4]{};
                            uint32  _instanceCount{ 0 };
                            uint32  _batchCount{ 0 };
                            uint32  _pad[2]{};
                        } sortParams{};
                        sortParams._cameraPos[0]  = _cullCameraPos._x;
                        sortParams._cameraPos[1]  = _cullCameraPos._y;
                        sortParams._cameraPos[2]  = _cullCameraPos._z;
                        sortParams._instanceCount = instanceCount;
                        sortParams._batchCount    = cullParams._batchCount;
                        // 값이 뷰마다 같으므로 버퍼 하나로 충분하다 — 다르게 만들 일이 생기면 컬링 CB 처럼
                        // 뷰마다 하나로 나눠야 한다(하나를 나눠 쓰면 뒤 업로드가 앞 디스패치를 덮어쓴다).
                        _pDevice->getResource()->updateConstantBuffer( _instanceSortCb, &sortParams, sizeof( sortParams ) );

                        _pCmd->setComputePipelineState( sortPso );
                        // 바인딩 자리는 컬링과 같다 — 인자·가시 목록을 그대로 읽고 쓴다.
                        _pCmd->bindComputeConstantBuffer( _instanceSortCbIndex, 0 );
                        _pCmd->bindComputeShaderResource( _gpuScene.getInstanceSrv(), 0 );
                        _pCmd->bindComputeShaderResource( _gpuScene.getBatchInfoSrv(), 1 );
                        _pCmd->bindComputeUAV( view._indirectArgs._uav, 0 );
                        _pCmd->bindComputeUAV( view._visibleInstances._uav, 1 );
                        // 배치마다 워크그룹 하나 — 그 배치의 목록을 그룹공유 안에서 정렬한다.
                        _pCmd->dispatchCompute( cullParams._batchCount, 1, 1 );
                    }

                    _pCmd->transitionBuffer( view._indirectArgs._buffer, RHIBufferState::IndirectArgument );
                    _pCmd->transitionBuffer( view._visibleInstances._buffer, RHIBufferState::ShaderResource );
                }
                _bGpuCullingActive = bAllViewsCulled ? 1u : 0u;
            }
        }
    }

    bool FrameRenderer::submitGraph( IRHIDevice* pDevice )
    {
        _pCmd->beginCommandList();
        _bGpuCullingActive = 0;
        _animTimer.updateTimer();

        // GPU 드리븐 프리패스 — **애니메이션이 먼저고 컬링이 나중이다.** 순서가 뒤집히면 컬링이
        // 이번 프레임에 회전하기 전의 바운드로 판정한다.
        const uint32 animInstanceCount = static_cast<uint32>( _gpuScene.getInstances().size() );
        dispatchInstanceAnimation( animInstanceCount );
        dispatchCullAndSort( animInstanceCount );

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
        _cullCameraPos = cameraPos; // 정렬 키(카메라까지의 거리)와 컬링이 같은 값을 본다
        _gpuScene.buildFromScene( pScene, cameraPos, _pTaskManager );
        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다 — 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 됐는지는 upload 뒤에 areIndirectCountsGpuFilled() 가 답한다.
        _gpuScene.setIndirectCountsFilledByGpu( wantsGpuGeneratedCommands() );
        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == 0 )
            bindPassCallbacks();

        // 상수버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다
        // (기록 중에는 버퍼 생성·bindless 등록을 할 수 없다).
        {
            const uint32 batchCount = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() + _gpuScene.getTransparentBatches().size() );
            const uint32 estimate   = batchCount * _s_kDrawCbPassEstimate + _s_kPassCbSlotCount;
            ensurePassCbCapacity( MathUtil::max( estimate, _passCbHighWater.load( std::memory_order_relaxed ) + _s_kPassCbSlotCount ) );
        }

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

        // 주광은 씬이 아니라 패킷으로 온다 — 렌더 스레드는 씬을 볼 수 없다(_pScene = nullptr).
        if ( packet._bHasLight != 0 )
        {
            _frameLight._dirIntensity       = packet._lightDirIntensity;
            _frameLight._colorAmbient       = packet._lightColorAmbient;
            _frameLight._shadowViewProj     = packet._lightViewProj;
            _frameLight._bHasShadowViewProj = 1;
        }
        else
        {
            _frameLight = FrameLightState{};
        }

        ensurePassResources();
        ensureTransientResources( packet._viewportWidth, packet._viewportHeight );
        resetPassCbRing();
        setIdentityWorld( _frameCtx );
        // 예전엔 여기서 시드를 따로 채웠고, updatePassConstants 와 겹치면서도 라이트/블룸/아웃라인
        // 색 상수는 빠져 있었다(그건 드로우 경로가 updatePassConstants 를 다시 부르며 가려주고
        // 있었다). 같은 함수를 쓰고, 패킷이 자기 뷰 행렬을 갖고 있을 때만 그 위에 덮어쓴다.
        // _pScene 이 null 이라 updatePassConstants 는 폴백 뷰를 세운다.
        updatePassConstants( _frameCtx );
        if ( packet._bHasViewProj != 0 )
        {
            _frameCtx._passValues.setMatrix( passConstantNames()._viewProj, packet._viewProj );
            _cullMainViewProj = packet._viewProj; // 컬링 절두체도 패킷의 뷰를 따라가야 한다
        }
        // 값 업로드/바인딩은 드로우 직전 ShaderBindingBinder 가 한다 — 여기서는 시드만 채운다.
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );

        // 컬링 컴퓨트가 개수를 만들지 **업로드 전에** 알려야 한다 — 간접 인자의 초기값이 달라지기 때문이다.
        // 실제로 그렇게 됐는지는 upload 뒤에 areIndirectCountsGpuFilled() 가 답한다.
        _gpuScene.setIndirectCountsFilledByGpu( wantsGpuGeneratedCommands() );
        _gpuScene.upload( pDevice );

        if ( _bCallbacksBound == 0 )
            bindPassCallbacks();

        // 상수버퍼 슬롯은 드로우마다 하나씩 나가므로 배치 수에 맞춰 **기록 시작 전에** 늘려 둔다
        // (기록 중에는 버퍼 생성·bindless 등록을 할 수 없다).
        {
            const uint32 batchCount = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() + _gpuScene.getTransparentBatches().size() );
            const uint32 estimate   = batchCount * _s_kDrawCbPassEstimate + _s_kPassCbSlotCount;
            ensurePassCbCapacity( MathUtil::max( estimate, _passCbHighWater.load( std::memory_order_relaxed ) + _s_kPassCbSlotCount ) );
        }

        if ( prepareCommandList( pDevice, "executePacket" ) == false )
            return false;

        const bool bOk = submitGraph( pDevice );
        _gpuScene.advanceMaterialRetireFrame();
        return bOk;
    }
} // namespace sw
