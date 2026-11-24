/**
 * @file FramePassExecutor.cpp
 * @brief 렌더 그래프 패스 실행기의 그래프 짓기와 제출입니다 — 수명, 커맨드 리스트 준비 · 제출, 뷰별 그래프(주 시점 · 추가 뷰), 뷰 패스 상수, 첨부 조회,
 *        콜백 묶기 · 트랜지언트 앨리어싱 계획, 그래프 콜백, 그래프 뒤 NaN 감시.
 * @details 한 클래스를 세 TU 로 나눈다 — 패스 종류마다의 기록은 `FramePassExecutorPasses.cpp`, 렌더 패스 열기와 씬 드로우는 `FramePassExecutorDraw.cpp`
 *          (언리얼 `DeferredShadingRenderer.cpp` 와 패스별 `*Rendering.cpp` 나눔과 같다). 프레임 패스의 실행은 `FrameGraphExecutor`, HZB 짓기 · 가림 판정은 `HZBOcclusion` 이 맡는다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/frame_vector.h"
#include "Core/Container/string.h"
#include "Core/Encoding/FourCCUtil.h"
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
#include "Engine/Graphics/RHI/RHIDescriptors.h"
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
#include "Engine/Renderer/Debug/MeshVertexCheck.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"
#include "Engine/Renderer/Frame/Screen/FrameDisplayEncode.h"
#include "Engine/Renderer/Frame/Screen/FrameScreenRotation.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimestampSlotLayout.h"
#include "Engine/Renderer/Frame/View/ViewTextureMipBiasUtil.h"
#include "Engine/Renderer/Lighting/ShadowFilter.h"
#include "Engine/Renderer/Pipeline/GBufferLayout.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        /// 피사계 심도 착란원 반지름의 상한(px)입니다. depth_of_field.hlsl 의 탭 16 개가 이 반지름 안에 퍼지므로, 더 키우면 탭 사이가 벌어져 고리 무늬가 보인다.
        constexpr float32 kMaxDepthOfFieldRadiusPixels = 8.0f;
        /// 센서 높이(mm, `CameraDepthOfFieldValues::_sensorHeight`)를 m 로 바꾸는 비율입니다.
        constexpr float32 kMillimetersPerMeter = 1000.0f;

        /**
         * @brief 뷰 패스의 첨부 접근 하나를 @p inoutListAccess 에 더합니다. 이미 있는 첨부면 쓰기 접근만 덮는다 — 한 첨부를 읽고 쓰는 패스는 쓰기 하나다
         *        (그래프가 레벨에서 하던 합치기와 같고, 접근 판 `addPass` 는 한 패스 한 자원 한 접근이다).
         */
        void addAttachmentAccess( vector<RenderGraphResourceAccess>& inoutListAccess, hashed_string name, RenderGraphAccess access )
        {
            for ( RenderGraphResourceAccess& existing : inoutListAccess )
            {
                if ( existing._resource != name )
                    continue;
                if ( isRenderGraphWriteAccess( access ) )
                    existing._access = access;
                return;
            }
            inoutListAccess.push_back( RenderGraphResourceAccess{ name, access, RenderGraphViewableKind::Texture } );
        }
    } // namespace

    FramePassExecutor::~FramePassExecutor() = default;

    FramePassExecutor::FramePassExecutor( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameRenderState& state, FramePSOManager& psoManager,
                                          FrameAutoExposure& autoExposure, FrameUpscale& upscale, FrameFog& fog, FrameExtensionHost& extensionHost, FrameCapture& capture,
                                          const FrameScreenRotation& screenRotation, const FrameDisplayEncode& displayEncode )
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
        , _displayEncode{ displayEncode }
        , _frameGraphExecutor{ pDevice, state, psoManager, fog }
        , _frameGraphContext{}
        , _pActiveView{ &state.getMainView() }
        , _pMainView{ &state.getMainView() }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{}
        , _asyncComputeCmd{}
        , _pCmd{ nullptr }
        , _listPassContext{}
        , _graphContext{}
        , _nanCheck{}
        , _nanCheckCommandList{}
        , _frameEndCommandList{}
        , _pLastSwapchainWriter{ nullptr }
        , _pMainOutputCanvasView{ nullptr }
        , _frameArena{}
        , _pendingPSOBatchCount{ 0 }
        , _fallbackPSOBatchCount{ 0 }
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
            reportFatalError( "The NaN check is on but its counters could not be created - see the log above" );
    }

    void FramePassExecutor::releaseResources()
    {
        _frameGraphExecutor.releaseResources();
        _bufferInspector.release();
        if ( _meshVertexCheck != nullptr )
            _meshVertexCheck->release( _pDevice );
        _nanCheckCommandList.reset();
        _frameEndCommandList.reset();
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
        _frameEndCommandList.reset();
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
            for ( unique_ptr<FrameViewTarget>& pView : _state._listView )
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
        return _state._settings._bDrawMerge != SW_FALSE;
    }

    bool FramePassExecutor::submitGraph( IRHIDevice* pDevice )
    {
        // 패스 컨텍스트 슬롯은 병렬 기록 **전에** 맞춘다. 기록 중에 늘리면 워커끼리 경합한다.
        const size_t passCount = _pipelineResource.getGraphPass().size() + _extensionHost.getPasses().size(); // XML 패스 + 모듈 확장 패스(칸 = 그래프 패스 번호)
        if ( _listPassContext.size() < passCount )
            _listPassContext.resize( passCount );
        _frameArena.reset(); // 앞 프레임의 frame_vector 는 모두 사라졌다(기록 지역 목록만 잡는다)
        _graphTransitionCount = 0;

        _pCmd->beginCommandList();
        _pendingPSOBatchCount.store( 0, std::memory_order_relaxed );
        _fallbackPSOBatchCount.store( 0, std::memory_order_relaxed );
        _state._deform._animationTimer.updateTimer();

        // 프레임의 첫 GPU 명령 위치다(Shipping 포함 — 자동 렌더 배율의 입력). 패스 시간 합과 이 값의 차이가 "패스 밖의 GPU 시간"(컴퓨트
        // 프리패스, 업로드, 배리어)이다. 쓸지는 시계가 정한다(`GPUFrameClock::readBack`).
        _state._gpuFrameClock.writeFrameBegin( *_pCmd );
        // 프레임 업로드 단계 — 파이프라인 XML 의 패스가 아니라 실행기가 첫 리스트 맨 앞에 고정으로 기록한다(언리얼 RDG 초기 업로드 · 유니티 SRP 프레임 시작 CommandBuffer).
        // 정점 풀의 GPU 복사가 여기 든다 — 어느 파이프라인이든 그리기보다 먼저 풀이 채워진다(R-15).
        // 시작하지 못한 프레임은 여기 오지 않는다(`RenderThread::executeFrameBody` · `SceneCapture` 가 그리지 않는다).
        SW_ASSERT( pDevice->isFrameStarted() );
        _state._gpuScene.recordFrameUploads( *_pCmd, *pDevice );
        // 실시간 프로브의 복셀 칠하기가 읽을 위치 흐름의 GPU 복사도 같은 단계다.
        _state._probeUpdater.recordUploads( *_pCmd );
#if !defined( SW_SHIPPING )
        if ( _state._pRenderDebug != nullptr )
        {
            if ( _meshVertexCheck == nullptr )
                _meshVertexCheck = make_unique<MeshVertexCheck>();
            _meshVertexCheck->service( *_pCmd, *pDevice, _state._gpuScene, *_state._pRenderDebug );
        }
#endif
#if SW_PROFILE_COMPILED
        // 프레임 그래프 구간은 프로파일러 기록 중일 때만 쓴다. 게이트는 패스 타임스탬프와 같다(`onGraphPassExecute`).
        const IRHITimestampProvider* pTimestamps   = pDevice->findTimestampProvider();
        const bool                   bWriteGPUTime = pTimestamps != nullptr && _state._gpuPassTimeline.isRecording() &&
                                   GPUTimestampSlotLayout::kComputeEnd < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            _pCmd->writeTimestamp( GPUTimestampSlotLayout::kComputeBegin );
#endif

        // 프레임 그래프(파이프라인 XML 의 `_listFramePass`) — 깊이 히스토리 · 입자 · 변형 · 컬링 · 라이트 그리드 등, 이번 프레임에 그리는 **모든 뷰**의 몫이다.
        // 순서는 XML 의 자원 읽기 · 쓰기가 정한다. 앞 프레임들의 셰이더 assert · NaN 기록이 도착했으면 로그로 낸다(몇 프레임 늦다).
        _nanCheck.poll();
        recordFrameGraph( pDevice );
        drawRainOcclusion(); // 컬링 뒤 · 모든 뷰 앞 — 이번 프레임의 뷰가 같은 맵을 읽는다
        // 월드 공간 UI 의 렌더 텍스처다. 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트(같은 큐)로 그린다.
        _state._canvasRenderer.drawTargets( *_pCmd, *pDevice, _state._canvasFrame, _psoManager.getCache() );
        // 2D 빛 쿠키 아틀라스 — 새로 칸을 받은 쿠키만 그 칸에 그린다. 빛 받는 스프라이트 패스가 읽기 전에 같은 프리패스 리스트에서 끝난다.
        drawLight2DCookieAtlas();
        // UV 공간 층 굽기(피부 화장 · 문신 · 번짐) — 구운 텍스처를 머티리얼이 읽기 전에 같은 프리패스 리스트에서 끝난다.
        drawTextureLayerBakes();

#if SW_PROFILE_COMPILED
        if ( bWriteGPUTime )
            _pCmd->writeTimestamp( GPUTimestampSlotLayout::kComputeEnd );
#endif

        // 프리패스 리스트를 먼저 닫아 큐에 낸다. 뷰마다의 리스트와 병렬 패스 리스트보다 인다이렉트 인자 준비가 GPU 타임라인에서 먼저 끝나게 하려는 것이다
        // (같은 큐의 ExecuteCommandLists 호출 순서가 곧 실행 순서다).
        pDevice->executeCommandList( _pCmd->closeCommandList() );
        _pCmd = nullptr;

        // 주 시점이 읽는 렌더 텍스처(CCTV 모니터)를 먼저 그린다. 주 시점의 패스 시드는 추가 뷰가 덮어쓰므로 보존해 둔다.
        _state._mainSeedScratch   = _state._frameContext;
        uint32 renderedExtraCount = renderExtraViewsOfKind( pDevice, RenderViewOutputKind::RenderTexture );
        // 주 시점의 시드와 클리어 기록으로 돌아온다(추가 뷰가 자기 값을 덮어썼다).
        _state._frameContext = _state._mainSeedScratch;
        _bHasExecutedDepthPrepass.store( 0 );

        // 주 출력의 캔버스는 주 출력에 마지막으로 그리는 뷰가 그린다. 화면 사각형 뷰는 주 시점 뒤에 겹치므로, 주 시점이 그리면 그 뷰가 UI 를 덮는다.
        _pMainOutputCanvasView = &_state.getMainView();
        for ( const unique_ptr<FrameViewTarget>& pView : _state._listView )
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
        writeFrameEndTimestamp( pDevice );
        // 다음 프레임의 깊이 히스토리가 이번 프레임의 장면 깊이와 짝이 되도록 뷰-투영을 적는다.
        _frameGraphExecutor.rememberDepthHistoryViews();
        // 그래프가 낸 전이 수 — 손 전이를 그래프로 옮기는 단계(렌더 그래프 접근 핸들 ②)마다 이 수가 늘지 않아야 한다.
        SW_PROFILE_COUNT( "RT.Graph.transitions", _graphTransitionCount );
        return bOk;
    }

    void FramePassExecutor::writeFrameEndTimestamp( IRHIDevice* pDevice )
    {
        // 마지막 패스가 어느 리스트(병렬 레벨 · 추가 뷰)에서 끝나든 그 뒤에 제출되는 작은 리스트 하나에 끝 시각을 쓴다(같은 큐는 제출 순서로 실행된다).
        if ( _state._gpuFrameClock.isWritingFrame() == false )
            return;
        if ( _frameEndCommandList == nullptr )
            _frameEndCommandList = pDevice->createCommandList();
        IRHICommandList* pCmd = _frameEndCommandList.get();
        if ( pCmd == nullptr )
            return;
        pCmd->beginCommandList();
        _state._gpuFrameClock.writeFrameEnd( *pCmd );
        pDevice->executeCommandList( pCmd->closeCommandList() );
    }

    uint32 FramePassExecutor::renderExtraViewsOfKind( IRHIDevice* pDevice, RenderViewOutputKind outputKind )
    {
        uint32 renderedCount = 0;
        for ( size_t viewIndex = FrameRenderState::kFirstExtraViewIndex; viewIndex < _state._listView.size(); ++viewIndex )
        {
            FrameViewTarget& view = *_state._listView[viewIndex];
            if ( view._bRenderThisFrame == SW_FALSE || view._outputKind != outputKind )
                continue;
            renderExtraView( pDevice, view );
            ++renderedCount;
        }
        return renderedCount;
    }

    void FramePassExecutor::prepareShadowCacheLayerResource()
    {
        // 그림자 캐시 층은 기록 **전에** 만든다 — 패스 기록(병렬 레벨) 안에서 텍스처를 만들면 자원 표를 바꿔 디바이스가 멈춘다(`requireRegistryMutableNow`).
        // 아틀라스는 점광 · 스폿 그림자 패스(깊이 첨부가 LocalShadowMap 역할)의 깊이 첨부다(첨부 풀이 이미 잡았다). 같은 형식 · 크기면 아무것도 하지 않는다.
        ShadowCache& cache = _state._shadowCache;
        if ( cache.usesCacheLayer() == false )
            return;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type != RenderPassKind::Shadow || pass._resolvedDepthRole != static_cast<uint8>( RenderPassInputRole::LocalShadowMap ) )
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
        registerViewResources( _state.getMainView() );
        // 병렬 기록이 가능하면(백엔드 capability, TaskManager, 레벨이 나올 만큼 컴파일된 그래프) 레벨마다 여러 리스트로 기록한다.
        // 첫 프레임처럼 그래프가 아직 컴파일되지 않았으면(getExecutionOrder() 가 비어 있으면) 직렬 경로로 간다.
        const bool bCanRunParallel = _state._pTaskManager != nullptr &&
                                     pDevice->getCapabilities()._bParallelCommandRecording != SW_FALSE &&
                                     _state._graph.getExecutionOrder().size() > 1;
        if ( bCanRunParallel )
            return _state._graph.executeParallel( _graphContext, *_state._pTaskManager, *pDevice );

        // 직렬 경로도 그래프가 추론한 배리어를 쓴다. 패스가 기록하는 리스트와 **같은 것**을 넘긴다.
        if ( _state.getMainView()._commandList == nullptr )
            _state.getMainView()._commandList = pDevice->createCommandList();
        IRHICommandList* pMainCmd = _state.getMainView()._commandList.get();
        if ( pMainCmd == nullptr )
            return false;
        // 직렬 경로도 그래프가 패스 문맥에 이 리스트를 싣는다(`RenderGraph::execute` — 병렬과 같은 `recordPassBody`).
        pMainCmd->beginCommandList();
        const bool bOk = _state._graph.execute( _graphContext, pMainCmd );
        pDevice->executeCommandList( pMainCmd->closeCommandList() );
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
        resetClearedAttachments();
        _bHasExecutedDepthPrepass.store( 0 );
        prepareShadowCacheLayerResource(); // 주 시점보다 먼저 그리는 뷰도 있다 — 층은 기록 전에
        registerViewResources( view );

        pCmd->beginCommandList();
        if ( _state._graph.execute( _graphContext, pCmd ) == false )
            SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.RenderGraphCouldNot", "Render graph could not run for an extra view (cycle or compile failure)" );
        // 렌더 텍스처는 이어서 주 시점의 머티리얼이, 호스트 타깃은 에디터 UI 가 읽는다 — 읽기 상태로 돌려 둔다(DX11 은 RTV 슬롯에서 뗀다).
        if ( view._outputKind == RenderViewOutputKind::RenderTexture && view._pOutputTexture != nullptr )
            pCmd->prepareTextureForShaderRead( view._pOutputTexture->getHandle() );
        else if ( view._outputKind == RenderViewOutputKind::ViewportTarget && view._viewportTarget != 0 )
            pCmd->prepareTextureForShaderRead( view._viewportTarget );
        pDevice->executeCommandList( pCmd->closeCommandList() );

        _pActiveView = &_state.getMainView();
    }

    bool FramePassExecutor::shouldDrawBatchInPass( RenderPassKind passType, const GPUMeshBatch& batch, const GPUMeshBatchBinding& binding ) const
    {
        // 변형 풀이 받지 못한 메시(상한 초과 · 버퍼 실패)는 어느 패스에서도 그리지 않는다 — 바인드 · 레스트 포즈로 대신 그리지 않는다(풀이 오류를 남겼다).
        if ( binding._bDeformRefused != SW_FALSE )
            return false;
        const RenderPassKindInfo& passInfo = getRenderPassKindInfo( passType );
        if ( passInfo.shouldFilterBatchesByMaterial() == false )
            return true;
        const GPUShaderPermutation* pPermutation =
            ( batch._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _state._gpuScene.findShaderPermutation( batch._shaderPermutation );
        return passInfo.shouldDrawMaterial( pPermutation != nullptr ? &pPermutation->_listDefine : nullptr );
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
        context._passValues.setFloat4( names._shadowFilter, ShadowFilter::makeShaderParams( ShadowFilter::getActiveMode( _state._settings._shadowFilter ), shadow._directionalPenumbraScale ) );
        // 접촉 그림자 채널은 프레임 공통이다(빛 목록이 프레임 공통이다) — 추가 뷰도 같은 값이다. 채널의 값은 목록 번호라 올라간 버퍼의 번호로 옮긴다
        // (상한을 넘은 프레임은 빛을 골라 줄여 번호가 당겨진다 — 빠진 빛의 채널은 빈 채널 -1).
        const float4& sourceLights                                            = _state._frameShadow._contactShadowLights;
        float32       arrChannel[FrameShadowData::kContactShadowChannelCount] = { sourceLights._x, sourceLights._y, sourceLights._z, sourceLights._w };
        for ( float32& channel : arrChannel )
        {
            if ( channel < 0.0f )
                continue;
            const uint32 uploaded = _state._lightBuffer.findUploadedLightIndex( static_cast<uint32>( channel + 0.5f ) );
            channel               = uploaded == constant::invalid_index::kUint32 ? -1.0f : static_cast<float32>( uploaded );
        }
        context._passValues.setFloat4( names._contactShadowLights, float4{ arrChannel[0], arrChannel[1], arrChannel[2], arrChannel[3] } );
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
        // 뷰 사각형 — 풀은 최대 크기 그대로이고 자동 렌더 배율은 이 사각형만 줄인다(`FrameViewTarget::updateViewRect`). 배율 1 이면 셰이더의 UV 변환이 값을 바꾸지 않는다.
        const FrameRendererUtil::ViewRectConstants viewRect =
            FrameRendererUtil::makeViewRectConstants( view._rect._width, view._rect._height, pool.getWidth(), pool.getHeight(), view._previousRectScale );
        context._passValues.setFloat4( passConstantNames()._viewRectScale, viewRect._scale );
        context._passValues.setFloat4( passConstantNames()._viewRectSize, viewRect._size );
        // 출력 해상도 공간(TAA 뒤) — 사각형 배율은 렌더 공간과 같고 크기만 다르다(`FrameViewTarget::updateViewRect`). 업스케일하지 않으면 위 값과 같다.
        const FrameRendererUtil::ViewRectConstants outputRect =
            FrameRendererUtil::makeViewRectConstants( view._outputRect._width, view._outputRect._height, pool.getOutputWidth(), pool.getOutputHeight(), view._previousRectScale );
        context._passValues.setFloat4( passConstantNames()._outputRectSize, outputRect._size );
        const float32 renderToOutput = pool.getOutputWidth() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getOutputWidth() ) : 1.0f;
        context._passValues.setFloat4( passConstantNames()._temporalUpscale, float4{ pool.hasTemporalUpscale() ? 1.0f : 0.0f, renderToOutput, 0.0f, 0.0f } );
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
        // 톤 곡선 어깨 — SDR 은 1(Reinhard 그대로, 골든 불변). HDR 출력의 장면 출력 RT 에 그리는 뷰(화면 출력이고 주 출력이 호스트 RT 가 아닌 뷰)만 늘린다(display_encode.hlsli).
        const bool    bHDRSceneView = bScreenOutput && _state._outputRenderTarget == 0 && _displayEncode.isActive();
        const float32 toneShoulder  = bHDRSceneView ? _displayEncode.getToneShoulder() : 1.0f;
        context._passValues.setFloat4( passConstantNames()._displayOutput, float4{ toneShoulder, 0.0f, 0.0f, 0.0f } );
        // 컬러 그레이딩(post_grade.hlsli) — 화이트 밸런스 · 범위 보정 · LUT. LUT 는 세기가 있는 후처리 뷰만 묻는다(안 쓰는 경로를 읽지 않는다). 띠 모양이 아니면 데이터 오류라 걸지 않는다.
        // 겹친 볼륨의 둘째 LUT(무게 _colorGradingLUTBlendWeight)도 같은 규칙으로 묻는다. 읽히지 않은 쪽은 항등 LUT 로 보고 남은 쪽의 무게만 세기에 곱한다(PostGradeConstants::make).
        const bool bWantsLUT = view._settings.usesPostProcess() && post._colorGradingLUTIntensity > 0.0f;
        const auto findLUT   = [this]( const DataPath& path ) -> const Texture2D*
        {
            const Texture2D* pFound = path.isSet() ? _state._pathTextures.requestReady( path, "colour grading LUT" ) : nullptr;
            if ( pFound != nullptr && PostGradeConstants::isLUTStrip( pFound->getWidth(), pFound->getHeight() ) == false )
            {
                SW_LOG_ERROR_ONCE_ID( "FramePassExecutor.ColorGradingLUTShape", "Colour grading LUT '%#' is %#x%# - it must be an N*N wide, N tall strip (e.g. 256x16); the LUT is left out",
                                      path.c_str(), pFound->getWidth(), pFound->getHeight() );
                return nullptr;
            }
            return pFound;
        };
        const Texture2D* pLUT      = bWantsLUT ? findLUT( post._colorGradingLUT ) : nullptr;
        const Texture2D* pBlendLUT = bWantsLUT && post._colorGradingLUTBlendWeight > 0.0f ? findLUT( post._colorGradingLUTBlend ) : nullptr;
        float32          lutWeight = 1.0f - MathUtil::saturate( post._colorGradingLUTBlendWeight ); // 첫 LUT 의 무게 — 둘째가 나머지
        if ( pLUT == nullptr && pBlendLUT != nullptr )
        {
            pLUT      = pBlendLUT; // 첫 LUT 가 없으면(항등) 둘째를 첫 칸에 건다
            pBlendLUT = nullptr;
            lutWeight = 1.0f - lutWeight;
        }
        view._pColorGradingLUT         = pLUT;
        view._pColorGradingLUTBlend    = pBlendLUT;
        const PostGradeConstants grade = PostGradeConstants::make( post, pLUT != nullptr ? pLUT->getHeight() : 0u, pBlendLUT != nullptr ? pBlendLUT->getHeight() : 0u, lutWeight );
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
        const Texture2D* pDirt      = bWantsDirt ? _state._pathTextures.requestReady( post._bloomDirtMask, "bloom dirt mask" ) : nullptr;
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
            // 반지름은 피사계 심도 패스가 그리는 해상도 공간의 px 다(디퍼드는 TAA 뒤라 출력 공간 — 시간 업스케일이면 렌더보다 크다).
            const uint32 passHeight = pool.getExtent( findPassResolutionSpace( RenderPassKind::DepthOfField ), false );
            cocScale                = 0.5f * static_cast<float32>( passHeight ) * sensorHeightMeters * scaleY * scaleY / ( 4.0f * lens._aperture * lens._focusDistance );
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

    RenderResolutionSpace FramePassExecutor::findPassResolutionSpace( RenderPassKind type ) const
    {
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._type != type || pass._listResolvedColorOutput.empty() )
                continue;
            for ( const RenderPassAttachment& attachment : _pipelineResource.getDesc()._listAttachment )
            {
                if ( string_view( attachment._name ) == pass._listResolvedColorOutput[0]._attachment.view() )
                    return attachment._resolutionSpace;
            }
        }
        return RenderResolutionSpace::Render;
    }

    void FramePassExecutor::applyViewProjection( FramePassContext& context, FrameViewTarget& view, const float4x4& viewProj )
    {
        // TAA 가 도는 뷰만 서브픽셀 지터를 래스터 행렬에 싣는다(언리얼 TAA · 유니티 URP/HDRP · Godot 의 Halton 지터). 지터 든 행렬은 이 함수 안에만 있다 —
        // 컬링 · 지난 뷰-투영(속도 기준)은 받은 @p viewProj(지터 없음)를 쓰고, 화면 속도는 셰이더가 `g_TemporalJitter` 를 빼서 물체 · 카메라 움직임만 남긴다.
        const TransientAttachmentPool& pool = view._transientPool;
        // 기록을 버리는 프레임(처음 · 컷 · 건너뛴 프레임 뒤)은 이번 원본만 남으므로 지터하지 않는다 — 한 장짜리 그림이 흔들리기만 한다.
        const bool bJitter = view._taaHistory.isWritten() && view._taaHistory.isReset() == false;
        if ( bJitter == false )
            view._temporalJitter.restart(); // 기록이 새로 시작하면 지터도 첫 표본부터 — 그 전에 흐른 프레임 수(앞 씬 · 대기)가 그림을 바꾸지 않는다
        // 지터는 렌더 공간 픽셀이다. 시간 업스케일이면 출력 픽셀마다 표본이 모이게 바퀴를 늘린다(`TemporalJitterSequence::computePhaseCount`).
        const float32  renderScale = pool.getOutputWidth() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getOutputWidth() ) : 1.0f;
        const float2   ndcJitter   = bJitter ? view._temporalJitter.advance( pool.getWidth(), pool.getHeight(), TemporalJitterSequence::computePhaseCount( renderScale ) ) : float2{};
        const float4x4 raster      = TemporalJitterSequence::applyToViewProjection( viewProj, ndcJitter );
        context._passValues.setMatrix( passConstantNames()._viewProj, raster );
        // 역행렬은 **여기서만** 만든다. 디퍼드 조명이 깊이에서 월드 위치를 복원하는 데 쓰는데,
        // 뷰와 따로 채우면 언젠가 한쪽만 갱신되고, 그 증상은 "빛이 한 프레임 늦게 따라온다"이다. 깊이는 지터 든 행렬로 그렸으므로 역행렬도 그것이다.
        context._passValues.setMatrix( passConstantNames()._invViewProj, raster.invert() );
        context._passValues.setFloat4( passConstantNames()._temporalJitter, float4{ ndcJitter._x, ndcJitter._y, 0.0f, 0.0f } );
        // 줄여 그려 키우는 뷰는 텍스처를 출력 해상도만큼 선명하게 읽는다(R-15 — 배율 1 이면 0). 렌더 크기는 첨부 풀, 출력 크기는 뷰가 든다.
        context._passValues.setFloat4( passConstantNames()._textureMipBias, float4{ ViewTextureMipBiasUtil::compute( pool.getHeight(), view._outputHeight ), 0.0f, 0.0f, 0.0f } );
        // 컬링은 기록 시작 전에 도는데 그때는 상수 버퍼에서 도로 꺼낼 수 없다. 지금 그리는 뷰에 같은 값을 남긴다(지터 없는 것 — 절두체가 프레임마다 흔들리지 않는다).
        view._cullInput.setViewProjection( viewProj );

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
        // 그리기마다 슬롯을 잡는 패스(데칼 · 확장 요소 · 그림자 타일)의 객체당 상수 비용 — 패스 수보다 크게 늘면 인스턴스 버퍼로 옮길 후보다.
        SW_PROFILE_COUNT( "RT.Bind.passCbAcquires", 1 );
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
        // 기본값(뎁스 1, 노멀 0.5 · 0.5 · 1, SSAO 흰색)은 선언과 갈라질 수 있어 지웠다. 풀에 없는 이름은 백버퍼(`kRHIBackBufferTexture`)이고,
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
            if ( pass._type != RenderPassKind::Present )
                continue;
            for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
            {
                if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor && findTransient( input._attachment ) != 0 )
                    return input._attachment;
            }
        }
        return hashed_string{};
    }

    void FramePassExecutor::prepareVisualizedAttachment( IRHICommandList& cmd ) const
    {
        // 개발 보기 `gpu.show <첨부>` — Present 가 선언하지 않은 첨부를 읽는다. 앞 레벨이 쓴 첨부라 같은 레벨의 다른 패스가 그것을 쓰지 않는다(쓰기 사슬).
        const GPUPixelInspector* const pInspector = engine::findSubsystem<GPUPixelInspector>();
        string                         attachment;
        if ( isRenderingExtraView() || pInspector == nullptr || pInspector->findVisualizedAttachment( attachment ) == false )
            return;
        const hashed_string    name    = hashed_string::findInterned( attachment );
        const RHITextureHandle texture = name.empty() ? 0 : findTransient( name );
        if ( texture != 0 )
            cmd.prepareTextureForShaderRead( texture );
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
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassKind::NanCheck );
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
                const TransientAttachmentPool::Attachment attachment = _state.getMainView()._transientPool.find( output._attachment );
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
        pDevice->executeCommandList( pCmd->closeCommandList() );
    }

    bool FramePassExecutor::bindPassCallbacks()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        _state._graph.clear();
        // Swapchain 을 쓰는 마지막 패스다. 그래프는 같은 출력의 쓰기를 선언 순서로 연결한다(쓰기 사슬). 스크린샷 캡처를 백버퍼로 복사하는 일은 그 패스 끝에서 한다.
        _pLastSwapchainWriter = nullptr;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            if ( VectorUtil::contains( pass._listOutput, string( kSwapchainOutputName ) ) )
                _pLastSwapchainWriter = &pass;
        }
        // 패스 종류가 쓰는 엔진 자원은 그래프보다 오래 산다(뷰가 가진 텍스처 · 프레임 그래프 버퍼의 이 뷰 칸) — 수명은 그래프 전체이고 "쓰지 않고 읽는다" 경고가 없다.
        const RenderGraphResourceAccess arrEngineResource[] = {
            ViewGraphResource::autoExposureHistory(),
            ViewGraphResource::autoExposureTarget(),
            ViewGraphResource::taaHistory(),
            ViewGraphResource::skyCloudHistory(),
            ViewGraphResource::upscaleIntermediate(),
            ViewGraphResource::shadowCacheLayer(),
            ViewGraphResource::postProcessChainBeforeTonemap(),
            ViewGraphResource::postProcessChainAfterTonemap(),
            ViewGraphResource::depthHistory(),
            ViewGraphResource::hzb(),
            ViewGraphResource::occlusionTiles(),
            ViewGraphResource::cullArgs(),
            ViewGraphResource::cullVisibleInstances(),
            ViewGraphResource::cullLODState(),
            ViewGraphResource::occludedOrders(),
        };
        for ( const RenderGraphResourceAccess& resource : arrEngineResource )
        {
            _state._graph.declareResource( resource._resource, RenderGraphResourceKind::Persistent );
        }

        // 모듈 확장 패스(ExtensionRenderPass M3)가 끼는 자리 — 단계마다 XML 패스 목록의 정해진 첫 자리 앞이다(없으면 끝). 선언 순서가 쓰기 사슬이라 같은 자리의 모듈 패스는 선언한 순서다.
        // 모듈 패스도 같은 그래프의 노드다 — 읽기 · 쓰기 선언이 같은 위상 정렬과 배리어 추론을 탄다. 패스 번호(`_payload`)는 XML 패스 뒤에 이어 붙인다.
        const uint32 passCount    = static_cast<uint32>( listPass.size() );
        const size_t anchorPre    = 0;
        size_t       anchorPost   = listPass.size();
        size_t       anchorOutput = listPass.size();
        for ( size_t anchorIndex = 0; anchorIndex < listPass.size(); ++anchorIndex )
        {
            const RenderPassKindInfo& anchorInfo = getRenderPassKindInfo( listPass[anchorIndex]._type );
            if ( anchorPost == listPass.size() && ( anchorInfo.hasFlag( RenderPassTraitFlag::kSkipsWithoutPostProcess ) || anchorInfo.hasFlag( RenderPassTraitFlag::kOutputPass ) ) )
                anchorPost = anchorIndex;
            if ( anchorOutput == listPass.size() && anchorInfo.hasFlag( RenderPassTraitFlag::kOutputPass ) )
                anchorOutput = anchorIndex;
        }
        auto addExtensionPasses = [this, passCount]( ExtensionHookStage stage )
        {
            for ( uint32 moduleIndex = 0; moduleIndex < static_cast<uint32>( _extensionHost.getPasses().size() ); ++moduleIndex )
            {
                const ExtensionFramePass& modulePass = _extensionHost.getPasses()[moduleIndex];
                if ( modulePass._params._stage != stage )
                    continue;
                vector<RenderGraphResourceAccess> listExtensionAccess;
                for ( const ExtensionPassRead& read : modulePass._params._listRead )
                {
                    addAttachmentAccess( listExtensionAccess, hashed_string( read._attachment.c_str() ), RenderGraphAccess::ShaderRead );
                }
                for ( const string& write : modulePass._params._listWrite )
                {
                    addAttachmentAccess( listExtensionAccess, hashed_string( write.c_str() ), RenderGraphAccess::RenderTarget );
                }
                // 깊이 첨부도 쓰기 선언이다(XML 패스와 같은 규칙 — 레벨 프롤로그가 첨부 전이를 낸다).
                if ( modulePass._params._depthAttachment.empty() == false )
                    addAttachmentAccess( listExtensionAccess, hashed_string( modulePass._params._depthAttachment.c_str() ), RenderGraphAccess::DepthWrite );
                _state._graph.addPass( modulePass._graphName, std::move( listExtensionAccess ),
                                       SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onGraphPassExecute, this ), RHIQueueKind::Graphics, passCount + moduleIndex );
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

        // 컴퓨트 구현(`_implementation="Compute"`)은 네이티브 bindless 에서만 돈다 — PSO 와 실행이 같은 판정을 본다(`FramePSOManager::createPassPSOs` · `executeGenericFullscreenPass`).
        const bool                 bNativeBindless = _pDevice != nullptr && _pDevice->supportsNativeBindlessSampling();
        const RenderGraphPassDesc* pTonemapPass    = FrameAutoExposure::findTonemapPass( _pipelineResource );
        for ( uint32 index = 0; index < passCount; ++index )
        {
            addModulePassesAt( index );
            const RenderGraphPassDesc& pass = listPass[index];
            const hashed_string        nameHash( pass._name.c_str() );

            // 접근 판: 입력은 셰이더 읽기, 컬러 출력은 렌더 타깃, 깊이 첨부는 깊이 쓰기다(언리얼 RDG 의 RTV · DSVWrite). 한 첨부를 읽고 쓰는 패스는 쓰기 하나다(`addAttachmentAccess`).
            // 깊이 첨부도 그래프가 아는 접근이다. 깊이를 쓰지 않는 패스(투명)도 그것을 DSV 로 바인딩하므로 "첨부" 상태가 필요하다. 입력에만 적었어도
            // 여기서 쓰기로 적는다. 그래야 레벨 프롤로그가 첨부 전이(prepareTextureForRenderTarget)를 미리 내고, 앞 패스가 SRV 로 바인딩해 둔 깊이를
            // D3D11 이 해제하며(OMSetRenderTargets 해저드), 뒤에서 깊이를 읽는 패스가 다시 읽기 전이를 받는다(언리얼 RDG 의 깊이 첨부 접근).
            // 순서는 쓰기 사슬을 따른다. 이 패스보다 **먼저 선언한** 읽는 패스는 앞 버전을 읽고 이 패스 앞에 온다.
            // 컴퓨트로 쓰는 패스는 그 첨부를 UAV 로 선언한다 — 패스 몸통이 전이하지 않고 레벨 콜백이 RW 텍스처 상태로 옮긴다(Compute 패스는 입력을 RW 텍스처로 읽기만 한다 — UAV 읽기).
            // 앞 컴퓨트의 쓰기 뒤에 오는 UAV 접근은 레벨 콜백이 텍스처 UAV 배리어를 낸다(`RenderGraphBarrier::_bUAVBarrier`).
            const bool                        bComputePass   = pass._type == RenderPassKind::Compute;
            const bool                        bComputeOutput = bComputePass || ( pass._implementation == RenderPassExecutionMode::Compute && bNativeBindless );
            vector<RenderGraphResourceAccess> listAccess;
            for ( const string& in : pass._listInput )
            {
                addAttachmentAccess( listAccess, hashed_string( in.c_str() ), bComputePass ? RenderGraphAccess::UnorderedAccessRead : RenderGraphAccess::ShaderRead );
            }
            for ( const string& out : pass._listOutput )
            {
                const hashed_string     outName( out.c_str() );
                const RenderGraphAccess colorAccess = bComputeOutput ? RenderGraphAccess::UnorderedAccess : RenderGraphAccess::RenderTarget;
                addAttachmentAccess( listAccess, outName, outName == pass._resolvedDepthAttachment ? RenderGraphAccess::DepthWrite : colorAccess );
            }
            if ( pass._resolvedDepthAttachment.empty() == false )
                addAttachmentAccess( listAccess, pass._resolvedDepthAttachment, RenderGraphAccess::DepthWrite );
            appendEngineResourceAccesses( pass, &pass == pTonemapPass, listAccess );

            // 패스 번호(`_payload`)는 이 패스의 차례다 — 실행 콜백이 이름으로 서술을 다시 찾지 않는다. 이름이 겹치면 그래프가 뒤의 선언을 버린다(파이프라인 검사가 먼저 알린다).
            _state._graph.addPass( nameHash, std::move( listAccess ), SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onGraphPassExecute, this ), RHIQueueKind::Graphics,
                                   index );
        }

        addModulePassesAt( listPass.size() ); // 자리를 못 찾은 단계(출력 패스가 없는 파이프라인)는 끝에 붙는다
        _extensionHost.markBound();

        _state._graph.setLevelPrologue( SW_DELEGATE_METHOD( RenderGraphLevelPrologueFn, &FramePassExecutor::onGraphLevelPrologue, this ) );

        if ( _state._graph.compile() == false )
        {
            SW_LOG_ERROR_ID( "FramePassExecutor.CallbackBindCompileFailed", "Callback bind compile failed" );
            return false;
        }
        rebuildTransientAliasPlan();
        return bindFramePassCallbacks();
    }

    void FramePassExecutor::appendEngineResourceAccesses( const RenderGraphPassDesc& pass, bool bTonemapPass, vector<RenderGraphResourceAccess>& inoutListAccess )
    {
        // 패스 종류가 스스로 쓰는 엔진 자원을 그 종류가 선언한다(XML 은 첨부만 적는다) — 패스 안 왕복(UAV ↔ 셰이더 읽기)은 쓰기로 선언한 자원만 `RenderGraphPassContext::transition` 으로 한다.
        // 자동 노출 — 톤맵하는 패스가 지난 칸을 읽고 이번 칸에 쓴 뒤 같은 패스가 읽는다.
        if ( bTonemapPass )
        {
            inoutListAccess.push_back( ViewGraphResource::autoExposureHistory() );
            inoutListAccess.push_back( ViewGraphResource::autoExposureTarget() );
        }
        if ( pass._type == RenderPassKind::Present )
        {
            inoutListAccess.push_back( ViewGraphResource::upscaleIntermediate() ); // FSR 1 — EASU 가 그리고 같은 패스의 RCAS 가 읽는다
        }
        else if ( pass._type == RenderPassKind::TAA )
        {
            inoutListAccess.push_back( ViewGraphResource::taaHistory() ); // 지난 기록을 읽고 같은 패스가 이번 출력을 복사한다
        }
        else if ( pass._type == RenderPassKind::VolumetricClouds )
        {
            inoutListAccess.push_back( ViewGraphResource::skyCloudHistory() ); // TAA 와 같다
        }
        else if ( pass._type == RenderPassKind::Lighting )
        {
            // 타일 분류 — 같은 패스의 컴퓨트가 쓰고(UAV) 간접 그리기가 읽는다.
            inoutListAccess.push_back( ViewGraphResource::deferredTileArgs() );
            inoutListAccess.push_back( ViewGraphResource::deferredTileList() );
        }
        else if ( pass._type == RenderPassKind::Shadow && pass._resolvedDepthRole == static_cast<uint8>( RenderPassInputRole::LocalShadowMap ) )
        {
            // 점광 · 스폿 아틀라스 패스가 그림자 캐시 층을 다시 그리고(깊이) 같은 패스의 복원이 읽는다.
            inoutListAccess.push_back( ViewGraphResource::shadowCacheLayer() );
        }
        else if ( pass._type == RenderPassKind::PostProcessMaterial )
        {
            // 같은 자리 머티리얼의 앞 결과 사본 — 출력을 복사하고(복사 대상) 다음 머티리얼이 셰이더 읽기로 읽는다. 톤맵 대신 자리는 사본이 없다.
            if ( pass._blendableLocation == PostProcessBlendableLocation::BeforeTonemap )
                inoutListAccess.push_back( ViewGraphResource::postProcessChainBeforeTonemap() );
            else if ( pass._blendableLocation == PostProcessBlendableLocation::AfterTonemap )
                inoutListAccess.push_back( ViewGraphResource::postProcessChainAfterTonemap() );
        }
        else if ( pass._type == RenderPassKind::ScreenSpaceReflection )
        {
            // hi-z trace 가 읽는 HZB 를 이번 장면 깊이로 짓는다 — 깊이 사본(렌더 타깃 → RW 텍스처) · 피라미드 · 가림 타일(UAV → 셰이더 읽기). 장면 깊이는 XML 입력이다.
            inoutListAccess.push_back( ViewGraphResource::depthHistory() );
            inoutListAccess.push_back( ViewGraphResource::hzb() );
            inoutListAccess.push_back( ViewGraphResource::occlusionTiles() );
        }
        else if ( pass._type == RenderPassKind::LateOcclusion )
        {
            // 2 단계 가림의 뒤 단계 — 장면 깊이를 깊이 쓰기로 선언한다(패스 안에서 복사 동안만 셰이더 읽기로 옮기고 되돌린다 — 돌지 않는 프레임에는 전이가 없다).
            addAttachmentAccess( inoutListAccess, attachmentNames()._sceneDepth, RenderGraphAccess::DepthWrite );
            inoutListAccess.push_back( ViewGraphResource::depthHistory() );
            inoutListAccess.push_back( ViewGraphResource::hzb() );
            inoutListAccess.push_back( ViewGraphResource::occlusionTiles() );
            inoutListAccess.push_back( ViewGraphResource::cullArgs() );
            inoutListAccess.push_back( ViewGraphResource::cullVisibleInstances() );
            inoutListAccess.push_back( ViewGraphResource::cullLODState() );
            inoutListAccess.push_back( ViewGraphResource::occludedOrders() );
        }
    }

    void FramePassExecutor::rebuildTransientAliasPlan()
    {
        ++_state._transientAliasGeneration; // 뷰 풀이 다음 셋업에서 새 계획으로 다시 만든다
        _state._transientAliasPlan = RenderGraphAliasPlan{};
        // NaN 감시는 그래프가 끝난 뒤 모든 패스 출력을 다시 읽는다 — 나눠 쓴 텍스처는 앞 첨부의 내용이 이미 덮였다. 감시를 켠 실행은 나누지 않는다.
        if ( _state._settings._bAliasTransients == SW_FALSE || GPUNanCheck::isRequested() )
            return;

        const RenderGraph&                   graph    = _state._graph;
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
            if ( pLife == nullptr )
                continue;
            // 서술 키 — 같은 텍스처로 넘길 수 있는 조건(형식 · 나눗수 · RW 용도 · 클리어 값, D3D12 의 최적 클리어 값이 서술에 든다).
            // 칸마다 32 비트로 옮겨 해시한다(구조체째 해시하면 정렬 채움 바이트가 섞인다).
            // 공간이 다르면 크기가 다를 수 있다(시간 업스케일) — 나눗수 칸에 공간을 함께 싣는다.
            const uint32 sizeWord = attachment._resolutionDivisor | ( static_cast<uint32>( attachment._resolutionSpace ) << 16u );
            uint32       arrDescWord[7]{ static_cast<uint32>( attachment._format ), sizeWord, FrameRendererUtil::isComputeOutputOrInput( listPass, attachment._name ) ? 1u : 0u, 0u, 0u, 0u, 0u };
            Memory::copy( &arrDescWord[3], &attachment._clearColor._x, sizeof( float32 ) );
            Memory::copy( &arrDescWord[4], &attachment._clearColor._y, sizeof( float32 ) );
            Memory::copy( &arrDescWord[5], &attachment._clearColor._z, sizeof( float32 ) );
            Memory::copy( &arrDescWord[6], &attachment._clearColor._w, sizeof( float32 ) );
            const uint64 descKey = HashUtil::appendBytes64( HashUtil::kFNVOffset64, arrDescWord, sizeof( arrDescWord ) );
            // 힙에 겹쳐 놓으려면 프레임의 첫 쓰기가 지우기여야 한다(앞 텍스처가 남긴 바이트가 내용이 된다) — RW(컴퓨트가 처음 쓴다)는 빼고 Clear 만 받는다.
            const uint8 bPlaceable  = attachment._loadOp == RHIRenderPassLoadOp::Clear && arrDescWord[2] == 0u ? 1 : 0;
            mapCandidateIndex[name] = listCandidate.size();
            listCandidate.push_back( RenderGraphAliasCandidate{ name, descKey, pLife->_firstLevel, pLife->_lastLevel, bPlaceable } );
        }

        // 꺼진 효과는 출력 이름이 원본을 가리킨다(`kBypassesToSource`) — 그 출력을 읽는 패스가 원본을 읽으므로 원본의 수명이 출력의 끝까지 늘어난다. 둘을 한 구간으로 묶는다.
        // TAA 출력은 셋업이 기록 형식으로 읽어 프레임 밖에서도 쓴다 — 나누지 않는다.
        vector<uint8>                listExcluded( listCandidate.size(), 0 );
        vector<RenderGraphAliasJoin> listJoin;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            const RenderPassKindInfo& info = getRenderPassKindInfo( pass._type );
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                const auto outputIt = mapCandidateIndex.find( output._attachment );
                if ( outputIt == mapCandidateIndex.end() )
                    continue;
                if ( pass._type == RenderPassKind::TAA )
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
                    listJoin.push_back( RenderGraphAliasJoin{ static_cast<uint32>( inputIt->second ), static_cast<uint32>( outputIt->second ) } );
                }
            }
        }
        // 사슬(꺼진 효과 둘이 잇따라 원본을 가리킨다)도 한 구간이다 — 짝마다 맞추면 한 단계만 전파된다.
        RenderGraphAliasPlan::joinLifetimes( listCandidate, listJoin );
        vector<RenderGraphAliasCandidate> listAliasable;
        for ( size_t candidateIndex = 0; candidateIndex < listCandidate.size(); ++candidateIndex )
        {
            if ( listExcluded[candidateIndex] == 0 )
                listAliasable.push_back( listCandidate[candidateIndex] );
        }
        _state._transientAliasPlan = RenderGraphAliasPlan::make( listAliasable );
        SW_PROFILE_COUNT( "RT.Frame.transientAliases", _state._transientAliasPlan._mapAliasToOwner.size() );
    }

    bool FramePassExecutor::bindFramePassCallbacks()
    {
        RenderGraph& frameGraph = _state._frameGraph;
        frameGraph.clear();
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
            // 접근은 XML 규칙에서 나온다(버퍼 입력 SRV · 출력 UAV · 간접 인자 · 복사 — `RenderPipelineAsset::buildFramePassAccesses`). 패스 번호(`_payload`)는 `_listFramePass` 차례다.
            frameGraph.addPass( nameHash, _pipelineResource.buildFramePassAccesses( pass ),
                                SW_DELEGATE_METHOD( RenderGraphPassExecuteFn, &FramePassExecutor::onFrameGraphPassExecute, this ), pass._queue, index );
        }
        // 비동기 컴퓨트 큐가 없는 백엔드(DX11 · GL · WebGPU · 큐가 하나인 Vulkan 장치)는 AsyncCompute 패스를 그래픽스 큐로 계획한다(능력 표가 정한 길).
        const bool bAsyncCompute = _state._settings._bAsyncCompute != SW_FALSE && _pDevice != nullptr && _pDevice->getCapabilities()._bAsyncCompute != SW_FALSE;
        frameGraph.setAsyncComputeSupported( bAsyncCompute );
        // 프레임 패스가 없는 파이프라인(미리보기 · 시험 파이프라인)은 빈 그래프다 — 컬링 · 그리드 등은 PSO 도 없다(`FramePSOManager::getEnginePSO`).
        if ( listFramePass.empty() )
            return true;
        // 등록한 자원(`FrameBufferTable`)의 전이는 레벨 콜백이 낸다. 등록하지 않은 자원은 아직 패스가 스스로 전이한다(렌더 그래프 접근 핸들 ② 의 차례).
        frameGraph.setLevelPrologue( SW_DELEGATE_METHOD( RenderGraphLevelPrologueFn, &FramePassExecutor::onFrameGraphLevelPrologue, this ) );
        if ( frameGraph.compile() == false )
        {
            SW_LOG_ERROR_ID( "FramePassExecutor.FrameGraphCompileFailed", "Frame graph (_listFramePass) could not be compiled - see the cycle report above" );
            return false;
        }
        return true;
    }

    void FramePassExecutor::recordFrameGraph( IRHIDevice* pDevice )
    {
        _frameBufferTable.reset();
        _frameGraphExecutor.beginFrame( *_pCmd, _frameBufferTable );
        RenderGraph& frameGraph = _state._frameGraph;
        // 주인이 등록한 접근이 프레임 그래프의 시작이자 끝이다(언리얼 RegisterExternalBuffer) — 그래프가 끝에 되돌린다.
        frameGraph.importResources( _frameBufferTable );
        bool bOk = true;
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
#if !defined( SW_SHIPPING )
        // 버퍼 보기: 그래프가 되돌려 둔 접근(등록 접근)에서 읽고 그 상태로 되돌린다(`RenderBufferInspector`).
        if ( _state._pRenderDebug != nullptr )
            _bufferInspector.service( *_pCmd, pDevice, _frameBufferTable, _frameGraphContext, _psoManager.getCache(), *_state._pRenderDebug );
#endif
        _frameGraphExecutor.endFrame( *_pCmd );
    }

    void FramePassExecutor::onFrameGraphPassExecute( const RenderGraphPassContext& graphContext )
    {
        // 패스 번호(`_payload`)는 `_listFramePass` 차례다(`bindFramePassCallbacks`). 리스트는 그래프가 늘 싣는다 — 직렬 · 큐 실행이 같은 `recordPassBody` 다.
        const vector<RenderGraphPassDesc>& listFramePass = _pipelineResource.getFramePass();
        const uint32                       passIndex     = graphContext._payload;
        SW_ASSERT( passIndex < listFramePass.size() );
        IRHIPassCommandList* const pCmd = graphContext.getCommandList();
        if ( passIndex >= listFramePass.size() || pCmd == nullptr )
            return;
        const RenderGraphPassDesc& passDesc = listFramePass[passIndex];
        // 프레임 패스마다 GPU 시간(`GPU.<프레임 패스>` — gpu.timings · 감시 값 GPU.PassMs)을 고정 칸 쌍에 쓴다. 게이트는 뷰 패스와 같다(계측 스위치 · 칸 수).
        // 비동기 컴퓨트 큐 리스트(pCmd != _pCmd)는 백엔드가 큐 사이의 리셋 · 해석 순서와 시계를 대조했을 때만 잰다(`IRHITimestampProvider::isComputeQueueTimestampComparable`).
        uint32 timestampBeginSlot = GPUPassScope::kNoTimestamp;
#if SW_PROFILE_COMPILED
        const IRHITimestampProvider* pTimestamps      = _pDevice != nullptr ? _pDevice->findTimestampProvider() : nullptr;
        const uint32                 frameSlot        = GPUTimestampSlotLayout::findFramePassSlot( passIndex );
        const bool                   bQueueComparable = pCmd == _pCmd || ( pTimestamps != nullptr && pTimestamps->isComputeQueueTimestampComparable() );
        const bool                   bWriteGPUTime    = pTimestamps != nullptr && bQueueComparable && _state._gpuPassTimeline.isRecording() && frameSlot != constant::invalid_index::kUint32 &&
                                   ( frameSlot + 1u ) < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            timestampBeginSlot = frameSlot;
#endif
        // 이벤트 표시 + 브레드크럼(시작 · 끝) + GPU 시간 한 벌 — 캡처 · 장치 손실 보고 · 프레임 프로파일러에 프레임 패스 이름(XML 의 `_name`)이 남는다.
        const GPUPassScope scope{ *pCmd, passDesc._name, timestampBeginSlot };
        // 패스 몸통은 선언한 접근 핸들로만 등록 자원을 받는다(`RenderGraphPassContext::getBuffer` · `uavBarrier`) — 표와 이 패스의 리스트를 문맥에 싣는다.
        RenderGraphPassContext passContext = graphContext;
        passContext._pResourceTable        = &_frameBufferTable;
        _frameGraphExecutor.executePass( passDesc._type, passContext );
    }

    void FramePassExecutor::onGraphLevelPrologue( const RenderGraphLevelContext& levelContext )
    {
        // 뷰 그래프 — 등록 자원은 지금 뷰의 엔진 자원 표다(`registerViewResources`). 첨부는 풀에서 찾는다.
        issueLevelBarriers( levelContext, _viewResourceTable );
    }

    void FramePassExecutor::onFrameGraphLevelPrologue( const RenderGraphLevelContext& levelContext )
    {
        issueLevelBarriers( levelContext, _frameBufferTable );
    }

    void FramePassExecutor::issueLevelBarriers( const RenderGraphLevelContext& levelContext, const FrameBufferTable& table )
    {
        if ( _pDevice == nullptr || levelContext._pCmdList == nullptr )
            return;

        if ( levelContext._pListBarrier == nullptr )
            return;

        // 그래프가 **실제로 바뀌는 전이만** 추려서 준다. 여기서는 이름으로 텍스처를 찾아 레벨의 전이를 한 묶음으로 낸다(`IRHICommandList::transitionTextures` —
        // DX12 ResourceBarrier · Vulkan vkCmdPipelineBarrier 한 번, 언리얼 RDG 의 패스 앞 전이 배치와 같은 자리). 같은 자원을 여러 패스가 읽어도, 이미 맞는 상태여도 다시 내지 않는다.
        // 이 뷰에서 빠진 패스는 실행 계획에 없고 꺼진 효과의 출력을 읽는 접근은 계획이 원본 이름으로 바꿨다(`selectGraphPlan`) — 이름은 늘 그 이름으로 만든 텍스처다.
        // 등록 자원(프레임 그래프는 `_frameBufferTable`, 뷰 그래프는 지금 뷰의 `_viewResourceTable`)은 이름의 모든 칸을 버퍼는 `transitionBuffers` 한 번으로, UAV → UAV 는 UAV 배리어로 낸다.
        // 큐: 그래프가 그 패스의 리스트를 넘긴다 — 비동기 컴퓨트 패스의 전이는 컴퓨트 리스트에 들어간다(D3D12 는 그 큐가 받는 상태로 접는다).
        IRHICommandList&                   cmd = *levelContext._pCmdList;
        frame_vector<RHITextureTransition> listTransition{ FrameAllocator<RHITextureTransition>{ _frameArena } };
        listTransition.reserve( levelContext._pListBarrier->size() );
        uint32 uavBarrierCount = 0;
        for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
        {
            bool                  bRegistered = false;
            const vector<uint32>* pSlots      = table.findSlots( barrier._resource );
            for ( size_t slot = 0; pSlots != nullptr && slot < pSlots->size(); ++slot )
            {
                const uint32 entryIndex = ( *pSlots )[slot];
                if ( entryIndex == constant::invalid_index::kUint32 || table.getEntries()[entryIndex]._viewable != barrier._viewable )
                    continue;
                const FrameBufferTable::Entry& entry = table.getEntries()[entryIndex];
                bRegistered                          = true;
                if ( barrier._viewable == RenderGraphViewableKind::Buffer )
                {
                    if ( barrier._bUAVBarrier != 0 )
                    {
                        cmd.uavBarrier( entry.getBuffer() );
                        ++uavBarrierCount;
                    }
                    else if ( const optional<RHIBufferState> state = RenderGraphAccessUtil::findBufferState( barrier._access ) )
                    {
                        // 그래프가 만든 버퍼의 첫 쓰기 — 앞 이름(앞 뷰의 그래프)이 같은 버퍼를 UAV 로 남겼으면 상태 전이가 없다. 그 쓰기를 기다린다.
                        if ( entry._bGraphOwned && barrier._before == RenderGraphResourceState::Undefined && barrier._access == RenderGraphAccess::UnorderedAccess )
                        {
                            cmd.uavBarrier( entry.getBuffer() );
                            ++uavBarrierCount;
                        }
                        _levelBufferTransitions.add( entry.getBuffer(), *state );
                    }
                }
                else if ( barrier._bUAVBarrier != 0 )
                {
                    cmd.uavBarrierTexture( entry.getTexture() );
                    ++uavBarrierCount;
                }
                else if ( const optional<RHITextureState> state = RenderGraphAccessUtil::findTextureState( barrier._access ) )
                {
                    listTransition.push_back( RHITextureTransition{ entry.getTexture(), *state } );
                }
            }
            // 등록하지 않은 버퍼는 이 프레임 · 뷰에 일하지 않는다(등록 조건이 실행 조건) — 전이를 내지 않는다.
            if ( bRegistered || barrier._viewable != RenderGraphViewableKind::Texture )
                continue;
            // 컴퓨트로 쓰고 읽는 첨부(UAV · UAV 읽기) — 앞 레벨의 UAV 쓰기 뒤면(`_bUAVBarrier`) 상태는 그대로이고 텍스처 UAV 배리어로 기다린다.
            if ( barrier._access == RenderGraphAccess::UnorderedAccess || barrier._access == RenderGraphAccess::UnorderedAccessRead )
            {
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource );
                if ( texture != 0 && barrier._bUAVBarrier != 0 )
                {
                    cmd.uavBarrierTexture( texture );
                    ++uavBarrierCount;
                }
                else if ( texture != 0 )
                {
                    listTransition.push_back( RHITextureTransition{ texture, RHITextureState::UnorderedAccess } );
                }
                continue;
            }
            const bool bAttachmentWrite = barrier._access == RenderGraphAccess::RenderTarget || barrier._access == RenderGraphAccess::DepthWrite;
            const bool bShaderRead      = barrier._access == RenderGraphAccess::ShaderRead || barrier._access == RenderGraphAccess::DepthRead;
            if ( bAttachmentWrite )
            {
                // 스왑체인은 전용 핸들(`kRHIBackBufferTexture`)이다. 이름으로는 트랜지언트에서 찾을 수 없다.
                if ( barrier._resource == attachmentNames()._swapchain )
                {
                    listTransition.push_back( RHITextureTransition{ kRHIBackBufferTexture, RHITextureState::RenderTarget } );
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
            else if ( bShaderRead )
            {
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource );
                if ( texture != 0 )
                    listTransition.push_back( RHITextureTransition{ texture, RHITextureState::ShaderResource } );
            }
        }
        _graphTransitionCount += static_cast<uint32>( listTransition.size() + _levelBufferTransitions.getPending().size() ) + uavBarrierCount;
        _levelBufferTransitions.flush( cmd );
        if ( listTransition.empty() == false )
            cmd.transitionTextures( listTransition.data(), static_cast<uint32>( listTransition.size() ) );
    }

    void FramePassExecutor::onGraphPassExecute( const RenderGraphPassContext& graphContext )
    {
        if ( _pDevice == nullptr )
            return;

        // 패스 번호(`_payload`)는 파이프라인 패스 차례이고 그 뒤가 모듈 확장 패스다(`bindPassCallbacks`) — 이름 해시로 다시 찾지 않는다.
        const vector<RenderGraphPassDesc>& listPass    = _pipelineResource.getGraphPass();
        const uint32                       passCount   = static_cast<uint32>( listPass.size() );
        const uint32                       passIndex   = graphContext._payload;
        const bool                         bModulePass = passIndex >= passCount;
        const uint32                       moduleIndex = bModulePass ? passIndex - passCount : 0u;
        SW_ASSERT( bModulePass == false || moduleIndex < _extensionHost.getPasses().size() );
        if ( bModulePass && moduleIndex >= _extensionHost.getPasses().size() )
            return;
        // 이번 뷰에 할 일이 없는 패스(뷰 패스 고르기)는 이 뷰의 실행 계획에 없다(`selectGraphPlan`) — 여기 오지 않는다.
        SW_ASSERT( bModulePass || isPassDisabledInView( passIndex ) == false );

        // 패스 로컬 상태를 새로 만든다. 주의: 멤버 _pCmd 를 저장하고 복원하는 방식은 "한 번에 한 패스만 돈다" 는 전제라 병렬 기록에서 서로를 덮어쓴다.
        // 패스 슬롯(= 패스 번호)의 컨텍스트에 프레임 시드를 **대입**한다. 복사본을 새로 만들면 값 목록과 레지스트리가 프레임마다 다시 늘어난다.
        // 슬롯 수는 submitGraph 가 기록 전에 맞춰 둔다 — 벡터 **자체**는 병렬 기록 중 읽기만 한다(슬롯은 패스마다 다르다). 비-const 인덱싱이나 data() 는 컨테이너 레이스
        // 탐지기가 벡터에 대한 쓰기로 잡아 워커 둘이 동시에 들어오면 울린다. const 로 읽고 슬롯만 고쳐 쓴다.
        SW_ASSERT( passIndex < _listPassContext.size() );
        if ( passIndex >= _listPassContext.size() )
            return;
        FramePassContext& passContext = const_cast<FramePassContext&>( std::as_const( _listPassContext ).data()[passIndex] );
        passContext                   = _state._frameContext;
        passContext._passName         = graphContext._passName;
        passContext._pCmd             = graphContext.getCommandList(); // 직렬 · 병렬 모두 그래프가 패스의 리스트를 싣는다(`recordPassBody`) — 패스 기록 면만
        // 패스 몸통은 뷰 엔진 자원(`ViewGraphResource`)을 선언한 핸들로만 받고 패스 안 왕복도 그 핸들로 옮긴다 — 지금 뷰의 표를 실은 문맥을 넘긴다(몸통이 도는 동안만 산다).
        RenderGraphPassContext viewGraphContext = graphContext;
        viewGraphContext._pResourceTable        = &_viewResourceTable;
        passContext._pGraphPass                 = &viewGraphContext;
        // 상수 버퍼도 패스마다 따로 잡는다. 커맨드 기록은 지연되는데 상수 쓰기는 즉시 일어나므로,
        // 하나를 공유하면 재생 시점에 마지막 패스 값만 남는다. 못 잡으면(링 고갈, 오류는 링이 남겼다) 이 패스를 그리지 않는다.
        if ( passContext._pCmd == nullptr || acquirePassCb( passContext ) == false )
            return;
        if ( bModulePass )
        {
            executeExtensionPass( passContext, _extensionHost.getPasses()[moduleIndex] );
            return;
        }

        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와
        // PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        const RenderGraphPassDesc& passDesc = listPass[passIndex];
        // 개발 보기 `gpu.show <첨부>` 는 Present 가 선언 밖 첨부를 읽는 유일한 길이다 — 무엇을 보일지가 실행 중에 바뀌어(개발 명령) 선언으로 적을 수 없다.
        // 패스 몸통은 전체 리스트에 닿지 않으므로 실행기(그래프 문맥의 friend — 이 한 자리)가 몸통 앞에서 이 패스의 리스트로 옮긴다(개발 전용).
        if ( passDesc._type == RenderPassKind::Present && graphContext._pCmdList != nullptr )
            prepareVisualizedAttachment( *graphContext._pCmdList );

#if SW_PROFILE_COMPILED
        // **GPU 시간은 패스 인덱스로 고정된 슬롯 쌍에 기록한다.** 패스는 병렬로 기록될 수 있어
        // 흐르는 카운터를 쓰면 경쟁이 된다. 인덱스로 고정하면 각 패스가 자기 슬롯 두 개만 건드린다.
        //
        // **계측에는 비용이 든다.** 타임스탬프 하나가 GPU 파이프라인에 표식을 넣고 프레임 끝에
        // resolve 와 읽기가 붙는다. 그래서 (1) Shipping 에서는 이 블록이 통째로 사라지고,
        // (2) Dev 에서도 프로파일러가 켜져 있을 때만 기록한다(`-gv_profileFrames`).
        const uint32 timestampBegin = passIndex * 2u;
        // 패스 슬롯은 패스 번호로 고정이라 주 시점만 기록한다(추가 뷰가 같은 슬롯을 다시 쓰면 주 시점의 시간이 덮인다).
        const IRHITimestampProvider* pTimestamps   = _pDevice != nullptr ? _pDevice->findTimestampProvider() : nullptr;
        const bool                   bWriteGPUTime = pTimestamps != nullptr && isRenderingExtraView() == false && _state._gpuPassTimeline.isRecording() &&
                                   passIndex < GPUTimestampSlotLayout::kTimedPassCapacity && ( timestampBegin + 1u ) < pTimestamps->getTimestampSlotCount();
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin );
#endif

        executePass( passContext, passDesc );

#if SW_PROFILE_COMPILED
        if ( bWriteGPUTime )
            passContext._pCmd->writeTimestamp( timestampBegin + 1u );
#endif
        passContext._pGraphPass = nullptr; // 문맥은 이 함수의 지역 값이다 — 몸통 밖에서 가리키지 않는다
    }

    void FramePassExecutor::registerViewResources( FrameViewTarget& view )
    {
        SW_ASSERT( &view == _pActiveView ); // 고르기 결과(`isPassKindEnabledInView`)는 지금 뷰의 것이다
        _viewResourceTable.reset();
        // 등록 접근은 이 뷰 그래프의 시작이자 끝이다 — 아래 끝에서 그래프에 들이고, 그래프가 끝에 그 접근으로 되돌린다(패스가 돌지 않아 UAV 로 남은 자원도 다음 사용자가 같은 상태를 본다).
        // 자동 노출 — 이번 프레임에 재는 뷰만(`prepareAutoExposure`). 지난 칸을 읽고 이번 칸에 쓴다.
        if ( view._autoExposureHistory.isWritten() && view._autoExposureHistory.isValid() )
        {
            _viewResourceTable.registerTexture( ViewGraphResource::autoExposureHistory()._name, 0, view._autoExposureHistory.getPrevious()._texture, RenderGraphAccess::ShaderRead );
            _viewResourceTable.registerTexture( ViewGraphResource::autoExposureTarget()._name, 0, view._autoExposureHistory.getCurrent()._texture, RenderGraphAccess::ShaderRead );
        }
        // TAA 기록 — TAA 가 도는 뷰만(`applyViewPassSelection`). 지난 프레임의 복사 끝에 셰이더 읽기로 남았다.
        if ( view._taaHistory.isWritten() && view._taaHistory.isValid() )
            _viewResourceTable.registerTexture( ViewGraphResource::taaHistory()._name, 0, view._taaHistory.getCurrent()._texture, RenderGraphAccess::ShaderRead );
        // 볼륨 구름 기록 — 구름이 도는 뷰만(`applyViewPassSelection`). TAA 기록과 같은 상태로 남는다.
        if ( view._skyCloudHistory.isWritten() && view._skyCloudHistory.isValid() )
            _viewResourceTable.registerTexture( ViewGraphResource::skyCloudHistory()._name, 0, view._skyCloudHistory.getCurrent()._texture, RenderGraphAccess::ShaderRead );
        // FSR 1 — 이번 프레임에 키우는 뷰만(`prepareUpscale`).
        if ( view._bUpscaleThisFrame == SW_TRUE && view._upscaleIntermediate.isValid() )
            _viewResourceTable.registerTexture( ViewGraphResource::upscaleIntermediate()._name, 0, view._upscaleIntermediate._texture, RenderGraphAccess::ShaderRead );
        // 그림자 캐시 층 — 다시 그리는 프레임만(층은 모든 뷰가 같아 처음 그리는 뷰가 다시 그리고 표시를 내린다). 다시 그리지 않는 프레임은 층이 셰이더 읽기 그대로라 전이가 없다.
        const ShadowCache& cache = _state._shadowCache;
        if ( cache.usesCacheLayer() && cache.needsRebuild() && cache.getLayer()._texture != 0 )
            _viewResourceTable.registerTexture( ViewGraphResource::shadowCacheLayer()._name, 0, cache.getLayer()._texture, RenderGraphAccess::ShaderRead );
        // 후처리 머티리얼의 앞 결과 사본 — 그 자리에 이어 그리는 머티리얼이 있는 뷰만 만들었다(`FramePostProcessMaterials::prepareChain`).
        if ( view._postProcessChainBeforeTonemap.isValid() )
            _viewResourceTable.registerTexture( ViewGraphResource::postProcessChainBeforeTonemap()._name, 0, view._postProcessChainBeforeTonemap._texture, RenderGraphAccess::ShaderRead );
        if ( view._postProcessChainAfterTonemap.isValid() )
            _viewResourceTable.registerTexture( ViewGraphResource::postProcessChainAfterTonemap()._name, 0, view._postProcessChainAfterTonemap._texture, RenderGraphAccess::ShaderRead );
        // 이번 장면 깊이로 HZB 를 짓는 패스(화면 공간 반사)와 2 단계 가림의 뒤 단계(주 시점)가 쓰는 깊이 사본 · HZB · 컬링 버퍼.
        _frameGraphExecutor.registerViewGraphResources( _viewResourceTable, view, isPassKindEnabledInView( RenderPassKind::ScreenSpaceReflection ) );
        _state._graph.importResources( _viewResourceTable );
        // 디퍼드 조명 타일 — 조명 패스가 도는 뷰에서 분류를 돌릴 수 있을 때만(능력 · PSO). 이 그래프 안에서 쓰고 버리므로 그래프가 만든다. 등록이 곧 실행 조건이다(`executeTiledLighting`).
        if ( isPassKindEnabledInView( RenderPassKind::Lighting ) && isTiledLightingReady( view ) )
        {
            constexpr RHIBufferUsage kArgsUsage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
            constexpr RHIBufferUsage kListUsage = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess;
            _state._graph.createBuffer( ViewGraphResource::deferredTileArgs()._name, RenderGraphBufferDesc{ static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), kDeferredTileClassCount, kArgsUsage } );
            _state._graph.createBuffer( ViewGraphResource::deferredTileList()._name,
                                        RenderGraphBufferDesc{ static_cast<uint32>( sizeof( uint32 ) ), kDeferredTileClassCount * view._deferredTileCapacity, kListUsage } );
        }
        if ( _pDevice != nullptr )
            _state._graph.allocateBuffers( *_pDevice, _viewResourceTable );
        selectGraphPlan();
    }

    void FramePassExecutor::selectGraphPlan()
    {
        const vector<RenderGraphPassDesc>& listPass = _pipelineResource.getGraphPass();
        const TransientAttachmentPool&     pool     = activePool();
        // 칸마다 — 돈다 · 빠지고 출력은 그대로이거나 없다 · 빠지고 출력이 원본을 가리킨다. 같은 패스가 빠져도 가리키는지가 다르면 읽는 쪽의 전이가 다르다.
        constexpr uint8 kOmitted   = 1;
        constexpr uint8 kForwarded = 2;
        _listScratchPassState.assign( listPass.size(), 0 );
        for ( uint32 passIndex = 0; passIndex < static_cast<uint32>( listPass.size() ); ++passIndex )
        {
            if ( isPassDisabledInView( passIndex ) == false )
                continue;
            const bool bForwarded            = VectorUtil::containsIf( listPass[passIndex]._listResolvedColorOutput, [&pool]( const RenderGraphPassDesc::ResolvedAttachment& output )
                       { return pool.isForwarded( output._attachment ) && pool.resolveName( output._attachment ).empty() == false; } );
            _listScratchPassState[passIndex] = bForwarded ? kForwarded : kOmitted;
        }
        if ( _state._graph.selectPlan( _listScratchPassState ) )
            return;
        unordered_map<hashed_string, hashed_string> mapForward;
        for ( const RenderGraphPassDesc& pass : listPass )
        {
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                if ( pool.isForwarded( output._attachment ) )
                    mapForward[output._attachment] = pool.resolveName( output._attachment );
            }
        }
        SW_PROFILE_COUNT( "RT.Graph.planBuilds", 1 ); // 계획을 새로 지은 수 — 켜짐 조합이 프레임마다 바뀌면 는다
        _state._graph.addPlan( _listScratchPassState, mapForward );
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
            case RenderPassInputRole::LocalShadowMap:
                return names._localShadowMap;
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
            case RenderPassInputRole::VelocityNeighborMax:
                return names._velocityNeighborMax;
            case RenderPassInputRole::DBufferA:
                return names._dbufferA;
            case RenderPassInputRole::DBufferB:
                return names._dbufferB;
            case RenderPassInputRole::DBufferC:
                return names._dbufferC;
            case RenderPassInputRole::SkyClouds:
                return names._skyClouds;
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
            // 이 뷰에서 빠진 패스의 출력은 걸리지 않는다(`registerPassTexture`) — 로드 검증 3-7 이 선택 입력만 그렇게 읽게 한다.
            registerPassTexture( context, inputRoleName( role ), input._attachment );
        }
    }
} // namespace sw
