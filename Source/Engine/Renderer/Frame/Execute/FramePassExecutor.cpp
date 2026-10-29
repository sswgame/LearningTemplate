/**
 * @file FramePassExecutor.cpp
 * @brief 패스 실행의 수명과 제출입니다 — 상주 자원(자리표 버퍼 · assert 링 · NaN 감시), 커맨드 리스트 준비, 컴퓨트 프리패스 · 뷰별 그래프 제출, 뷰 패스 상수, 첨부 조회.
 * @details 패스별 기록은 `FramePassExecutorPasses.cpp`, 드로우는 `FramePassExecutorDraw.cpp`, 컴퓨트 프리패스는 `FramePassExecutorCompute.cpp` 에 있다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/IRHITimestampProvider.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostProcess.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"

namespace sw
{
    SW_LOG_CALLER( "FramePassExecutor" );

    /**
     * @brief `-gv_drawMerge=0` 이면 같은 PSO 와 머티리얼의 연속 배치를 멀티 드로우 하나로 묶지 않고 배치마다 한 번씩 부릅니다.
     * @details 묶은 이미지와 묶지 않은 이미지가 같아야 한다. 다르면 배치 테이블(g_SwBatches)이나 드로우 ID 가 틀린 것이다. 기본은 묶는 것이다.
     */
    SW_GLOBAL_VARIABLE( bool, gv_drawMerge, true, "씬 배치 멀티 드로우 묶기 (false=배치마다 호출, 진단용)" );

    namespace
    {
        constexpr float4 kDefaultBloomParams{ 0.55f, 0.65f, 0.25f, 0.0f };
        /// 피사계 심도 착란원 반지름의 상한(px)입니다. depthoffield.hlsl 의 탭 16 개가 이 반지름 안에 퍼지므로, 더 키우면 탭 사이가 벌어져 고리 무늬가 보인다.
        constexpr float32 kMaxDepthOfFieldRadiusPixels = 8.0f;
        /// 센서 높이(mm, `CameraDepthOfFieldValues::_sensorHeight`)를 m 로 바꾸는 비율입니다.
        constexpr float32 kMillimetersPerMeter = 1000.0f;
    } // namespace

    FramePassExecutor::~FramePassExecutor() = default;

    FramePassExecutor::FramePassExecutor( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameRenderState& state, FramePSOManager& psoManager,
                                          FramePostProcess& postProcess, FrameExtensionHost& extensionHost, FrameCapture& capture )
        : _pDevice{ pDevice }
        , _pipelineResource{ pipeline }
        , _state{ state }
        , _psoManager{ psoManager }
        , _postProcess{ postProcess }
        , _extensionHost{ extensionHost }
        , _capture{ capture }
        , _pActiveView{ &state._mainView }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{}
        , _pCmd{ nullptr }
        , _listPassContext{}
        , _mapPassNameToIndex{}
        , _mapExtensionPassIndex{}
        , _graphContext{}
        , _transparentRankPlaceholder{}
        , _occlusionTilesPlaceholder{}
        , _occlusionReadback{}
        , _occlusionMailbox{}
        , _arrOcclusionReadbackMeta{}
        , _occlusionReadbackSerial{ 0 }
        , _gpuAssert{}
        , _nanCheck{}
        , _nanCheckCommandList{}
        , _pLastSwapchainWriter{ nullptr }
        , _pMainOutputCanvasView{ nullptr }
        , _indirectDrawCallCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _lastRenderedExtraViewCount{ 0 }
        , _lodFadeStep{ 0 }
        , _lodFadePreviousNanoseconds{ 0 }
        , _bGPUCullingActive{ SW_FALSE }
        , _bMaterialFallbackMissingLogged{ 0 }
        , _bMissingColorTargetLogged{ 0 }
        , _bHasExecutedDepthPrepass{ 0 }
    {
    }

    void FramePassExecutor::ensureResources()
    {
        // 셰이더 assert 링은 PSO 보다 먼저 만든다 — assert 변형 PSO 는 이 버퍼 없이는 돌릴 수 없으므로 만들지 못하면 멈춘다(폴백 없음).
        if ( _gpuAssert.initialize( _pDevice ) == false )
            SW_FATAL( "gv_gpuAssert is on but the GPU assert ring could not be created - see the log above" );
        if ( _nanCheck.initialize( _pDevice ) == false )
            SW_FATAL( "gv_nanCheck is on but the NaN check counters could not be created - see the log above" );

        // 정렬 디스패치의 t2 자리표 — 순번 표가 없는 뷰도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트).
        constexpr uint32 kZeroRank = 0;
        if ( _transparentRankPlaceholder.ensureCapacity( _pDevice, sizeof( uint32 ), 1, RHIBufferUsage::ShaderResource, true, false, &kZeroRank ) == false )
            SW_LOG_ERROR( "Failed to create the transparent rank placeholder - extra views draw transparency in the main view order" );
        // 컬링 디스패치의 t2 자리표 — 가림을 보지 않는 슬롯도 빈 슬롯으로 디스패치하지 않는다(위와 같은 이유). 깊이 1 은 아무것도 가리지 않는다.
        constexpr float32 kFarDepth = 1.0f;
        if ( _occlusionTilesPlaceholder.ensureCapacity( _pDevice, sizeof( float32 ), 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, &kFarDepth ) == false )
            SW_LOG_ERROR( "Failed to create the occlusion tiles placeholder - GPU culling binds no buffer at t2" );
    }

    void FramePassExecutor::releaseResources()
    {
        _transparentRankPlaceholder.release( _pDevice );
        _occlusionTilesPlaceholder.release( _pDevice );
        _occlusionReadback.shutdown();
        _occlusionMailbox.clear();
        _gpuAssert.shutdown();
        _nanCheckCommandList.reset();
        _nanCheck.shutdown();
    }

    void FramePassExecutor::forgetDevice()
    {
        // 리소스 상태 추적은 이름에서 상태를 찾는 구조라 디바이스가 바뀌어도 살아남는다. 새 디바이스의 텍스처는 다시 Undefined 다.
        // 지우지 않으면 첫 프레임이 "이미 렌더 타깃 상태" 로 믿고 배리어를 건너뛴다.
        _graphContext.reset();
        _frameCmd.reset();
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
            SW_LOG_ERROR_ONCE( "%#: createCommandList returned null", pCallerName );
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

        _pCmd->beginCommandList();
        _bGPUCullingActive = SW_FALSE;
        _indirectDrawCallCount.store( 0, std::memory_order_relaxed );
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

        // GPU 드리븐 프리패스다. **애니메이션이 먼저고 컬링이 나중이다.** 순서가 뒤집히면 컬링이
        // 이번 프레임에 회전하기 전의 바운드로 판정한다. 컬링은 이번 프레임에 그리는 **모든 뷰**의 슬롯을 돈다.
        // 깊이 히스토리(뷰마다 지난 장면 깊이의 사본)가 먼저다 — GPU 입자의 깊이 충돌이 읽는다. 이번 프레임 패스가 장면 깊이를 쓰기 전이라 지난 깊이가 남아 있다.
        // 앞 프레임들의 셰이더 assert 기록이 도착했으면 로그로 낸다(몇 프레임 늦다).
        _gpuAssert.poll();
        _nanCheck.poll();
        recordDepthHistories();
        // GPU 방출기의 입자 시뮬레이션은 인스턴스 애니메이션 · 컬링 **앞**이다 — 메시 입자가 인스턴스 칸에 행렬 · 바운드를 쓰고 컬링이 그 바운드로 거른다.
        // 빌보드 · 띠는 투명 패스가 렌더 입력과 간접 인자를 읽는다(VFXGPUSimulation, vfxsimulate.hlsl · vfxmeshinstance.hlsl).
        _state._particleRenderer.recordSimulation( *_pCmd, _psoManager.getEnginePSO( RenderPassType::VFXSimulation ), _psoManager.getEnginePSO( RenderPassType::VFXMeshInstance ) );
        const uint32 animationInstanceCount = static_cast<uint32>( _state._gpuScene.getInstances().size() );
        dispatchInstanceAnimation( animationInstanceCount );
        dispatchMeshMorph();
        dispatchMeshSkin();
        dispatchCullAndSort( animationInstanceCount );
        // 라이트 그리드(GPU 생산자) — 조명을 계산하는 패스(포워드 · 디퍼드 · 투명)보다 먼저 끝나야 한다. 이 리스트는 그 패스들의 리스트보다 먼저 큐에 낸다.
        dispatchLightGrid();
        // 볼륨 안개 — 라이트 그리드를 읽으므로 그 디스패치 뒤다. 안개 패스(그래프)가 결과를 읽는다.
        _postProcess.dispatchVolumetricFog( *_pCmd, _state._lightBuffer, _transparentRankPlaceholder._srv );
        // 실시간 프로브 갱신 - 라이트 업로드 뒤, 조명 패스(그래프) 앞이다. 프로브 버퍼를 쓴다.
        dispatchRealtimeProbes();
        // 화면 마커 투영 — 캔버스 업로드(기록 전)가 올린 앵커 원소를 GPU 가 덮어쓴다. 주 출력 캔버스를 그리는 패스가 읽기 전이면 어디든 되지만 렌더 텍스처 대상 그리기와 같은 자리에 둔다.
        _state._canvasRenderer.recordMarkerProjection( *_pCmd, _psoManager.getEnginePSO( RenderPassType::CanvasMarker ), _state._gpuScene.getInstanceBuffer(), _state._gpuScene.getInstanceSRV(), _transparentRankPlaceholder._srv,
                                                       _gpuAssert, _state._mainView._settings._viewMode == RenderViewMode::MarkerSlots );
        // assert 가 켜진 디스패치(컬링 · 모프 · 마커)가 끝났다 — 링을 읽기 버퍼로 복사하는 명령을 넣는다.
        _gpuAssert.requestReadback( *_pCmd );
        // 월드 공간 UI 의 렌더 텍스처다. 장면 뷰가 그 텍스처를 읽기 전에 먼저 제출하는 프리패스 리스트(같은 큐)로 그린다.
        _state._canvasRenderer.drawTargets( *_pCmd, *pDevice, _state._canvasFrame, _psoManager.getCache() );
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
        // 그래프가 끝난 주 시점의 패스 출력을 NaN · Inf 감시 컴퓨트로 훑는다(`-gv_nanCheck` 일 때만).
        scanPassOutputsForNan( pDevice );

        // 화면 사각형 뷰(분할 화면, PiP)는 주 시점 위에 겹친다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::ScreenRect );
        // 호스트 타깃 뷰(에디터 씬 뷰)는 자기 RT 에 그린다. 이번 프레임에 그린 렌더 텍스처(CCTV)를 읽을 수 있도록 마지막에 그린다.
        renderedExtraCount += renderExtraViewsOfKind( pDevice, RenderViewOutputKind::HostTarget );
        _state._frameContext        = _state._mainSeedScratch;
        _lastRenderedExtraViewCount = renderedExtraCount;
        // 다음 프레임의 깊이 히스토리가 이번 프레임의 장면 깊이와 짝이 되도록 뷰-투영을 적는다.
        rememberDepthHistoryViews();
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

    bool FramePassExecutor::executeMainGraph( IRHIDevice* pDevice )
    {
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

        pCmd->beginCommandList();
        if ( _state._graph.execute( _graphContext, pCmd ) == false )
            SW_LOG_ERROR_ONCE( "Render graph could not run for an extra view (cycle or compile failure)" );
        // 렌더 텍스처는 이어서 주 시점의 머티리얼이, 호스트 타깃은 에디터 UI 가 읽는다 — 읽기 상태로 돌려 둔다(DX11 은 RTV 슬롯에서 뗀다).
        if ( view._outputKind == RenderViewOutputKind::RenderTexture && view._pOutputTexture != nullptr )
            pCmd->prepareTextureForShaderRead( view._pOutputTexture->getHandle() );
        else if ( view._outputKind == RenderViewOutputKind::HostTarget && view._hostTarget != 0 )
            pCmd->prepareTextureForShaderRead( view._hostTarget );
        pCmd->endCommandList();
        pDevice->executeCommandList( pCmd );

        _pActiveView = &_state._mainView;
    }

    bool FramePassExecutor::drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch ) const
    {
        // 변형 풀이 받지 못한 메시(상한 초과 · 버퍼 실패)는 어느 패스에서도 그리지 않는다 — 바인드 · 레스트 포즈로 대신 그리지 않는다(풀이 오류를 남겼다).
        if ( batch._bDeformRefused != SW_FALSE )
            return false;
        if ( FrameRendererUtil::filtersBatchesByMaterial( passType ) == false )
            return true;
        const GPUShaderPermutation* pPermutation =
            ( batch._shaderPermutation == kInvalidShaderPermutation ) ? nullptr : _state._gpuScene.findShaderPermutation( batch._shaderPermutation );
        return FrameRendererUtil::drawsMaterialInPass( passType, pPermutation != nullptr ? &pPermutation->_listDefine : nullptr );
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
        // 카메라별 후처리 값(CameraPostProcessValues)도 뷰마다다. 중립 값(노출 0, 배율 1, 비네트 · 레터박스 0)이면 셰이더 결과가 전역 값과 같다.
        const CameraPostProcessValues& post = view._settings._postProcess;
        float4                         bloomParams{ kDefaultBloomParams };
        bloomParams._y *= MathUtil::max( 0.0f, post._bloomScale );
        context._passValues.setFloat4( passConstantNames()._bloomParams, bloomParams );
        // 노출 배율 = 보정 2^EV × (자동 노출을 재지 않는 카메라의 고정 노출 2^-EV). 측정하는 카메라는 셰이더가 이번 EV(`g_AutoExposureIndex`)를 더 곱한다.
        float32 exposureScale = MathUtil::pow( 2.0f, post._exposureCompensation );
        _postProcess.prepareAutoExposure( view, _state.getAnimationTime(), exposureScale );
        context._passValues.setFloat4( passConstantNames()._cameraGrade, float4{ exposureScale, MathUtil::max( 0.0f, post._saturation ), MathUtil::max( 0.0f, post._contrast ),
                                                                                 MathUtil::saturate( post._vignetteIntensity ) } );
        const float32 viewAspect = pool.getHeight() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) : 0.0f;
        // 색각 보정은 화면 · 호스트 출력 뷰만 건다. 렌더 텍스처는 주 화면에 그려진 뒤 그 뷰의 톤맵에서 보정된다(두 번 걸지 않는다).
        const bool    bScreenOutput   = view._pOutputTexture == nullptr;
        const float32 colorVisionMode = bScreenOutput ? static_cast<float32>( _state._canvasFrame._colorVisionMode ) : 0.0f;
        context._passValues.setFloat4( passConstantNames()._cameraFrame, float4{ MathUtil::max( 0.0f, post._letterboxAspectRatio ), viewAspect, colorVisionMode, 0.0f } );
        // 피사계 심도(depthoffield.hlsl) — 얇은 렌즈의 착란원. 지름 / 센서 높이 = 센서 높이 · s² / ( 4 · N · 초점 거리 ) · |1 - 초점 거리 / 깊이|
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
        // 패스 플래그: 비트는 bindingslots.hlsli 의 SW_PASS_FLAG_*(C++ 는 shaderslot::kPassFlag*)가 기준이다. 후처리는 뷰마다 끌 수 있다(CCTV).
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
        _state.cullInputOf( view ).setViewProjection( viewProj );

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

    float4 FramePassExecutor::getTargetClearColor( string_view attachmentName ) const
    {
        // 첨부의 클리어 색은 파이프라인 XML 의 선언 하나가 원본이다(풀이 할당할 때 선언 값을 든다). 패스마다 코드가 따로 적던
        // 기본값(뎁스 1, 노멀 0.5 · 0.5 · 1, SSAO 흰색)은 선언과 갈라질 수 있어 지웠다. 풀에 없는 이름은 백버퍼(핸들 0)이고,
        // 백버퍼의 클리어 색은 렌더러의 클리어 색(SceneColor 선언에서 온다) 하나다.
        const TransientAttachmentPool::Attachment attachment = activePool().find( attachmentName );
        return attachment._texture != 0 ? attachment._clearColor : _state._clearColor;
    }

    TransientAttachmentPool::Attachment FramePassExecutor::findTransientAttachment( string_view name ) const
    {
        return activePool().find( name );
    }

    RHITextureHandle FramePassExecutor::findTransient( string_view name ) const
    {
        return activePool().findTexture( name );
    }

    string_view FramePassExecutor::resolvePresentSource() const
    {
        // Present 의 SourceColor 입력이 원본이다(계약의 필수 입력 — 로드가 선언을 요구한다). 이름 후보로 "가장 나중 컬러" 를 짐작하지 않는다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType != RenderPassType::Present )
                continue;
            for ( const RenderGraphPassDesc::ResolvedAttachment& input : pass._listResolvedInput )
            {
                if ( static_cast<RenderPassInputRole>( input._role ) == RenderPassInputRole::SourceColor && findTransient( input._attachment.view() ) != 0 )
                    return input._attachment.view();
            }
        }
        return string_view{};
    }
} // namespace sw
