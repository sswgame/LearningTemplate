/**
 * @file FramePassExecutor.cpp
 * @brief 렌더 그래프 패스 실행기의 그래프 짓기와 제출입니다 — 수명, 커맨드 리스트 준비 · 제출, 뷰별 그래프(주 시점 · 추가 뷰), 뷰 패스 상수, 첨부 조회,
 *        콜백 묶기 · 트랜지언트 앨리어싱 계획, 그래프 콜백, 그래프 뒤 NaN 감시.
 * @details 한 클래스를 세 TU 로 나눈다 — 패스 종류마다의 기록은 `FramePassExecutorPasses.cpp`, 렌더 패스 열기와 씬 드로우는 `FramePassExecutorDraw.cpp`
 *          (언리얼 `DeferredShadingRenderer.cpp` 와 패스별 `*Rendering.cpp` 나눔과 같다). 프레임 패스의 실행은 `FrameGraphExecutor`, 가림 판정은 `OcclusionTiles` 가 맡는다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/frame_vector.h"
#include "Core/Container/string.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/Debug/GPUPixelInspector.h"
#include "Engine/Graphics/Debug/RenderDiagnostics.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/BindlessIndexValidator.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/RHI/RHIMeshVertexStreams.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Graphics/Texture/TextureLayerBake.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"
#include "Engine/Renderer/Frame/Screen/FrameScreenRotation.h"
#include "Engine/Renderer/Lighting/ShadowFilter.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    /**
     * @brief `-gv_drawMerge=0` 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
     * @details 묶은 이미지와 묶지 않은 이미지가 같아야 한다. 다르면 배치 테이블(g_SwBatches)이나 드로우 ID 가 틀린 것이다. 기본은 묶는 것이다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_drawMerge, true, "씬 배치 멀티 드로우 묶기 (false=배치마다 호출, 진단용)" );

    /**
     * @brief `-gv_asyncCompute=0` 이면 `_queue="AsyncCompute"` 프레임 패스도 그래픽스 큐에서 돕니다(시작 인자 — 파이프라인을 묶을 때 계획한다).
     * @details 비동기 큐가 이득인지는 측정으로 정한다(같은 장면에서 켜고 끈 GPU 프레임 시간). 능력이 없는 백엔드는 이 값과 상관없이 그래픽스 큐다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_asyncCompute, true, "프레임 패스의 비동기 컴퓨트 큐 (false=모두 그래픽스 큐, 측정용)" );

    /**
     * @brief `-gv_transientAliasing=0` 이면 뷰 그래프의 트랜지언트 첨부가 텍스처를 나누지 않습니다(시작 인자 — 콜백을 묶을 때 계획한다).
     * @details 나눈 그림과 나누지 않은 그림이 같아야 한다 — 다르면 계획이 겹치는 수명을 놓쳤다(같은 실행에서 끄고 켠 두 장을 견준다). 메모리는 `RT.Frame.transientAliases` 가 센다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_transientAliasing, true, "뷰 그래프 트랜지언트 첨부의 텍스처 나눔 (false=첨부마다 텍스처, 진단용)" );

    namespace
    {
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        /// 피사계 심도 착란원 반지름의 상한(px)입니다. depth_of_field.hlsl 의 탭 16 개가 이 반지름 안에 퍼지므로, 더 키우면 탭 사이가 벌어져 고리 무늬가 보인다.
        constexpr float32 kMaxDepthOfFieldRadiusPixels = 8.0f;
        /// 센서 높이(mm, `CameraDepthOfFieldValues::_sensorHeight`)를 m 로 바꾸는 비율입니다.
        constexpr float32 kMillimetersPerMeter = 1000.0f;
    } // namespace

    FramePassExecutor::~FramePassExecutor() = default;

    FramePassExecutor::FramePassExecutor( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameRenderState& state, FramePSOManager& psoManager,
                                          FrameAutoExposure& autoExposure, FrameUpscale& upscale, FrameFog& fog, FrameExtensionHost& extensionHost, FrameCapture& capture,
                                          const FrameScreenRotation& screenRotation )
        : _pDevice{ pDevice }
        , _pipelineResource{ pipeline }
        , _state{ state }
        , _psoManager{ psoManager }
        , _autoExposure{ autoExposure }
        , _upscale{ upscale }
        , _fog{ fog }
        , _extensionHost{ extensionHost }
        , _capture{ capture }
        , _screenRotation{ screenRotation }
        , _frameGraphExecutor{ pDevice, state, psoManager, fog }
        , _mapFramePassNameToIndex{}
        , _frameGraphContext{}
        , _pActiveView{ &state._mainView }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{}
        , _asyncComputeCmd{}
        , _pCmd{ nullptr }
        , _listPassContext{}
        , _mapPassNameToIndex{}
        , _mapExtensionPassIndex{}
        , _graphContext{}
        , _nanCheck{}
        , _nanCheckCommandList{}
        , _pLastSwapchainWriter{ nullptr }
        , _pMainOutputCanvasView{ nullptr }
        , _frameArena{}
        , _indirectDrawCallCount{ 0 }
        , _pendingPSOBatchCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _lastRenderedExtraViewCount{ 0 }
        , _bMaterialFallbackMissingLogged{ 0 }
        , _bMissingColorTargetLogged{ 0 }
        , _bHasExecutedDepthPrepass{ 0 }
    {
    }

    void FramePassExecutor::ensureResources()
    {
        _frameGraphExecutor.ensureResources();
        if ( _nanCheck.initialize( _pDevice ) == false )
            SW_FATAL( "The NaN check is on but its counters could not be created - see the log above" );
    }

    void FramePassExecutor::releaseResources()
    {
        _frameGraphExecutor.releaseResources();
        _nanCheckCommandList.reset();
        _nanCheck.shutdown();
    }

    void FramePassExecutor::clearDevice()
    {
        // 리소스 상태 추적은 이름에서 상태를 찾는 구조라 디바이스가 바뀌어도 살아남는다. 새 디바이스의 텍스처는 다시 Undefined 다.
        // 지우지 않으면 첫 프레임이 "이미 렌더 타깃 상태" 로 믿고 배리어를 건너뛴다.
        _graphContext.reset();
        _frameGraphContext.reset();
        _frameCmd.reset();
        _asyncComputeCmd.reset();
        _pCmdOwnerDevice = nullptr;
        _pCmd            = nullptr;
    }

    void FramePassExecutor::resetViewRecordState()
    {
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );
    }

    bool FramePassExecutor::prepareCommandList( IRHIDevice* pDevice, [[maybe_unused]] const utf8* pCallerName )
    {
        if ( _frameCmd && _pCmdOwnerDevice != pDevice )
        {
            // 리스트는 만든 디바이스의 것이다. 뷰마다의 리스트도 함께 버린다.
            _frameCmd.reset();
            _asyncComputeCmd.reset();
            _state._mainView._commandList.reset();
            for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
            {
                pView->_commandList.reset();
            }
        }

        if ( _frameCmd == nullptr )
            _frameCmd = pDevice->createCommandList();

        _pCmdOwnerDevice = pDevice;
        _pCmd            = _frameCmd.get();
        // 패스 컨텍스트 시드도 같은 리스트를 가리키게 한다(직렬 경로가 이것을 쓴다).
        _state._frameContext._pCmd = _pCmd;

        if ( _pCmd == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.CreateCommandListReturnedNull", "%#: createCommandList returned null", pCallerName );
            return false;
        }

        return true;
    }

    bool FramePassExecutor::isDrawMergeEnabled() const
    {
        return gv_drawMerge;
    }

    bool FramePassExecutor::submitGraph( IRHIDevice* pDevice )
    {
        // 패스 컨텍스트 슬롯은 병렬 기록 **전에** 맞춘다. 기록 중에 늘리면 워커끼리 경합한다.
        const size_t passCount = _pipelineResource.getGraphPass().size() + 1 + _extensionHost.getPasses().size(); // XML 패스 + 공용 슬롯 + 모듈 확장 패스
        if ( _listPassContext.size() < passCount )
            _listPassContext.resize( passCount );
        _frameArena.reset(); // 앞 프레임의 frame_vector 는 모두 사라졌다(기록 지역 목록만 잡는다)

        _pCmd->beginCommandList();
        _indirectDrawCallCount.store( 0, std::memory_order_relaxed );
        _pendingPSOBatchCount.store( 0, std::memory_order_relaxed );
        _state._deform._animationTimer.updateTimer();

#if SW_PROFILE_COMPILED
        // 프레임의 첫 GPU 명령 위치다. 패스 시간 합과 이 값의 차이가 "패스 밖의 GPU 시간"(컴퓨트
        // 프리패스, 업로드, 배리어)이다. 게이트는 패스 타임스탬프와 같다(`onGraphPassExecute`).
        const IRHITimestampProvider* pTimestamps   = pDevice->findTimestampProvider();
        const bool                   bWriteGPUTime = pTimestamps != nullptr && _state._gpuPassTimeline.isRecording() &&
                                   FrameRendererUtil::kGPUTimestampSlotFrameBegin < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
        {
            _pCmd->writeTimestamp( FrameRendererUtil::kGPUTimestampSlotFrameBegin );
            _pCmd->writeTimestamp( FrameRendererUtil::kGPUTimestampSlotComputeBegin );
        }
#endif

        // 프레임 그래프(파이프라인 XML 의 `_listFramePass`) — 깊이 히스토리 · 입자 · 변형 · 컬링 · 라이트 그리드 등, 이번 프레임에 그리는 **모든 뷰**의 몫이다.
        // 순서는 XML 의 자원 읽기 · 쓰기가 정한다. 앞 프레임들의 셰이더 assert · NaN 기록이 도착했으면 로그로 낸다(몇 프레임 늦다).
        _nanCheck.poll();
        recordFrameGraph( pDevice );
        // 월드 공간 UI 의 렌더 텍스처다. 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트(같은 큐)로 그린다.
        _state._canvasRenderer.drawTargets( *_pCmd, *pDevice, _state._canvasFrame, _psoManager.getCache() );
        // 2D 빛 쿠키 아틀라스 — 새로 칸을 받은 쿠키만 그 칸에 그린다. 빛 받는 스프라이트 패스가 읽기 전에 같은 프리패스 리스트에서 끝난다.
        drawLight2DCookieAtlas();
        // UV 공간 층 굽기(피부 화장 · 문신 · 번짐) — 구운 텍스처를 머티리얼이 읽기 전에 같은 프리패스 리스트에서 끝난다.
        drawTextureLayerBakes();

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
        _state._mainSeedScratch   = _state._frameContext;
        uint32 renderedExtraCount = renderExtraViewsOfKind( pDevice, RenderViewOutputKind::RenderTexture );
        // 주 시점의 시드와 클리어 기록으로 돌아온다(추가 뷰가 자기 값을 덮어썼다).
        _state._frameContext = _state._mainSeedScratch;
        _bHasExecutedDepthPrepass.store( 0 );

        // 주 출력의 캔버스는 주 출력에 마지막으로 그리는 뷰가 그린다. 화면 사각형 뷰는 주 시점 뒤에 겹치므로, 주 시점이 그리면 그 뷰가 UI 를 덮는다.
        _pMainOutputCanvasView = &_state._mainView;
        for ( const unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_TRUE && pView->_outputKind == RenderViewOutputKind::ScreenRect )
                _pMainOutputCanvasView = pView.get();
        }

        const bool bOk = executeMainGraph( pDevice );
        // 그래프가 끝난 주 시점의 패스 출력을 NaN · Inf 감시 컴퓨트로 훑는다(진단 정책이 NaN 감시를 켰을 때만).
        scanPassOutputsForNan( pDevice );

        // 화면 사각형 뷰(분할 화면, PiP)는 주 시점 위에 겹친다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::ScreenRect );
        // 호스트 타깃 뷰(에디터 씬 뷰)는 자기 RT 에 그린다. 이번 프레임에 그린 렌더 텍스처(CCTV)를 읽을 수 있도록 마지막에 그린다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::ViewportTarget );
        _state._frameContext        = _state._mainSeedScratch;
        _lastRenderedExtraViewCount = renderedExtraCount;
        // 다음 프레임의 깊이 히스토리가 이번 프레임의 장면 깊이와 짝이 되도록 뷰-투영을 적는다.
        _frameGraphExecutor.rememberDepthHistoryViews();
        return bOk;
    }

    uint32 FramePassExecutor::renderExtraViewsOfKind( IRHIDevice* pDevice, RenderViewOutputKind outputKind )
    {
        uint32 renderedCount = 0;
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            if ( pView->_bRenderThisFrame == SW_FALSE || pView->_outputKind != outputKind )
                continue;
            renderExtraView( pDevice, *pView );
            ++renderedCount;
        }
        return renderedCount;
    }

    void FramePassExecutor::prepareShadowCacheLayerResource()
    {
        // 그림자 캐시 층은 기록 **전에** 만든다 — 패스 기록(병렬 레벨) 안에서 텍스처를 만들면 자원 표를 바꿔 디바이스가 멈춘다(`requireRegistryMutableNow`).
        // 아틀라스는 그림자 패스의 깊이 첨부다(첨부 풀이 이미 잡았다). 같은 형식 · 크기면 아무것도 하지 않는다.
        ShadowCache& cache = _state._shadowCache;
        if ( cache.usesCacheLayer() == false )
            return;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type != RenderPassType::Shadow )
                continue;
            const TransientAttachmentPool::Attachment atlas = findTransientAttachment( pass._resolvedDepthAttachment );
            if ( atlas._texture != 0 )
                (void)cache.ensureLayer( _pDevice, atlas._format, atlas._width ); // 실패하면 패스가 층 없음을 보고 캐시 없이 다 그린다
            return;
        }
    }

    bool FramePassExecutor::executeMainGraph( IRHIDevice* pDevice )
    {
        prepareShadowCacheLayerResource();
        // 병렬 기록이 가능하면(백엔드 capability, TaskManager, 레벨이 나올 만큼 컴파일된 그래프) 레벨마다 여러 리스트로 기록한다.
        // 첫 프레임처럼 그래프가 아직 컴파일되지 않았으면(getExecutionOrder() 가 비어 있으면) 직렬 경로로 간다.
        const bool bCanRunParallel = _state._pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != SW_FALSE &&
                                     _state._graph.getExecutionOrder().size() > 1;
        if ( bCanRunParallel )
            return _state._graph.executeParallel( _graphContext, _state._pTaskManager, pDevice );

        // 직렬 경로도 그래프가 추론한 배리어를 쓴다. 패스가 기록하는 리스트와 **같은 것**을 넘긴다.
        if ( _state._mainView._commandList == nullptr )
            _state._mainView._commandList = pDevice->createCommandList();
        IRHICommandList* pMainCmd = _state._mainView._commandList.get();
        if ( pMainCmd == nullptr )
            return false;
        // 직렬 경로의 패스는 시드의 리스트에 기록한다(그래프가 패스에 리스트를 주지 않는다). 그래서 시드를 이 리스트로 돌린다.
        _state._frameContext._pCmd = pMainCmd;
        pMainCmd->beginCommandList();
        const bool bOk = _state._graph.execute( _graphContext, pMainCmd );
        pMainCmd->endCommandList();
        pDevice->executeCommandList( pMainCmd );
        return bOk;
    }

    void FramePassExecutor::renderExtraView( IRHIDevice* pDevice, FrameViewTarget& view )
    {
        if ( view._commandList == nullptr )
            view._commandList = pDevice->createCommandList();
        IRHICommandList* pCmd = view._commandList.get();
        if ( pCmd == nullptr )
            return;

        // 주 시점의 시드에서 출발해 이 뷰의 것(뷰-투영 · 풀 크기 · 플래그 · 컬링 칸 · 방향광 그림자)만 덮어쓴다. 라이트 목록과 점광 · 스폿 그림자는 프레임 공통이다.
        _pActiveView         = &view;
        _state._frameContext = _state._mainSeedScratch;
        applyViewProjection( _state._frameContext, view, view._cullInput._viewProj );
        applyViewPassConstants( _state._frameContext, view );
        applyViewShadowConstants( _state._frameContext, view._shadow );
        _state._frameContext._cullViewIndex = view._cullSlot;
        _state._frameContext._pCmd          = pCmd; // 직렬 경로의 패스는 시드의 리스트에 기록한다
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );
        prepareShadowCacheLayerResource(); // 주 시점보다 먼저 그리는 뷰도 있다 — 층은 기록 전에

        pCmd->beginCommandList();
        if ( _state._graph.execute( _graphContext, pCmd ) == false )
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.RenderGraphCouldNot", "Render graph could not run for an extra view (cycle or compile failure)" );
        // 렌더 텍스처는 이어서 주 시점의 머티리얼이, 호스트 타깃은 에디터 UI 가 읽는다 — 읽기 상태로 돌려 둔다(DX11 은 RTV 슬롯에서 뗀다).
        if ( view._outputKind == RenderViewOutputKind::RenderTexture && view._pOutputTexture != nullptr )
            pCmd->prepareTextureForShaderRead( view._pOutputTexture->getHandle() );
        else if ( view._outputKind == RenderViewOutputKind::ViewportTarget && view._viewportTarget != 0 )
            pCmd->prepareTextureForShaderRead( view._viewportTarget );
        pCmd->endCommandList();
        pDevice->executeCommandList( pCmd );

        _pActiveView = &_state._mainView;
    }

    bool FramePassExecutor::shouldDrawBatchInPass( RenderPassType passType, const GPUMeshBatch& batch, const GPUMeshBatchBinding& binding ) const
    {
        // 변형 풀이 받지 못한 메시(상한 초과 · 버퍼 실패)는 어느 패스에서도 그리지 않는다 — 바인드 · 레스트 포즈로 대신 그리지 않는다(풀이 오류를 남겼다).
        if ( binding._bDeformRefused != SW_FALSE )
            return false;
        if ( FrameRendererUtil::shouldFilterBatchesByMaterial( passType ) == false )
            return true;
        const GPUShaderPermutation* pPermutation =
            ( batch._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _state._gpuScene.findShaderPermutation( batch._shaderPermutation );
        return FrameRendererUtil::shouldDrawMaterialInPass( passType, pPermutation != nullptr ? &pPermutation->_listDefine : nullptr );
    }

    bool FramePassExecutor::shouldDrawCasterBatch( ShadowCasterFilter filter, const GPUMeshBatch& batch )
    {
        const bool bStatic = batch._bStaticShadowCaster != SW_FALSE;
        return filter == ShadowCasterFilter::All || ( filter == ShadowCasterFilter::StaticOnly ) == bStatic;
    }

    void FramePassExecutor::applyViewShadowConstants( FramePassContext& context, const ViewShadowData& shadow )
    {
        const PassConstantNames& names = passConstantNames();
        context._passValues.setMatrix( names._lightViewProj, shadow._lightViewProj );
        context._passValues.setFloat4( names._shadowParams, shadow._shadowParams );
        context._passValues.setFloat4( names._shadowCascadeLayout, shadow._cascadeLayout );
        for ( uint32 cascadeIndex = 0; cascadeIndex < shaderslot::kShadowCascadeMax; ++cascadeIndex )
        {
            context._passValues.setFloat4( names._arrShadowCascade[cascadeIndex], shadow._arrCascade[cascadeIndex] );
        }
        context._passValues.setFloat4( names._shadowFilter, ShadowFilter::makeShaderParams( ShadowFilter::getActiveMode(), shadow._directionalPenumbraScale ) );
        // 접촉 그림자 채널은 프레임 공통이다(빛 목록이 프레임 공통이다) — 추가 뷰도 같은 값이다.
        context._passValues.setFloat4( names._contactShadowLights, _state._frameShadow._contactShadowLights );
    }

    void FramePassExecutor::applyViewPassConstants( FramePassContext& context, FrameViewTarget& view )
    {
        // 화면 텍셀 크기는 지금 그리는 뷰의 풀 크기다(뷰마다 해상도가 다르다).
        const TransientAttachmentPool& pool     = view._transientPool;
        const float32                  outlineY = pool.getWidth() > 0 ? ( 1.0f / static_cast<float32>( pool.getWidth() ) ) : 0.001f;
        const float32                  outlineZ = pool.getHeight() > 0 ? ( 1.0f / static_cast<float32>( pool.getHeight() ) ) : 0.001f;
        context._passValues.setFloat4( passConstantNames()._outlineParams, float4{ 0.02f, outlineY, outlineZ, 0.0f } );
        // 원본 텍셀의 기본값은 프레임 텍셀이다. 원본 역할 입력을 바인딩하는 패스가 그 첨부의 실제 크기로 덮는다(registerPassTexture).
        context._passValues.setFloat4( passConstantNames()._sourceTexel,
                                       float4{ outlineY, outlineZ, static_cast<float32>( pool.getWidth() ), static_cast<float32>( pool.getHeight() ) } );
        // 카메라별 후처리 값(PostProcessValues)도 뷰마다다. 중립 값(노출 0, 배율 1, 비네트 · 레터박스 0)이면 셰이더 결과가 전역 값과 같다.
        const PostProcessValues& post = view._settings._postProcess;
        float4                   bloomParams{ kDefaultBloomParams };
        bloomParams._y *= MathUtil::max( 0.0f, post._bloomScale );
        context._passValues.setFloat4( passConstantNames()._bloomParams, bloomParams );
        // 노출 배율 = 보정 2^EV × (자동 노출을 재지 않는 카메라의 고정 노출 2^-EV). 측정하는 카메라는 셰이더가 이번 EV(`g_AutoExposureIndex`)를 더 곱한다.
        float32 exposureScale = MathUtil::pow( 2.0f, post._exposureCompensation );
        // 적응 시계는 패킷의 게임 시계다(멈춘 게임은 적응도 멈춘다 — 언리얼 눈 적응의 월드 델타). 벽시계면 고정 프레임 시간 실행에서 같은 프레임이 다른 노출이 된다.
        _autoExposure.prepareAutoExposure( view, _state._gpuScene.getVertexAnimationTime(), exposureScale );
        context._passValues.setFloat4( passConstantNames()._cameraGrade, float4{ exposureScale, MathUtil::max( 0.0f, post._saturation ), MathUtil::max( 0.0f, post._contrast ),
                                                                                 MathUtil::saturate( post._vignetteIntensity ) } );
        const float32 viewAspect = pool.getHeight() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) : 0.0f;
        // 색각 보정은 화면 · 호스트 출력 뷰만 건다. 렌더 텍스처는 주 화면에 그려진 뒤 그 뷰의 톤맵에서 보정된다(두 번 걸지 않는다).
        const bool    bScreenOutput   = view._pOutputTexture == nullptr;
        const float32 colorVisionMode = bScreenOutput ? static_cast<float32>( _state._canvasFrame._colorVisionMode ) : 0.0f;
        context._passValues.setFloat4( passConstantNames()._cameraFrame, float4{ MathUtil::max( 0.0f, post._letterboxAspectRatio ), viewAspect, colorVisionMode, 0.0f } );
        // 컬러 그레이딩(post_grade.hlsli) — 화이트 밸런스 · 범위 보정 · LUT. LUT 는 세기가 있는 후처리 뷰만 묻는다(안 쓰는 경로를 읽지 않는다). 띠 모양이 아니면 데이터 오류라 걸지 않는다.
        const bool       bWantsLUT = view._settings.usesPostProcess() && post._colorGradingLUTIntensity > 0.0f;
        const Texture2D* pLUT      = bWantsLUT ? _state._postProcessTextures.findReady( post._colorGradingLUT, "colour grading LUT" ) : nullptr;
        if ( pLUT != nullptr && PostGradeConstants::isLUTStrip( pLUT->getWidth(), pLUT->getHeight() ) == false )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ColorGradingLUTShape", "Colour grading LUT '%#' is %#x%# - it must be an N*N wide, N tall strip (e.g. 256x16); the LUT is left out",
                                  post._colorGradingLUT.c_str(), pLUT->getWidth(), pLUT->getHeight() );
            pLUT = nullptr;
        }
        view._pColorGradingLUT         = pLUT;
        const PostGradeConstants grade = PostGradeConstants::make( post, pLUT != nullptr ? pLUT->getHeight() : 0u );
        for ( uint32 row = 0; row < 3; ++row )
        {
            context._passValues.setFloat4( passConstantNames()._arrGradeWhiteBalance[row], grade._arrWhiteBalance[row] );
        }
        static_assert( PassConstantNames::kGradeRangeConstantCount == PostGradeConstants::kRangeCount * PostGradeConstants::kRangeFieldCount, "그레이딩 범위 상수 이름 수가 상수 배치와 다르다" );
        for ( uint32 index = 0; index < PassConstantNames::kGradeRangeConstantCount; ++index )
        {
            context._passValues.setFloat4( passConstantNames()._arrGradeRange[index], grade._arrRange[index] );
        }
        context._passValues.setFloat4( passConstantNames()._gradeParams, grade._params );
        context._passValues.setFloat4( passConstantNames()._gradeLUT, grade._lut );
        // 렌즈 · 필름(post_grade.hlsli) — 모두 0 이면 셰이더가 원본을 다시 읽지 않는다. 범위는 칸의 Min · Max 와 같게 자른다(데이터 오류가 화면 밖을 읽지 않게).
        context._passValues.setFloat4( passConstantNames()._lensParams, float4{ MathUtil::max( 0.0f, post._chromaticAberrationIntensity ), MathUtil::saturate( post._chromaticAberrationStart ),
                                                                                MathUtil::clamp( post._lensDistortion, -1.0f, 1.0f ), MathUtil::max( 0.0f, post._sharpen ) } );
        context._passValues.setFloat4( passConstantNames()._grainParams, float4{ MathUtil::saturate( post._filmGrainIntensity ), MathUtil::max( 1.0f, post._filmGrainSize ), 0.0f, 0.0f } );
        // 블룸 렌즈(post_bloom.hlsli) — 먼지 마스크는 LUT 와 같이 세기가 있는 후처리 뷰만 묻고, 읽힌 마스크가 없으면 세기를 0 으로 보낸다(셰이더가 읽지 않는다).
        const bool       bWantsDirt = view._settings.usesPostProcess() && post._bloomDirtMaskIntensity > 0.0f;
        const Texture2D* pDirt      = bWantsDirt ? _state._postProcessTextures.findReady( post._bloomDirtMask, "bloom dirt mask" ) : nullptr;
        view._pBloomDirtMask        = pDirt;
        context._passValues.setFloat4( passConstantNames()._bloomLens, float4{ pDirt != nullptr ? post._bloomDirtMaskIntensity : 0.0f, MathUtil::max( 0.0f, post._lensFlareIntensity ),
                                                                               MathUtil::saturate( post._lensFlareGhostSpacing ), MathUtil::saturate( post._lensFlareHaloWidth ) } );
        context._passValues.setFloat4( passConstantNames()._flareTint, float4{ post._lensFlareTint._x, post._lensFlareTint._y, post._lensFlareTint._z, MathUtil::max( 0.0f, post._lensFlareThreshold ) } );
        context._passValues.setFloat4( passConstantNames()._ssrParams, float4{ MathUtil::saturate( post._screenSpaceReflectionIntensity ), MathUtil::max( 0.0f, post._screenSpaceReflectionMaxDistance ),
                                                                               MathUtil::max( 0.0f, post._screenSpaceReflectionThickness ),
                                                                               MathUtil::saturate( post._screenSpaceReflectionMaxRoughness ) } );
        // 피사계 심도(depth_of_field.hlsl) — 얇은 렌즈의 착란원. 지름 / 센서 높이 = 센서 높이 · s² / ( 4 · N · 초점 거리 ) · |1 - 초점 거리 / 깊이|
        // (s = 1 / tan( fovY / 2 ) 라 초점 길이 = 센서 높이 · s / 2, 구경 = 초점 길이 / N, 초점 길이가 초점 거리보다 훨씬 짧다고 본다). y 는 그 반지름을 화면 높이 px 로 바꾼 배율이다.
        // 조리개 0(핀홀) · 직교 · 후처리를 끈 뷰는 패스를 기록하지 않는다(applyViewPassSelection). 그때는 배율을 0 으로 두고 w 도 0 이다(셰이더는 w 를 읽지 않는다).
        const CameraDepthOfFieldValues& lens          = view._settings._depthOfField;
        const float32                   scaleY        = view._settings._projectionScaleY;
        const bool                      bDepthOfField = view._settings.usesPostProcess() && lens.hasDepthOfField() && scaleY > 0.0f;
        float32                         cocScale      = 0.0f;
        if ( bDepthOfField )
        {
            const float32 sensorHeightMeters = lens._sensorHeight / kMillimetersPerMeter;
            cocScale                         = 0.5f * static_cast<float32>( pool.getHeight() ) * sensorHeightMeters * scaleY * scaleY / ( 4.0f * lens._aperture * lens._focusDistance );
        }
        context._passValues.setFloat4( passConstantNames()._cameraLens,
                                       float4{ lens._focusDistance, cocScale, kMaxDepthOfFieldRadiusPixels, bDepthOfField ? 1.0f : 0.0f } );
        // 패스 플래그: 비트는 binding_slots.hlsli 의 SW_PASS_FLAG_*(C++ 는 shaderslot::kPassFlag*)가 기준이다. 후처리는 뷰마다 끌 수 있다(CCTV).
        uint32 flags = ( _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling() ) ? shaderslot::kPassFlagNativeBindless : 0u;
        if ( view._settings.usesPostProcess() == false )
            flags |= shaderslot::kPassFlagSkipPost;
        if ( view._settings.usesTonemap() == false )
            flags |= shaderslot::kPassFlagSkipTonemap;
        if ( view._settings._upscaleZoom > 0 )
            flags |= shaderslot::kPassFlagPointUpscale;
        context._passValues.setUint( passConstantNames()._flags, flags );
    }

    void FramePassExecutor::applyViewProjection( FramePassContext& context, FrameViewTarget& view, const float4x4& viewProj )
    {
        context._passValues.setMatrix( passConstantNames()._viewProj, viewProj );
        // 역행렬은 **여기서만** 만든다. 디퍼드 조명이 깊이에서 월드 위치를 복원하는 데 쓰는데,
        // 뷰와 따로 채우면 언젠가 한쪽만 갱신되고, 그 증상은 "빛이 한 프레임 늦게 따라온다"이다.
        context._passValues.setMatrix( passConstantNames()._invViewProj, viewProj.invert() );
        // 컬링은 기록 시작 전에 도는데 그때는 상수 버퍼에서 도로 꺼낼 수 없다. 지금 그리는 뷰에 같은 값을 남긴다.
        _state.getCullInputOf( view ).setViewProjection( viewProj );

        // 시간 누적 기록을 버릴지는 셋업이 뷰마다 프레임에 한 번 정했다(`beginViewHistory` — 처음 그리는 프레임과 컷). TAA · 모션 블러 · 자동 노출 · 가림 판정이
        // 모두 `_bHistoryReset` 하나를 본다 — 모션 블러는 지난 뷰-투영을 이번 것으로 둬 속도가 0 이다.
        context._passValues.setMatrix( passConstantNames()._prevViewProj, view._bHistoryReset == SW_TRUE ? viewProj : view._prevViewProj );
        view._prevViewProj = viewProj;
    }

    void FramePassExecutor::setIdentityWorld( FramePassContext& context )
    {
        context._world = float4x4::Identity;
    }

    bool FramePassExecutor::acquirePassCb( FramePassContext& context )
    {
        // 슬롯 하나를 잡는다. 패스 진입 때 한 번 부르고, 한 패스 안에서 상수가 바뀌는 반복(그림자 타일)은 반복마다 다시 부른다.
        // 못 잡으면(링 고갈) false — 부르는 쪽은 그 패스 · 타일을 그리지 않는다. 앞 슬롯으로 그리면 상수가 섞인다(링이 이유를 남겼다).
        return _state._passCbRing.acquire( context._passCb, context._passCbIndex );
        // 값은 드로우 직전 ShaderParameterBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (context._passValues 에 이미 프레임 시드가 들어 있으므로 따로 먼저 올릴 필요가 없다).
    }

    bool FramePassExecutor::markAttachmentCleared( const hashed_string& key )
    {
        return activePool().markCleared( key );
    }

    void FramePassExecutor::resetClearedAttachments()
    {
        activePool().resetCleared();
    }

    float4 FramePassExecutor::getTargetClearColor( const hashed_string& attachmentName ) const
    {
        // 첨부의 클리어 색은 파이프라인 XML 의 선언 하나가 원본이다(풀이 할당할 때 선언 값을 든다). 패스마다 코드가 따로 적던
        // 기본값(뎁스 1, 노멀 0.5 · 0.5 · 1, SSAO 흰색)은 선언과 갈라질 수 있어 지웠다. 풀에 없는 이름은 백버퍼(핸들 0)이고,
        // 백버퍼의 클리어 색은 렌더러의 클리어 색(SceneColor 선언에서 온다) 하나다.
        const TransientAttachmentPool::Attachment attachment = activePool().find( attachmentName );
        return attachment._texture != 0 ? attachment._ops._clearColor : _state._clearColor;
    }

    TransientAttachmentPool::Attachment FramePassExecutor::findTransientAttachment( const hashed_string& name ) const
    {
        return activePool().find( name );
    }

    RHITextureHandle FramePassExecutor::findTransient( const hashed_string& name ) const
    {
        return activePool().findTexture( name );
    }

    hashed_string FramePassExecutor::resolvePresentSource() const
    {
        // Present 의 SourceColor 입력이 원본이다(계약의 필수 입력 — 로드가 선언을 요구한다). 이름 후보로 "가장 나중 컬러" 를 짐작하지 않는다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type != RenderPassType::Present )
                continue;
            for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
            {
                if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor && findTransient( input._attachment ) != 0 )
                    return input._attachment;
            }
        }
        return hashed_string{};
    }

    void FramePassExecutor::registerVisualizedAttachment( FramePassContext& context )
    {
        // 주 시점만 바꾼다 — 추가 뷰(CCTV · 렌더 텍스처)는 제 원본을 그대로 낸다. 없는 이름이면 원본을 그대로 둔다(개발 명령이 이미 이름을 알렸다).
        const GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
        string                         attachment;
        if ( isRenderingExtraView() || pInspector == nullptr || pInspector->findVisualizedAttachment( attachment ) == false )
            return;
        const hashed_string name = hashed_string::findInterned( attachment );
        if ( name.empty() || findTransient( name ) == 0 )
        {
            SW_LOG_WARNING_ONCE_ID( "FramePassExecutor.VisualizedAttachmentMissing", "gpu.show: attachment '%#' is not allocated in this view - showing the normal output", attachment.c_str() );
            return;
        }
        registerPassTexture( context, attachmentNames()._sourceColor, name );
    }

    void FramePassExecutor::scanPassOutputsForNan( IRHIDevice* pDevice )
    {
        if ( _nanCheck.isActive() == false || pDevice == nullptr )
            return;
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassType::NanCheck );
        if ( pso == 0 )
        {
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.GvNanCheckButNaN", "The NaN check is on but its PSO is missing (shader not cooked?) - no output is scanned" );
            return;
        }
        if ( _nanCheckCommandList == nullptr )
            _nanCheckCommandList = pDevice->createCommandList();
        IRHICommandList* pCmd = _nanCheckCommandList.get();
        if ( pCmd == nullptr )
            return;

        pCmd->beginCommandList();
        frame_vector<RHITextureHandle> listScanned{ FrameAllocator<RHITextureHandle>{ _frameArena } }; // 같은 텍스처를 가리키는 출력은 한 번만 훑는다
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                const TransientAttachmentPool::Attachment attachment = _state._mainView._transientPool.find( output._attachment );
                const bool                                bScannable = attachment._texture != 0 && attachment._srv != kInvalidDescriptorIndex && GPUNanCheck::isScannableFormat( attachment._format );
                if ( bScannable == false || VectorUtil::contains( listScanned, attachment._texture ) )
                    continue;
                listScanned.push_back( attachment._texture );

                utf8 arrName[constant::buffer::kMax64];
                formatstring( arrName, sizeof( arrName ), "%#:%#", pass._name, output._attachment.view() );
                // 컴퓨트가 엔진 텍스처로 읽도록 셰이더 읽기 상태로 옮기고, 다음 프레임의 패스가 쓰도록 렌더 타깃 상태로 돌려 둔다.
                pCmd->prepareTextureForShaderRead( attachment._texture );
                _nanCheck.scan( *pCmd, pso, arrName, attachment._srv, attachment._width, attachment._height );
                pCmd->prepareTextureForRenderTarget( attachment._texture );
            }
        }
        _nanCheck.requestReadback( *pCmd );
        pCmd->endCommandList();
        pDevice->executeCommandList( pCmd );
    }

    bool FramePassExecutor::bindPassCallbacks()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        _state._graph.clear();
        _mapPassNameToIndex.clear();
        // Swapchain 을 쓰는 마지막 패스다. 그래프는 같은 출력의 쓰기를 선언 순서로 연결한다(쓰기 사슬). 스크린샷 캡처를 백버퍼로 복사하는 일은 그 패스 끝에서 한다.
        _pLastSwapchainWriter = nullptr;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            if ( VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) ) )
                _pLastSwapchainWriter = &pass;
        }

        // 모듈 확장 패스(ExtensionRenderPass M3)가 끼는 자리 — 단계마다 XML 패스 목록의 정해진 첫 자리 앞이다(없으면 끝). 선언 순서가 쓰기 사슬이라 같은 자리의 모듈 패스는 선언한 순서다.
        // 모듈 패스도 같은 그래프의 노드다 — 읽기 · 쓰기 선언이 같은 위상 정렬과 배리어 추론을 탄다.
        const size_t anchorPre    = 0;
        size_t       anchorPost   = listPass.size();
        size_t       anchorOutput = listPass.size();
        for ( size_t anchorIndex = 0; anchorIndex < listPass.size(); ++anchorIndex )
        {
            const RenderPassTypeInfo& anchorInfo = getRenderPassTypeInfo( listPass[anchorIndex]._type );
            if ( anchorPost == listPass.size() && ( anchorInfo.hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess ) || anchorInfo.hasFlag( RenderPassTraitFlag::kOutputPass ) ) )
                anchorPost = anchorIndex;
            if ( anchorOutput == listPass.size() && anchorInfo.hasFlag( RenderPassTraitFlag::kOutputPass ) )
                anchorOutput = anchorIndex;
        }
        _mapExtensionPassIndex.clear();
        auto addExtensionPasses = [this]( ExtensionHookStage stage )
        {
            for ( uint32 moduleIndex = 0; moduleIndex < static_cast<uint32>( _extensionHost.getPasses().size() ); ++moduleIndex )
            {
                const ExtensionFramePass& modulePass = _extensionHost.getPasses()[moduleIndex];
                if ( modulePass._params._stage != stage )
                    continue;
                vector<hashed_string> listExtensionInput;
                vector<hashed_string> listExtensionOutput;
                for ( const ExtensionPassRead& read : modulePass._params._listRead )
                {
                    listExtensionInput.emplace_back( read._attachment.c_str() );
                }
                for ( const string& write : modulePass._params._listWrite )
                {
                    listExtensionOutput.emplace_back( write.c_str() );
                }
                // 깊이 첨부도 쓰기 선언이다(XML 패스와 같은 규칙 — 레벨 프롤로그가 첨부 전이를 낸다).
                if ( modulePass._params._depthAttachment.empty() == false )
                {
                    const hashed_string depthName( modulePass._params._depthAttachment.c_str() );
                    if ( VectorUtil::contains( listExtensionOutput, depthName ) == false )
                        listExtensionOutput.push_back( depthName );
                }
                _mapExtensionPassIndex[modulePass._graphName] = moduleIndex;
                _state._graph.addPass( modulePass._graphName, std::move( listExtensionInput ), std::move( listExtensionOutput ),
                                       SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onGraphPassExecute, this ) );
            }
        };
        auto addModulePassesAt = [&]( size_t index )
        {
            if ( index == anchorPre )
                addExtensionPasses( ExtensionHookStage::PreRenderView );
            if ( index == anchorPost )
                addExtensionPasses( ExtensionHookStage::PrePostProcess );
            if ( index == anchorOutput )
                addExtensionPasses( ExtensionHookStage::PostRenderView );
        };

        for ( uint32 index = 0; index < static_cast<uint32>( listPass.size() ); ++index )
        {
            addModulePassesAt( index );
            const RenderGraphPassDesc& pass = listPass[index];
            const hashed_string        nameHash( pass._name.c_str() );
            // 이름이 겹치면 앞의 것이 이긴다. 그래프도 뒤의 선언을 버린다(`RenderGraph::addPass`). 파이프라인 검사가 먼저 알린다.
            if ( _mapPassNameToIndex.find( nameHash ) == _mapPassNameToIndex.end() )
                _mapPassNameToIndex[nameHash] = index;

            vector<hashed_string> listInput;
            vector<hashed_string> listOutput;
            for ( const string& in : pass._listInput )
            {
                listInput.emplace_back( in.c_str() );
            }
            for ( const string& out : pass._listOutput )
            {
                listOutput.emplace_back( out.c_str() );
            }
            // 깊이 첨부도 그래프가 아는 접근이다. 깊이를 쓰지 않는 패스(투명)도 그것을 DSV 로 바인딩하므로 "첨부" 상태가 필요하다. 출력에 없으면
            // 여기서 추가한다. 그래야 레벨 프롤로그가 첨부 전이(prepareTextureForRenderTarget)를 미리 내고, 앞 패스가 SRV 로 바인딩해 둔 깊이를
            // D3D11 이 해제하며(OMSetRenderTargets 해저드), 뒤에서 깊이를 읽는 패스가 다시 읽기 전이를 받는다(언리얼 RDG 의 깊이 첨부 접근).
            // 순서는 쓰기 사슬을 따른다. 이 패스보다 **먼저 선언한** 읽는 패스는 앞 버전을 읽고 이 패스 앞에 온다.
            if ( pass._resolvedDepthAttachment.empty() == false &&
                 VectorUtil::contains( listOutput, pass._resolvedDepthAttachment ) == false )
                listOutput.push_back( pass._resolvedDepthAttachment );

            RenderGraphPassExecuteFn execute =
                SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onGraphPassExecute, this );

            _state._graph.addPass( nameHash, std::move( listInput ), std::move( listOutput ), std::move( execute ) );
        }

        addModulePassesAt( listPass.size() ); // 자리를 못 찾은 단계(출력 패스가 없는 파이프라인)는 끝에 붙는다
        _extensionHost.markBound();

        _state._graph.setLevelPrologue( SW_DELEGATE_METHOD( RenderGraphLevelPrologueFn, &FramePassExecutor::onGraphLevelPrologue, this ) );

        if ( _state._graph.compile() == false )
        {
            SW_LOG_ERROR_ID( "FramePassExecutor.CallbackBindCompileFailed", "Callback bind compile failed" );
            return false;
        }
        buildTransientAliasPlan();
        return bindFramePassCallbacks();
    }

    void FramePassExecutor::buildTransientAliasPlan()
    {
        ++_state._transientAliasGeneration; // 뷰 풀이 다음 셋업에서 새 계획으로 다시 만든다
        _state._transientAliasPlan = RenderGraphAliasPlan{};
        // NaN 감시는 그래프가 끝난 뒤 모든 패스 출력을 다시 읽는다 — 나눠 쓴 텍스처는 앞 첨부의 내용이 이미 덮였다. 감시를 켠 실행은 나누지 않는다.
        if ( gv_transientAliasing == false || GPUNanCheck::isRequested() )
            return;

        // 실행 순서 번호 → 레벨. 실행 순서는 레벨을 차례로 이은 것이다(`RenderGraph::compile`).
        const RenderGraph&                   graph = _state._graph;
        vector<uint32>                       listLevelOfOrder;
        const vector<vector<hashed_string>>& listLevel = graph.getExecutionLevels();
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( listLevel.size() ); ++levelIndex )
        {
            listLevelOfOrder.insert( listLevelOfOrder.end(), listLevel[levelIndex].size(), levelIndex );
        }

        const vector<RenderGraphPassDesc>&   listPass = _pipelineResource.getGraphPass();
        unordered_map<hashed_string, size_t> mapCandidateIndex;
        vector<RenderGraphAliasCandidate>    listCandidate;
        for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
        {
            // 나누지 않는 것: 깊이(장면 깊이는 다음 프레임의 깊이 히스토리가 읽는다 · 그림자 맵), 프레임을 넘어 내용을 잇는 첨부(`Load`), 그래프가 모르는 첨부.
            const bool bDepth = FrameRendererUtil::isDepthFormat( attachment._format );
            if ( bDepth || attachment._loadOp == RHIRenderPassLoadOp::Load )
                continue;
            const hashed_string                name( attachment._name.c_str() );
            const RenderGraphResourceLifetime* pLife = graph.findResourceLifetime( name );
            if ( pLife == nullptr || pLife->_lastPassIndex >= listLevelOfOrder.size() )
                continue;
            // 서술 키 — 같은 텍스처로 넘길 수 있는 조건(형식 · 나눗수 · RW 용도 · 클리어 값, D3D12 의 최적 클리어 값이 서술에 든다).
            // 칸마다 32 비트로 옮겨 해시한다(구조체째 해시하면 정렬 채움 바이트가 섞인다).
            uint32 arrDescWord[7]{ static_cast<uint32>( attachment._format ), attachment._resolutionDivisor,
                                   FrameRendererUtil::isComputeOutputOrInput( listPass, attachment._name ) ? 1u : 0u, 0u, 0u, 0u, 0u };
            Memory::copy( &arrDescWord[3], &attachment._clearColor._x, sizeof( float32 ) );
            Memory::copy( &arrDescWord[4], &attachment._clearColor._y, sizeof( float32 ) );
            Memory::copy( &arrDescWord[5], &attachment._clearColor._z, sizeof( float32 ) );
            Memory::copy( &arrDescWord[6], &attachment._clearColor._w, sizeof( float32 ) );
            const uint64 descKey = HashUtil::appendBytes64( HashUtil::kFNVOffset64, arrDescWord, sizeof( arrDescWord ) );
            // 힙에 겹쳐 놓으려면 프레임의 첫 쓰기가 지우기여야 한다(앞 텍스처가 남긴 바이트가 내용이 된다) — RW(컴퓨트가 처음 쓴다)는 빼고 Clear 만 받는다.
            const uint8 bPlaceable  = attachment._loadOp == RHIRenderPassLoadOp::Clear && arrDescWord[2] == 0u ? 1 : 0;
            mapCandidateIndex[name] = listCandidate.size();
            listCandidate.push_back( RenderGraphAliasCandidate{ name, descKey, listLevelOfOrder[pLife->_firstPassIndex], listLevelOfOrder[pLife->_lastPassIndex], bPlaceable } );
        }

        // 꺼진 효과는 출력 이름이 원본을 가리킨다(`kBypassesToSource`) — 그 출력을 읽는 패스가 원본을 읽으므로 원본의 수명이 출력의 끝까지 늘어난다. 둘을 한 구간으로 묶는다.
        // TAA 출력은 셋업이 기록 형식으로 읽어 프레임 밖에서도 쓴다 — 나누지 않는다.
        vector<uint8> listExcluded( listCandidate.size(), 0 );
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            const RenderPassTypeInfo& info = getRenderPassTypeInfo( pass._type );
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                const auto outputIt = mapCandidateIndex.find( output._attachment );
                if ( outputIt == mapCandidateIndex.end() )
                    continue;
                if ( pass._type == RenderPassType::TAA )
                    listExcluded[outputIt->second] = 1;
                if ( info.hasFlag( RenderPassTraitFlag::kBypassesToSource ) == false )
                    continue;
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
                {
                    const auto inputIt = mapCandidateIndex.find( input._attachment );
                    if ( static_cast<RenderPassInputRole>( input._role ) != RenderPassInputRole::SourceColor )
                        continue;
                    if ( inputIt == mapCandidateIndex.end() )
                    {
                        listExcluded[outputIt->second] = 1; // 원본이 후보가 아니면(깊이 · Load) 출력도 나누지 않는다
                        continue;
                    }
                    RenderGraphAliasCandidate& source = listCandidate[inputIt->second];
                    RenderGraphAliasCandidate& target = listCandidate[outputIt->second];
                    source._firstLevel                = MathUtil::min( source._firstLevel, target._firstLevel );
                    source._lastLevel                 = MathUtil::max( source._lastLevel, target._lastLevel );
                    target._firstLevel                = source._firstLevel;
                    target._lastLevel                 = source._lastLevel;
                }
            }
        }
        vector<RenderGraphAliasCandidate> listAliasable;
        for ( size_t candidateIndex = 0; candidateIndex < listCandidate.size(); ++candidateIndex )
        {
            if ( listExcluded[candidateIndex] == 0 )
                listAliasable.push_back( listCandidate[candidateIndex] );
        }
        _state._transientAliasPlan = RenderGraphAliasPlan::build( listAliasable );
        SW_PROFILE_COUNT( "RT.Frame.transientAliases", _state._transientAliasPlan._mapAliasToOwner.size() );
    }

    bool FramePassExecutor::bindFramePassCallbacks()
    {
        RenderGraph& frameGraph = _state._frameGraph;
        frameGraph.clear();
        _mapFramePassNameToIndex.clear();
        _frameGraphContext.reset();
        for ( const RenderGraphResourceDesc& resource : _pipelineResource.getResources() )
        {
            frameGraph.declareResource( hashed_string( resource._name.c_str() ), resource._kind );
        }
        const vector<RenderGraphPassDesc>& listFramePass = _pipelineResource.getFramePass();
        for ( uint32 index = 0; index < static_cast<uint32>( listFramePass.size() ); ++index )
        {
            const RenderGraphPassDesc& pass = listFramePass[index];
            const hashed_string        nameHash( pass._name.c_str() );
            vector<hashed_string>      listInput;
            vector<hashed_string>      listOutput;
            for ( const string& in : pass._listInput )
            {
                listInput.emplace_back( in.c_str() );
            }
            for ( const string& out : pass._listOutput )
            {
                listOutput.emplace_back( out.c_str() );
            }
            _mapFramePassNameToIndex[nameHash] = index;
            frameGraph.addPass( nameHash, std::move( listInput ), std::move( listOutput ),
                                SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onFrameGraphPassExecute, this ), pass._queue );
        }
        // 비동기 컴퓨트 큐가 없는 백엔드(DX11 · GL · WebGPU · 큐가 하나인 Vulkan 장치)는 AsyncCompute 패스를 그래픽스 큐로 계획한다(능력 표가 정한 길).
        const bool bAsyncCompute = gv_asyncCompute && _pDevice != nullptr && _pDevice->getCapabilities()._bAsyncCompute != SW_FALSE;
        frameGraph.setAsyncComputeSupported( bAsyncCompute );
        // 프레임 패스가 없는 파이프라인(미리보기 · 시험 파이프라인)은 빈 그래프다 — 컬링 · 그리드 등은 PSO 도 없다(`FramePSOManager::getEnginePSO`).
        if ( listFramePass.empty() )
            return true;
        // 배리어는 패스가 자기 버퍼에 스스로 건다 — 프레임 그래프 자원은 순서만 정하므로 레벨 프롤로그를 걸지 않는다.
        if ( frameGraph.compile() == false )
        {
            SW_LOG_ERROR_ID( "FramePassExecutor.FrameGraphCompileFailed", "Frame graph (_listFramePass) could not be compiled - see the cycle report above" );
            return false;
        }
        return true;
    }

    void FramePassExecutor::recordFrameGraph( IRHIDevice* pDevice )
    {
        _frameGraphExecutor.beginFrame();
        RenderGraph& frameGraph = _state._frameGraph;
        bool         bOk        = true;
        if ( frameGraph.getNodeCount() > 0 && frameGraph.getAsyncBatchCount() > 0 )
        {
            // 비동기 묶음이 있다 — 컴퓨트 큐 리스트를 그래프가 포크마다 열고 닫는다. 프리패스 리스트는 포크 · 조인에서 잘려 큐에 나가고 다시 열린다.
            if ( _asyncComputeCmd == nullptr )
                _asyncComputeCmd = pDevice->createAsyncComputeCommandList();
            bOk = _asyncComputeCmd != nullptr && frameGraph.executeOnQueues( _frameGraphContext, *pDevice, *_pCmd, _asyncComputeCmd.get() );
        }
        else if ( frameGraph.getNodeCount() > 0 )
        {
            bOk = frameGraph.execute( _frameGraphContext, _pCmd );
        }
        if ( bOk == false )
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.FrameGraphExecuteFailed", "Frame graph could not run - culling, light grid and skinning results are stale this frame" );
        _frameGraphExecutor.endFrame( *_pCmd );
    }

    void FramePassExecutor::onFrameGraphPassExecute( const RenderGraphPassContext& graphContext )
    {
        const vector<RenderGraphPassDesc>& listFramePass = _pipelineResource.getFramePass();
        const auto                         iter          = _mapFramePassNameToIndex.find( graphContext._passName );
        const bool                         bPassListed   = iter != _mapFramePassNameToIndex.end() && iter->second < listFramePass.size();
        SW_ASSERT( bPassListed );
        IRHICommandList* const pCmd = graphContext._pCmdList != nullptr ? graphContext._pCmdList : _pCmd;
        if ( bPassListed == false || pCmd == nullptr )
            return;
        const RenderGraphPassDesc& passDesc = listFramePass[iter->second];
        // 프레임 패스마다 GPU 시간(`GPU.<프레임 패스>` — gpu.timings · 감시 값 GPU.PassMs)을 고정 칸 쌍에 쓴다. 게이트는 뷰 패스와 같다(계측 스위치 · 칸 수).
        // 비동기 컴퓨트 큐 리스트(pCmd != _pCmd)는 백엔드가 큐 사이의 리셋 · 해석 순서와 시계를 대조했을 때만 잰다(`IRHITimestampProvider::isComputeQueueTimestampComparable`).
        uint32 timestampBeginSlot = GPUPassScope::kNoTimestamp;
#if SW_PROFILE_COMPILED
        const IRHITimestampProvider* pTimestamps      = _pDevice != nullptr ? _pDevice->findTimestampProvider() : nullptr;
        const uint32                 frameSlot        = FrameRendererUtil::findFramePassTimestampSlot( iter->second );
        const bool                   bQueueComparable = pCmd == _pCmd || ( pTimestamps != nullptr && pTimestamps->isComputeQueueTimestampComparable() );
        const bool                   bWriteGPUTime    = pTimestamps != nullptr && bQueueComparable && _state._gpuPassTimeline.isRecording() && frameSlot != constant::invalid_index::kUint32 &&
                                   ( frameSlot + 1u ) < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            timestampBeginSlot = frameSlot;
#endif
        // 이벤트 표시 + 브레드크럼(시작 · 끝) + GPU 시간 한 벌 — 캡처 · 장치 손실 보고 · 프레임 프로파일러에 프레임 패스 이름(XML 의 `_name`)이 남는다.
        const GPUPassScope scope{ *pCmd, passDesc._name, timestampBeginSlot };
        _frameGraphExecutor.executePass( passDesc._type, *pCmd );
    }

    void FramePassExecutor::onGraphLevelPrologue( const RenderGraphLevelContext& levelContext )
    {
        if ( _pDevice == nullptr || levelContext._pCmdList == nullptr )
            return;

        if ( levelContext._pListBarrier == nullptr )
            return;

        // 그래프가 **실제로 바뀌는 전이만** 추려서 준다. 여기서는 이름으로 텍스처를 찾아 레벨의 전이를 한 묶음으로 낸다(`IRHICommandList::transitionTextures` —
        // DX12 ResourceBarrier · Vulkan vkCmdPipelineBarrier 한 번, 언리얼 RDG 의 패스 앞 전이 배치와 같은 자리). 같은 자원을 여러 패스가 읽어도, 이미 맞는 상태여도 다시 내지 않는다.
        // 꺼진 효과의 출력 이름은 원본을 가리키지만(`TransientAttachmentPool::forward`) 전이는 그 이름으로 만든 텍스처에만 낸다 — 따라가면 꺼진 패스의 "쓰기" 가
        // 같은 레벨에서 원본을 읽는 패스와 부딪힌다. 원본의 읽기 상태는 원본을 입력으로 선언한 그 효과 패스의 레벨이 이미 냈고, 뒤에서 원본을 쓰는 패스는 로드 검증이 막는다.
        frame_vector<RHITextureTransition> listTransition{ FrameAllocator<RHITextureTransition>{ _frameArena } };
        listTransition.reserve( levelContext._pListBarrier->size() );
        for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
        {
            if ( barrier._after == RenderGraphResourceState::Write )
            {
                // 스왑체인은 전용 경로가 있다(핸들 0). 이름으로는 트랜지언트에서 찾을 수 없다.
                if ( barrier._resource == attachmentNames()._swapchain )
                {
                    listTransition.push_back( RHITextureTransition{ 0, RHITextureState::RenderTarget } );
                    continue;
                }
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource );
                if ( texture == 0 )
                    continue;
                // 힙에 겹쳐 놓은 텍스처는 같은 바이트의 앞 텍스처 일이 끝난 뒤에 쓴다(앨리어싱 배리어). 묶음보다 먼저 낸다 — 앨리어싱 시작이 내용을 버린 뒤에 첫 쓰기 전이가 온다.
                // 쓰기 전이마다 내도 맞다 — 첫 쓰기만 골라내는 표를 두지 않는다.
                if ( activePool().isPlaced( barrier._resource ) )
                    levelContext._pCmdList->beginTextureAlias( texture );
                listTransition.push_back( RHITextureTransition{ texture, RHITextureState::RenderTarget } );
            }
            else if ( barrier._after == RenderGraphResourceState::Read )
            {
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource );
                if ( texture != 0 )
                    listTransition.push_back( RHITextureTransition{ texture, RHITextureState::ShaderResource } );
            }
        }
        if ( listTransition.empty() == false )
            levelContext._pCmdList->transitionTextures( listTransition.data(), static_cast<uint32>( listTransition.size() ) );
    }

    void FramePassExecutor::onGraphPassExecute( const RenderGraphPassContext& graphContext )
    {
        if ( _pDevice == nullptr )
            return;

        // 패스 로컬 상태를 새로 만든다. 주의: 멤버 _pCmd 를 저장하고 복원하는 방식은 "한 번에 한 패스만 돈다" 는
        // 전제라 병렬 기록에서 서로를 덮어쓴다.
        // 프레임 시드에서 복사해 뷰, 조명 등 프레임 공통값을 물려받는다.
        // 패스 슬롯의 컨텍스트에 프레임 시드를 **대입**한다. 복사본을 새로 만들면 값 목록과 레지스트리가 프레임마다 다시
        // 늘어난다. 슬롯 수는 submitGraph 가 기록 전에 맞춰 둔다. 이름을 찾지 못하면 마지막 슬롯을 쓴다(직렬 경로에서만 온다).
        const size_t passCount    = _pipelineResource.getGraphPass().size();
        const auto   passSlotIter = _mapPassNameToIndex.find( graphContext._passName );
        // 모듈 확장 패스는 XML 패스 슬롯 **뒤**에 자기 슬롯을 갖는다(병렬 기록의 패스마다 독립 컨텍스트). 슬롯 수는 submitGraph 가 기록 전에 맞춰 둔다.
        const auto   moduleIter  = _mapExtensionPassIndex.find( graphContext._passName );
        const bool   bModulePass = moduleIter != _mapExtensionPassIndex.end() && moduleIter->second < _extensionHost.getPasses().size();
        const size_t slotCount   = passCount + 1 + _extensionHost.getPasses().size();
        const size_t passSlot    = bModulePass ? passCount + 1 + moduleIter->second
                                               : ( ( passSlotIter != _mapPassNameToIndex.end() && passSlotIter->second < passCount ) ? passSlotIter->second : passCount );
        if ( _listPassContext.size() <= passSlot )
            _listPassContext.resize( slotCount );
        // 벡터 **자체**는 병렬 기록 중 읽기만 한다(슬롯은 패스마다 다르다). 비-const 인덱싱이나 data() 는 컨테이너 레이스
        // 탐지기가 벡터에 대한 쓰기로 잡아 워커 둘이 동시에 들어오면 울린다. const 로 읽고 슬롯만 고쳐 쓴다.
        FramePassContext& passContext = const_cast<FramePassContext&>( std::as_const( _listPassContext ).data()[passSlot] );
        passContext                   = _state._frameContext;
        passContext._passName         = graphContext._passName;
        if ( graphContext._pCmdList != nullptr )
            passContext._pCmd = graphContext._pCmdList;
        // 상수 버퍼도 패스마다 따로 잡는다. 커맨드 기록은 지연되는데 상수 쓰기는 즉시 일어나므로,
        // 하나를 공유하면 재생 시점에 마지막 패스 값만 남는다. 못 잡으면(링 고갈, 오류는 링이 남겼다) 이 패스를 그리지 않는다.
        if ( acquirePassCb( passContext ) == false )
            return;
        if ( bModulePass )
        {
            executeExtensionPass( passContext, _extensionHost.getPasses()[moduleIter->second] );
            return;
        }

        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와
        // PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        // 그래프의 패스는 모두 파이프라인 서술에서 만들었다(`_mapPassNameToIndex`). 서술이 없는 패스는 실행할 규칙이 없다 — 이름으로 짐작하지 않는다.
        const auto iter        = _mapPassNameToIndex.find( graphContext._passName );
        const bool bPassListed = iter != _mapPassNameToIndex.end() && iter->second < listPass.size();
        SW_ASSERT( bPassListed );
        if ( bPassListed == false )
            return;
        const RenderGraphPassDesc&    passDesc  = listPass[iter->second];
        [[maybe_unused]] const size_t passIndex = iter->second;

#if SW_PROFILE_COMPILED
        // **GPU 시간은 패스 인덱스로 고정된 슬롯 쌍에 기록한다.** 패스는 병렬로 기록될 수 있어
        // 흐르는 카운터를 쓰면 경쟁이 된다. 인덱스로 고정하면 각 패스가 자기 슬롯 두 개만 건드린다.
        //
        // **계측에는 비용이 든다.** 타임스탬프 하나가 GPU 파이프라인에 표식을 넣고 프레임 끝에
        // resolve 와 읽기가 붙는다. 그래서 (1) Shipping 에서는 이 블록이 통째로 사라지고,
        // (2) Dev 에서도 프로파일러가 켜져 있을 때만 기록한다(`-gv_profileFrames`).
        const uint32 timestampBegin = static_cast<uint32>( passIndex ) * 2u;
        // 패스 슬롯은 패스 번호로 고정이라 주 시점만 기록한다(추가 뷰가 같은 슬롯을 다시 쓰면 주 시점의 시간이 덮인다).
        const IRHITimestampProvider* pTimestamps   = _pDevice != nullptr ? _pDevice->findTimestampProvider() : nullptr;
        const bool                   bWriteGPUTime = pTimestamps != nullptr && passContext._pCmd != nullptr && isRenderingExtraView() == false &&
                                   _state._gpuPassTimeline.isRecording() &&
                                   static_cast<uint32>( passIndex ) < FrameRendererUtil::kGPUTimedPassCapacity &&
                                   ( timestampBegin + 1u ) < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin );
#endif

        executePass( passContext, passDesc );

#if SW_PROFILE_COMPILED
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin + 1u );
#endif
    }

    void FramePassExecutor::setInputRoleEnabled( RenderPassInputRole role, bool bEnabled )
    {
        const uint32 bit = 1u << static_cast<uint32>( role );
        if ( bEnabled )
            _disabledInputRoleMask &= ~bit;
        else
            _disabledInputRoleMask |= bit;
    }

    const hashed_string& FramePassExecutor::inputRoleName( RenderPassInputRole role ) const
    {
        const AttachmentNames& names = attachmentNames();
        switch ( role )
        {
            case RenderPassInputRole::SourceColor:
                return names._sourceColor;
            case RenderPassInputRole::SceneDepth:
                return names._sceneDepth;
            case RenderPassInputRole::GBufferAlbedo:
                return names._gbufferAlbedo;
            case RenderPassInputRole::GBufferNormal:
                return names._gbufferNormal;
            case RenderPassInputRole::ShadowMap:
                return names._shadowMap;
            case RenderPassInputRole::AmbientOcclusion:
                return names._ambientOcclusion;
            case RenderPassInputRole::BloomBright:
                return names._bloomBright;
            case RenderPassInputRole::GBufferMaterial:
                return names._gbufferMaterial;
            case RenderPassInputRole::SceneColorCopy:
                return names._sceneColorCopy;
            case RenderPassInputRole::SceneDepthCopy:
                return names._sceneDepthCopy;
            case RenderPassInputRole::Velocity:
                return names._velocity;
            case RenderPassInputRole::Distortion:
                return names._distortion;
            case RenderPassInputRole::ContactShadow:
                return names._contactShadow;
            case RenderPassInputRole::GBufferCustomData:
                return names._gbufferCustomData;
            case RenderPassInputRole::Invalid:
            case RenderPassInputRole::Count:
                return names._sourceColor;
        }
    }

    void FramePassExecutor::registerDeclaredInputs( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
        {
            const RenderPassInputRole role = static_cast<RenderPassInputRole>( input._role );
            if ( ( _disabledInputRoleMask & ( 1u << input._role ) ) != 0 )
                continue;
            // 이 뷰에서 빠진 패스의 출력(`TransientAttachmentPool::markAbsent`, 꺼진 SSAO)은 걸지 않는다 — 셰이더는 그 역할의 기본값(AO = 1)을 쓴다. 로드 검증 3-7 이 선택 입력만 그렇게 읽게 한다.
            if ( activePool().resolveName( input._attachment ).empty() )
                continue;
            registerPassTexture( context, inputRoleName( role ), input._attachment );
        }
    }
} // namespace sw
