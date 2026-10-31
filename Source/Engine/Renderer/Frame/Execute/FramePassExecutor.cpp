/**
 * @file FramePassExecutor.cpp
 * @brief 렌더 그래프 패스의 실행입니다 — 커맨드 리스트 준비 · 제출, 뷰별 그래프, 패스 종류마다의 기록(풀스크린 · 포워드 · G버퍼 · 투명 · 하늘 · 안개 · TAA · 출력 · 캔버스),
 *        씬 메시 드로우(GPU 배치 · 멀티 드로우), 모듈 확장 패스, 그래프 뒤 NaN 감시.
 * @details 그래프 앞의 프리패스는 `FramePrepass`, 가림 판정은 `OcclusionTiles` 가 맡는다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/FramePassExecutor.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/array.h"
#include "Core/Container/formatString.h"
#include "Core/Container/string.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/FourCcUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
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
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"
#include "Engine/Renderer/Frame/Capture/FrameCapture.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameFog.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"
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
        /// 피사계 심도 착란원 반지름의 상한(px)입니다. depth_of_field.hlsl 의 탭 16 개가 이 반지름 안에 퍼지므로, 더 키우면 탭 사이가 벌어져 고리 무늬가 보인다.
        constexpr float32 kMaxDepthOfFieldRadiusPixels = 8.0f;
        /// 센서 높이(mm, `CameraDepthOfFieldValues::_sensorHeight`)를 m 로 바꾸는 비율입니다.
        constexpr float32 kMillimetersPerMeter = 1000.0f;

        struct FramePassExecutorInternal
        {
            /**
             * @brief 선언한 컬러 출력에서 역할로 고르고, 그 역할을 받은 출력이 없으면 선언 순서(@p orderIndex)로 고릅니다.
             * @param pTaken 이미 다른 역할로 고른 출력(건너뛴다)입니다.
             */
            static const hashed_string* pickColorOutput( const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor, RenderPassInputRole role,
                                                         size_t orderIndex, const hashed_string* pTaken )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& output : declaredColor )
                {
                    if ( static_cast<RenderPassInputRole>( output._role ) == role )
                        return &output._attachment;
                }
                if ( orderIndex >= declaredColor.size() || &declaredColor[orderIndex]._attachment == pTaken )
                    return nullptr;
                return &declaredColor[orderIndex]._attachment;
            }

            /** @brief 선언한 입력 중 @p role 인 첫 첨부입니다. 그 역할이 없으면 nullptr 입니다. */
            static const RenderGraphPassDesc::ResolvedAttachment* findInputByRole( const RenderGraphPassDesc& passDesc, RenderPassInputRole role )
            {
                for ( const RenderGraphPassDesc::ResolvedAttachment& input : passDesc._listResolvedInput )
                {
                    if ( static_cast<RenderPassInputRole>( input._role ) == role )
                        return &input;
                }
                return nullptr;
            }
        };

        /**
         * @brief 패스 상수를 쓰고 소멸에서 되돌립니다. 이름은 엔진 패스 상수 칸(PassCB)의 이름이라 같은 컨텍스트의 뒤 드로우가 같은 칸을 읽습니다 — 쓰기 전의 값을 떠 두었다가
         *        되돌립니다(없던 칸은 지웁니다). 되돌리지 않으면 요소 하나가 빌린 칸의 값이 뒤 요소의 그림을 바꾼다.
         */
        template <typename TContext>
        class ExtensionConstantScope
        {
        public:
            explicit ExtensionConstantScope( TContext& context )
                : _context{ context }
            {
            }
            ~ExtensionConstantScope()
            {
                for ( const Saved& saved : _listSaved )
                {
                    if ( saved._bExisted )
                        _context._passValues.setFloat4( saved._name, saved._value );
                    else
                        _context._passValues.remove( saved._name );
                }
            }
            ExtensionConstantScope( const ExtensionConstantScope& )            = delete;
            ExtensionConstantScope& operator=( const ExtensionConstantScope& ) = delete;

            void apply( const ExtensionConstant* pConstant, uint32 count )
            {
                for ( uint32 index = 0; index < count; ++index )
                {
                    Saved saved;
                    saved._name = pConstant[index]._name;
                    uint32       valueSize{ 0 };
                    const uint8* pBytes = _context._passValues.find( saved._name, valueSize );
                    if ( pBytes != nullptr && valueSize == sizeof( float4 ) )
                    {
                        saved._bExisted = true;
                        Memory::copy( &saved._value, pBytes, sizeof( float4 ) );
                    }
                    _listSaved.push_back( saved );
                    _context._passValues.setFloat4( saved._name, pConstant[index]._value );
                }
            }

        private:
            struct Saved
            {
                hashed_string _name{};
                float4        _value{};
                bool          _bExisted{ false };
            };
            TContext&     _context;
            vector<Saved> _listSaved;
        };
    } // namespace

    FramePassExecutor::~FramePassExecutor() = default;

    FramePassExecutor::FramePassExecutor( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameRenderState& state, FramePSOManager& psoManager,
                                          FrameAutoExposure& autoExposure, FrameUpscale& upscale, FrameFog& fog, FrameExtensionHost& extensionHost, FrameCapture& capture )
        : _pDevice{ pDevice }
        , _pipelineResource{ pipeline }
        , _state{ state }
        , _psoManager{ psoManager }
        , _autoExposure{ autoExposure }
        , _upscale{ upscale }
        , _fog{ fog }
        , _extensionHost{ extensionHost }
        , _capture{ capture }
        , _prepass{ pDevice, state, psoManager, fog }
        , _pActiveView{ &state._mainView }
        , _pCmdOwnerDevice{ nullptr }
        , _frameCmd{}
        , _pCmd{ nullptr }
        , _listPassContext{}
        , _mapPassNameToIndex{}
        , _mapExtensionPassIndex{}
        , _graphContext{}
        , _nanCheck{}
        , _nanCheckCommandList{}
        , _pLastSwapchainWriter{ nullptr }
        , _pMainOutputCanvasView{ nullptr }
        , _indirectDrawCallCount{ 0 }
        , _disabledInputRoleMask{ 0 }
        , _lastRenderedExtraViewCount{ 0 }
        , _bMaterialFallbackMissingLogged{ 0 }
        , _bMissingColorTargetLogged{ 0 }
        , _bHasExecutedDepthPrepass{ 0 }
    {
    }

    void FramePassExecutor::ensureResources()
    {
        _prepass.ensureResources();
        if ( _nanCheck.initialize( _pDevice ) == false )
            SW_FATAL( "gv_nanCheck is on but the NaN check counters could not be created - see the log above" );
    }

    void FramePassExecutor::releaseResources()
    {
        _prepass.releaseResources();
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
        _nanCheck.poll();
        _prepass.record( *pDevice, *_pCmd );
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
        _prepass.rememberDepthHistoryViews();
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
        _autoExposure.prepareAutoExposure( view, _state.getAnimationTime(), exposureScale );
        context._passValues.setFloat4( passConstantNames()._cameraGrade, float4{ exposureScale, MathUtil::max( 0.0f, post._saturation ), MathUtil::max( 0.0f, post._contrast ),
                                                                                 MathUtil::saturate( post._vignetteIntensity ) } );
        const float32 viewAspect = pool.getHeight() > 0 ? static_cast<float32>( pool.getWidth() ) / static_cast<float32>( pool.getHeight() ) : 0.0f;
        // 색각 보정은 화면 · 호스트 출력 뷰만 건다. 렌더 텍스처는 주 화면에 그려진 뒤 그 뷰의 톤맵에서 보정된다(두 번 걸지 않는다).
        const bool    bScreenOutput   = view._pOutputTexture == nullptr;
        const float32 colorVisionMode = bScreenOutput ? static_cast<float32>( _state._canvasFrame._colorVisionMode ) : 0.0f;
        context._passValues.setFloat4( passConstantNames()._cameraFrame, float4{ MathUtil::max( 0.0f, post._letterboxAspectRatio ), viewAspect, colorVisionMode, 0.0f } );
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

    void FramePassExecutor::scanPassOutputsForNan( IRHIDevice* pDevice )
    {
        if ( _nanCheck.isActive() == false || pDevice == nullptr )
            return;
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( RenderPassType::NanCheck );
        if ( pso == 0 )
        {
            SW_LOG_ERROR_ONCE( "gv_nanCheck is on but the NaN check PSO is missing (shader not cooked?) - no output is scanned" );
            return;
        }
        if ( _nanCheckCommandList == nullptr )
            _nanCheckCommandList = pDevice->createCommandList();
        IRHICommandList* pCmd = _nanCheckCommandList.get();
        if ( pCmd == nullptr )
            return;

        pCmd->beginCommandList();
        vector<RHITextureHandle> listScanned; // 같은 텍스처를 가리키는 출력은 한 번만 훑는다
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const RenderGraphPassDesc::ResolvedAttachment& output : pass._listResolvedColorOutput )
            {
                const TransientAttachmentPool::Attachment attachment = _state._mainView._transientPool.find( output._attachment.view() );
                const bool                                bScannable = attachment._texture != 0 && attachment._srv != kInvalidDescriptorIndex && GPUNanCheck::isScannableFormat( attachment._format );
                if ( bScannable == false || VectorUtil::contains( listScanned, attachment._texture ) )
                    continue;
                listScanned.push_back( attachment._texture );

                utf8 arrName[constant::kMaxBuffer64];
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

    /**
     * @brief `-gv_dumpRenderGraph=1` 이면 파이프라인을 연결할 때마다(시작, 파이프라인 교체) 컴파일된 레벨 순서를 로그로 남깁니다.
     * @details 레벨, 패스, 읽고 쓰는 자원, 컬링된 패스를 보여 준다(`RenderGraph::describeCompiledOrder`). "왜 이 패스가 저것보다 먼저 도나", "왜 안 도나" 에 답한다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_dumpRenderGraph, false, "렌더 그래프를 컴파일할 때마다 레벨 · 패스 · 읽고 쓰는 자원을 로그로 남긴다" );

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
            const RenderPassTypeInfo& anchorInfo = getRenderPassTypeInfo( listPass[anchorIndex]._resolvedType );
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
            SW_LOG_ERROR( "Callback bind compile failed" );
            return false;
        }
        if ( gv_dumpRenderGraph )
            SW_LOG_INFO( "[gv_dumpRenderGraph]\n%#", _state._graph.describeCompiledOrder().c_str() );
        return true;
    }

    void FramePassExecutor::onGraphLevelPrologue( const RenderGraphLevelContext& levelContext )
    {
        if ( _pDevice == nullptr || levelContext._pCmdList == nullptr )
            return;

        if ( levelContext._pListBarrier == nullptr )
            return;

        // 그래프가 **실제로 바뀌는 전이만** 추려서 준다. 여기서는 이름으로 텍스처를 찾아 그대로 바인딩한다.
        // 같은 자원을 여러 패스가 읽어도, 이미 맞는 상태여도 다시 바인딩하지 않는다.
        // 꺼진 효과의 출력 이름은 원본을 가리키지만(`TransientAttachmentPool::forward`) 전이는 그 이름으로 만든 텍스처에만 낸다 — 따라가면 꺼진 패스의 "쓰기" 가
        // 같은 레벨에서 원본을 읽는 패스와 부딪힌다. 원본의 읽기 상태는 원본을 입력으로 선언한 그 효과 패스의 레벨이 이미 냈고, 뒤에서 원본을 쓰는 패스는 로드 검증이 막는다.
        for ( const RenderGraphBarrier& barrier : *levelContext._pListBarrier )
        {
            if ( barrier._after == RenderGraphResourceState::Write )
            {
                // 스왑체인은 전용 경로가 있다(핸들 0). 이름으로는 트랜지언트에서 찾을 수 없다.
                if ( barrier._resource == attachmentNames()._swapchain )
                {
                    levelContext._pCmdList->prepareTextureForRenderTarget( 0 );
                    continue;
                }
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelContext._pCmdList->prepareTextureForRenderTarget( texture );
            }
            else if ( barrier._after == RenderGraphResourceState::Read )
            {
                const RHITextureHandle texture = activePool().findOwnTexture( barrier._resource.c_str() );
                if ( texture != 0 )
                    levelContext._pCmdList->prepareTextureForShaderRead( texture );
            }
        }
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
            if ( activePool().resolveName( input._attachment.view() ).empty() )
                continue;
            registerPassTexture( context, inputRoleName( role ), input._attachment.view() );
        }
    }

    // 일반 풀스크린 패스는 테이블의 플래그로 고르고, 전용 실행 코드가 있는 패스만 아래 switch 의 case 를 가진다(짧은 디스패처라 나누지 않는다).
    // 열거자를 추가하고 case 를 빠뜨리면 -Wswitch-enum 이 이 함수를 알려 준다.
    void FramePassExecutor::executePass( FramePassContext& context, const RenderGraphPassDesc& passDesc )
    {
        SW_PROFILE_SCOPE( "RT.Pass.execute" );
        // 타입은 로드 시점에 한 번 해석해 둔 값을 쓴다. 여기서 문자열을 다시 비교하면 디스패치와 PSO 생성이 서로 다른 표기를 받아 줄 여지가 생긴다.
        const RenderPassType passType        = passDesc._resolvedType;
        const string_view    passName        = string_view( passDesc._name );
        const hashed_string& depthAttachment = passDesc._resolvedDepthAttachment;

        if ( context._pCmd == nullptr )
        {
            SW_LOG_ERROR_ONCE( "executePass: no active IRHICommandList" );
            return;
        }

        if ( passName.empty() == false )
        {
            utf8         arrPassName[constant::kMaxBuffer64];
            const size_t copyLen = ( passName.size() < sizeof( arrPassName ) - 1 ) ? passName.size() : ( sizeof( arrPassName ) - 1 );
            Memory::copy( arrPassName, passName.data(), copyLen );
            arrPassName[copyLen] = '\0';
            // GPU 브레드크럼 - 디바이스 소실 때 마지막으로 닿은 패스 이름을 싣는다(꺼져 있으면 아무것도 하지 않는다).
            GPUBreadcrumbs::mark( *context._pCmd, string_view( arrPassName, copyLen ) );
            context._pCmd->beginEventMarker( arrPassName );
        }
        context._resourceRegistry.reset();
        context._passType = passType;
        // 라이트는 **패스 종류를 가리지 않는다.** 포워드 지오메트리도, 디퍼드 풀스크린 조명도 읽는다.
        // 그래서 인스턴스 버퍼(지오메트리 패스 전용)와 달리 여기서 모든 패스에 바인딩한다.
        registerLightBuffer( context );

        // 이 패스가 바인딩할 뎁스다. 파이프라인 XML 의 `_depthAttachment` 가 기준이고, 비어 있으면
        // 뎁스 없이 연다. "이 패스는 일부러 뎁스를 쓰지 않는다" 를 선언으로 표현할 수 있어야 한다.
        // 선언한 이름이 이번 프레임에 실제로 없으면(트랜지언트가 할당되지 않았으면) 뎁스 없이 진행한다.
        const hashed_string passDepth = ( depthAttachment.empty() == false && findTransient( depthAttachment.view() ) != 0 )
                                          ? depthAttachment
                                          : hashed_string{};

        // 컬러 타깃은 파이프라인이 선언한 컬러 출력이다(로드 때 해석한 `_listResolvedColorOutput`, 선언 순서). 컬러를 그리는 패스(kDrawsDeclaredColor)는
        // 로드가 선언을 요구하므로 비어 있지 않다. 이름(SceneColor · GBufferAlbedo · TAAColor 등)으로 짐작하지 않는다 — 다른 이름을 쓰는
        // 파이프라인에서 없는 첨부(핸들 0 은 백버퍼)를 열었다. 뎁스 로드 연산은 바인딩한 뎁스의 클리어 기록으로 정한다.
        const vector<RenderGraphPassDesc::ResolvedAttachment>& declaredColor = passDesc._listResolvedColorOutput;

        const RenderPassTypeInfo& info = getRenderPassTypeInfo( passType );
        // 로드가 막았어야 할 상태다(검증 3-1). 여기서는 그리지 않고 끝낸다 — 백버퍼에 그리는 것보다 낫다.
        const bool bMissingDeclaredColor = info.hasFlag( RenderPassTraitFlag::kDrawsDeclaredColor ) && declaredColor.empty();
        SW_ASSERT( bMissingDeclaredColor == false );
        if ( bMissingDeclaredColor )
        {
            context._pCmd->endEventMarker(); // 함수 끝과 같은 짝
            return;
        }
        const PassExecuteInput input{ context, passType, passDesc, declaredColor, passDepth, info.hasFlag( RenderPassTraitFlag::kDrawsTransparentBatch ) };
        if ( info.hasFlag( RenderPassTraitFlag::kGenericFullscreen ) )
        {
            executeGenericFullscreenPass( input );
        }
        else
        {
            switch ( passType )
            {
                case RenderPassType::Shadow:
                {
                    // 클리어 값은 실제로 바인딩하는 뎁스 첨부의 선언에서 읽는다(이름 ShadowMap 으로 찾지 않는다).
                    const float4 clearVal = getTargetClearColor( passDepth.view() );
                    beginDepthOnlyPass( context, passDepth.view(), clearVal._x, colorLoadFor( passDepth ) );
                    // 그림자를 끈 뷰는 지우기만 한다. 깊이 1(가장 멀다)이면 모두 빛을 받는다.
                    if ( _pActiveView->_settings._bShadows == SW_TRUE )
                        drawShadowTiles( context, input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    break;
                }
                case RenderPassType::DepthPrepass:
                {
                    const float4 clearVal = getTargetClearColor( passDepth.view() );
                    beginDepthOnlyPass( context, passDepth.view(), clearVal._x, colorLoadFor( passDepth ) );
                    drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassType::DepthPrepass ), input._bTransparentBatch );
                    context._pCmd->endRenderPass();
                    _bHasExecutedDepthPrepass.store( 1 );
                    break;
                }
                case RenderPassType::ForwardOpaque:
                case RenderPassType::ForwardOnlyOpaque:
                {
                    executeForwardOpaquePass( input );
                    break;
                }
                case RenderPassType::GBuffer:
                {
                    executeGBufferPass( input );
                    break;
                }
                case RenderPassType::Transparent:
                {
                    executeTransparentPass( input );
                    break;
                }
                case RenderPassType::SceneColorCopy:
                {
                    executeSceneColorCopyPass( input );
                    break;
                }
                case RenderPassType::MeshOutline:
                {
                    executeMeshOutlinePass( input );
                    break;
                }
                case RenderPassType::HighlightOutline:
                case RenderPassType::HighlightSense:
                {
                    executeHighlightPass( input );
                    break;
                }
                case RenderPassType::Sky:
                {
                    executeSkyPass( input );
                    break;
                }
                case RenderPassType::Fog:
                {
                    executeFogPass( input );
                    break;
                }
                case RenderPassType::TAA:
                {
                    executeTAAPass( input );
                    break;
                }
                case RenderPassType::Present:
                {
                    executePresentPass( input );
                    break;
                }
                case RenderPassType::Canvas:
                {
                    executeCanvasPass( input );
                    break;
                }
                // 실행 코드가 없는 타입이다. 패스 서술이 없는 일반 풀스크린 패스이거나, PSO 슬롯만 있는 엔진 내부 타입이다.
                case RenderPassType::Invalid:
                case RenderPassType::Lighting:
                case RenderPassType::SSAO:
                case RenderPassType::Bloom:
                case RenderPassType::BloomDownsample:
                case RenderPassType::Outline:
                case RenderPassType::Tonemap:
                case RenderPassType::DepthOfField:
                case RenderPassType::MotionBlur:
                case RenderPassType::SceneDepthCopy:
                case RenderPassType::ForwardOpaqueNoDepthWrite:
                case RenderPassType::GPUCull:
                case RenderPassType::InstanceAnimation:
                case RenderPassType::InstanceSort:
                case RenderPassType::MeshMorph:
                case RenderPassType::MeshSkin:
                case RenderPassType::LightGrid:
                case RenderPassType::VFXSimulation:
                case RenderPassType::VFXMeshInstance:
                case RenderPassType::DepthHistory:
                case RenderPassType::AutoExposure:
                case RenderPassType::UpscaleEasu:
                case RenderPassType::UpscaleRcas:
                case RenderPassType::OcclusionTiles:
                case RenderPassType::VolumetricFog:
                case RenderPassType::CanvasMarker:
                case RenderPassType::NanCheck:
                case RenderPassType::ProbeUpdate:
                {
                    SW_LOG_WARNING( "Unknown pass type '%#' in '%#'", passType, passName );
                    break;
                }
            }
        }

        context._pCmd->endEventMarker();
    }

    RHIRenderPassLoadOp FramePassExecutor::colorLoadFor( const hashed_string& name )
    {
        // 이름은 **이미 intern 된 것**만 받는다. string_view 를 받으면 패스마다 여기서 다시 intern 하게
        // 된다(FNV + 32-way 샤드 뮤텍스). 타깃 이름은 모두 코드 리터럴이라 attachmentNames() 캐시로 충분하다.
        return markAttachmentCleared( name ) ? RHIRenderPassLoadOp::Clear : RHIRenderPassLoadOp::Load;
    }

    void FramePassExecutor::executeFullscreenInto( FramePassContext& context, RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor )
    {
        if ( beginColorPass( context, targetName.view(), "", passClearColor, colorLoadFor( targetName ), RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, pso );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeGenericFullscreenPass( const PassExecuteInput& input )
    {
        // 일반 풀스크린 패스(테이블의 kGenericFullscreen: Lighting, SSAO, Bloom, BloomDownsample, Outline, Tonemap)다. **선언이 곧 바인딩**이다.
        // 입력은 역할 이름으로 모두 바인딩하고, 타깃은 선언한 첫 컬러 출력이다. 계약(RenderPassInputSignature)이
        // 로드 시점에 같은 목록을 검사했다. 새 포스트 패스는 테이블의 한 줄로 여기를 탄다.
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const hashed_string*       pTarget  = &input._declaredColor[0]._attachment;
        // 이 뷰에서 빠진 패스는 셋업이 출력 이름을 원본에 잇거나 비웠다(`applyViewPassSelection` — 꺼진 효과, 읽는 패스가 모두 빠진 패스). 아무것도 기록하지 않는다.
        // 후처리를 끈 뷰 · 품질로 꺼지는 효과(kSkipsWithoutPostProcess)도 모두 이 길이다 — 원본을 복사하거나 셰이더가 원본을 고르던 길은 지웠다.
        if ( activePool().isForwarded( pTarget->view() ) )
            return;
        registerDeclaredInputs( context, passDesc );
        _autoExposure.recordAutoExposure( context, *_pActiveView, passDesc );

        // PSO 가 0이면 `drawFullscreen` 이 파이프라인 설정을 건너뛴다. 대체할 PSO 는 테이블이 정한 것만 쓴다(Tonemap 은 Present).
        const RHIPipelineStateHandle pso = _psoManager.getEnginePSO( input._passType );

        // 클리어 색은 타깃 첨부의 선언이다(SSAO 의 흰색 = 가림 없음도 AOColor 선언에 있다).
        const float4 targetClear = getTargetClearColor( pTarget->view() );
        executeFullscreenInto( context, pso, *pTarget, targetClear );
    }

    void FramePassExecutor::executeForwardOpaquePass( const PassExecuteInput& input )
    {
        // ForwardOnlyOpaque 는 디퍼드의 G버퍼가 뺀 배치(MATERIAL_FORWARD_ONLY — 툰)만 조명 패스가 쓴 색과 장면 깊이 위에 그린다(거르기는 drawsBatchInPass).
        // 첨부는 앞 패스가 이미 썼으므로 colorLoadFor 가 Load 를 고른다.
        // 그림자 맵은 선언한 입력 중 ShadowMap 역할이다(첨부의 `_role` 또는 원본 이름). 선언하지 않은 파이프라인(2D)은 그림자를 바인딩하지 않는다
        // — 옛 코드는 이름 ShadowMap 으로 찾았고, 그 첨부가 없는 파이프라인에서는 어차피 빈 바인딩이었다.
        using Internal                                              = FramePassExecutorInternal;
        FramePassContext&                              context      = input._context;
        const hashed_string&                           passDepth    = input._passDepth;
        const RenderGraphPassDesc::ResolvedAttachment* pShadowInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::ShadowMap );
        if ( pShadowInput != nullptr )
            registerPassTexture( context, attachmentNames()._shadowMap, pShadowInput->_attachment.view() );
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        const float4         sceneClear  = getTargetClearColor( colorTarget.view() );
        if ( beginColorPass( context, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        const bool bDepthAlreadyWritten = input._passType == RenderPassType::ForwardOpaque && _bHasExecutedDepthPrepass.load() != 0 &&
                                          _psoManager.getEnginePSO( RenderPassType::ForwardOpaqueNoDepthWrite ) != 0;
        const RHIPipelineStateHandle psoForward = bDepthAlreadyWritten ? _psoManager.getEnginePSO( RenderPassType::ForwardOpaqueNoDepthWrite ) : _psoManager.getEnginePSO( input._passType );
        drawSceneMeshes( context, psoForward, input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeGBufferPass( const PassExecuteInput& input )
    {
        // 알베도 · 노멀 · 머티리얼은 역할로 고른다(첨부의 `_role` 또는 원본 이름). 역할이 없으면 선언 순서([0] 알베도, [1] 노멀, [2] 머티리얼)를 따른다.
        // 한 MRT 패스로 셋 다 쓴다(네 백엔드 모두 MRT 를 보장한다). 컬러 출력 수가 표의 값이 아닌 G버퍼는 검증(3-2)이 거부한다.
        using Internal                       = FramePassExecutorInternal;
        FramePassContext&    context         = input._context;
        const hashed_string& passDepth       = input._passDepth;
        const hashed_string* pAlbedoTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferAlbedo, 0, nullptr );
        const hashed_string* pNormalTarget   = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferNormal, 1, pAlbedoTarget );
        const hashed_string* pMaterialTarget = Internal::pickColorOutput( input._declaredColor, RenderPassInputRole::GBufferMaterial, 2, pNormalTarget );
        const bool           bHasTargets     = pAlbedoTarget != nullptr && pNormalTarget != nullptr && findTransient( pNormalTarget->view() ) != 0 &&
                                 pMaterialTarget != nullptr && findTransient( pMaterialTarget->view() ) != 0;
        if ( bHasTargets == false || _psoManager.getEnginePSO( RenderPassType::GBuffer ) == 0 )
            return;
        const hashed_string&      albedoTarget = *pAlbedoTarget;
        const string_view         arrNames[]   = { albedoTarget.view(), pNormalTarget->view(), pMaterialTarget->view() };
        const float4              arrClears[]  = { getTargetClearColor( albedoTarget.view() ), getTargetClearColor( pNormalTarget->view() ),
                                                   getTargetClearColor( pMaterialTarget->view() ) };
        const RHIRenderPassLoadOp arrLoads[]   = { colorLoadFor( albedoTarget ), colorLoadFor( *pNormalTarget ), colorLoadFor( *pMaterialTarget ) };
        if ( beginColorPassMrt( context, arrNames, arrClears, arrLoads, 3, passDepth.view(), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassType::GBuffer ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeTransparentPass( const PassExecuteInput& input )
    {
        // 타깃은 선언한 컬러 출력이다. 원본 색 입력(SourceColor 역할)을 다른 첨부로 선언했으면 그 위에 섞도록 먼저 복사한다
        // — 옛 코드는 타깃 이름이 TransparentColor 일 때 LitColor · SceneColor 이름으로 원본을 짐작했다.
        FramePassContext&    context     = input._context;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        for ( const RenderGraphPassDesc::ResolvedAttachment& sourceInput : input._passDesc._listResolvedInput )
        {
            const bool bSeparateSource = static_cast<RenderPassInputRole>( sourceInput._role ) == RenderPassInputRole::SourceColor && sourceInput._attachment != colorTarget;
            if ( bSeparateSource == false )
                continue;
            const RHITextureHandle src = findTransient( sourceInput._attachment.view() );
            const RHITextureHandle dst = findTransient( colorTarget.view() );
            if ( src != 0 && dst != 0 )
            {
                context._pCmd->blitTexture( src, dst );
                (void)markAttachmentCleared( colorTarget );
            }
            break;
        }

        // 장면 색 사본(SceneColorCopy 패스가 반투명 앞에 떠 둔 것)을 선언했으면 역할로 건다 — 물의 굴절이 읽는다. 선언하지 않은 파이프라인은 걸지 않고 셰이더는 kInvalidIndex 로 안다.
        using Internal                   = FramePassExecutorInternal;
        const auto* pSceneColorCopyInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SceneColorCopy );
        if ( pSceneColorCopyInput != nullptr )
            registerPassTexture( context, attachmentNames()._sceneColorCopy, pSceneColorCopyInput->_attachment.view() );
        // 장면 깊이 사본(SceneDepthCopy 패스 출력)도 같다 — 물의 화면 공간 두께가 읽는다. 반투명은 장면 깊이를 깊이 붙임으로 쥐고 있어 그 깊이를 샘플할 수 없다.
        const auto* pSceneDepthCopyInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SceneDepthCopy );
        if ( pSceneDepthCopyInput != nullptr )
            registerPassTexture( context, attachmentNames()._sceneDepthCopy, pSceneDepthCopyInput->_attachment.view() );

        if ( beginColorPass( context, colorTarget.view(), input._passDepth.view(), _state._clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        // 메시 투명 배치와 입자(방출기 하나 = 후보 하나)를 그 뷰의 한 순서로 섞어 그린다(GPUSceneBuilder::populateParticleBatches · computeViewTransparentOrders).
        SW_ASSERT( input._bTransparentBatch );
        drawTransparentQueue( context, _psoManager.getEnginePSO( RenderPassType::Transparent ) );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeSceneColorCopyPass( const PassExecuteInput& input )
    {
        // 원본 색(SourceColor 역할 입력)을 선언한 컬러 출력으로 그대로 복사한다 — 셰이더도 PSO 도 없다. 반투명 패스는 장면 색에 섞으며 쓰므로 그 색을 읽을 수 없다(물의 굴절).
        // 둘은 같은 크기 · 형식이어야 한다(파이프라인이 같은 형식으로 선언한다). 계약(`RenderPassTypeInfo`)이 SourceColor 를 필수로 검사했다.
        using Internal                                              = FramePassExecutorInternal;
        const RenderGraphPassDesc::ResolvedAttachment* pSourceInput = Internal::findInputByRole( input._passDesc, RenderPassInputRole::SourceColor );
        SW_ASSERT( pSourceInput != nullptr );
        if ( pSourceInput == nullptr )
            return;
        const hashed_string&   copyTarget = input._declaredColor[0]._attachment;
        const RHITextureHandle src        = findTransient( pSourceInput->_attachment.view() );
        const RHITextureHandle dst        = findTransient( copyTarget.view() );
        if ( src == 0 || dst == 0 )
            return;
        input._context._pCmd->blitTexture( src, dst );
        (void)markAttachmentCleared( copyTarget );
    }

    void FramePassExecutor::executeMeshOutlinePass( const PassExecuteInput& input )
    {
        // 인버티드 헐(inverted hull) 외곽선이다. 불투명 패스가 그린 색과 깊이 위에, 외곽선을 켠 머티리얼의 배치만 앞면을 컬링해 그린다(drawsBatchInPass).
        // 와이어프레임과 표면 값 보기(Normals, Depth, Overdraw)에서는 빠진다(껍질이 선과 값 위를 덮는다). 타깃은 선언한 컬러 출력이다(불투명 패스와 같은 규칙).
        const RenderViewModeInfo& viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer ) // 겹침(ShadedWireframe)은 Lit 그림이라 그린다
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& passDepth   = input._passDepth;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        const float4         sceneClear  = getTargetClearColor( colorTarget.view() );
        if ( beginColorPass( context, colorTarget.view(), passDepth.view(), sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, _psoManager.getEnginePSO( RenderPassType::MeshOutline ), input._bTransparentBatch );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeHighlightPass( const PassExecuteInput& input )
    {
        // 인스턴스 강조 외곽선이다(highlight_outline.hlsl). 불투명 배치를 모두 그리되 이 패스의 강조 방식인 인스턴스만 정점이 남는다 —
        // 배치를 CPU 에서 거르지 않으므로 강조를 켜고 꺼도 배치가 다시 나뉘지 않는다. 와이어프레임과 표면 값 보기에서는 빠진다(메시 외곽선과 같다).
        const RenderViewModeInfo& viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        if ( ( viewModeInfo._bWireframe && viewModeInfo._bOverlay == false ) || viewModeInfo._bShowsBuffer )
            return;
        FramePassContext&            context     = input._context;
        const hashed_string&         passDepth   = input._passDepth;
        const hashed_string&         colorTarget = input._declaredColor[0]._attachment;
        const float4                 sceneClear  = getTargetClearColor( colorTarget.view() );
        const RHIPipelineStateHandle pso         = _psoManager.getEnginePSO( input._passType );
        // 렌더 패스의 깊이 붙임은 PSO 의 깊이 형식을 따른다 — 깊이 테스트를 끈 감각 패스의 PSO 는 깊이가 없고, 둘이 다르면 Vulkan 은 그리지 않는다.
        RHIPipelineStateDesc psoDesc{};
        const bool           bUsesDepth = _psoManager.findDesc( pso, psoDesc ) && psoDesc._depthStencilFormat != RHIFormat::Unknown;
        const string_view    depthName  = bUsesDepth ? passDepth.view() : string_view{};
        if ( beginColorPass( context, colorTarget.view(), depthName, sceneClear, colorLoadFor( colorTarget ), colorLoadFor( passDepth ) ) == false )
            return;
        drawSceneMeshes( context, pso, false );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeSkyPass( const PassExecuteInput& input )
    {
        // 하늘이다. 깊이 1 의 풀스크린 삼각형을 깊이 테스트(LessEqual, 쓰기 없음)로 그려 아무것도 그리지 않은 곳만 칠한다(sky.hlsl).
        // 씬에 하늘이 없으면 그리지 않는다 — 배경은 컬러 첨부의 클리어 색이다. 와이어프레임과 표면 값 보기에서도 빠진다(값 위를 덮는다).
        // 뎁스나 선언한 컬러 출력이 없으면 그리지 않는다(장면을 통째로 덮거나 백버퍼에 그린다 — 그런 파이프라인은 검증이 거부한다).
        // 앞의 불투명 패스가 색과 깊이를 이미 썼으므로 둘 다 불러온다(지우지 않는다).
        const RenderViewModeInfo&    viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle skyPSO       = _psoManager.getEnginePSO( RenderPassType::Sky );
        const bool                   bShowsSky    = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
        const bool                   bHasTargets  = input._passDepth.empty() == false;
        const bool                   bDrawsSky    = _state._bHasFrameSky == SW_TRUE && bHasTargets && skyPSO != 0 && bShowsSky;
        if ( bDrawsSky == false )
            return;
        FramePassContext&    context     = input._context;
        const hashed_string& colorTarget = input._declaredColor[0]._attachment;
        if ( beginColorPass( context, colorTarget.view(), input._passDepth.view(), _state._clearColor, RHIRenderPassLoadOp::Load, RHIRenderPassLoadOp::Load ) == false )
            return;
        drawFullscreen( context, skyPSO );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeFogPass( const PassExecuteInput& input )
    {
        // 안개다. 장면 깊이로 광학 두께를 구해 선언한 컬러 출력(앞 패스가 칠한 색) 위에 프리멀티플라이 블렌드한다(fog.hlsl).
        // 씬에 안개가 없거나 표면 값 보기(Normals, Depth, Overdraw)와 와이어프레임에서는 그리지 않는다 — 값 위를 덮는다. 후처리를 끈 뷰(CCTV)도 안개는 그린다(장면의 일이다).
        // 출력이 곧 입력의 위(블렌드)라 "그리지 않음" 은 아무것도 하지 않는 것이다 — 일반 풀스크린 패스의 복사 · 지우기 길을 타면 장면이 지워진다.
        const RenderViewModeInfo&    viewModeInfo = getRenderViewModeInfo( _pActiveView->_settings._viewMode );
        const RHIPipelineStateHandle pso          = _psoManager.getEnginePSO( RenderPassType::Fog );
        const bool                   bShowsScene  = ( viewModeInfo._bWireframe == false || viewModeInfo._bOverlay ) && viewModeInfo._bShowsBuffer == false;
        const bool                   bDraws       = _fog.hasFrameFog() && pso != 0 && bShowsScene;
        if ( bDraws == false )
            return;

        FramePassContext&    context = input._context;
        const hashed_string& target  = input._declaredColor[0]._attachment;
        registerDeclaredInputs( context, input._passDesc );

        // 볼륨은 컴퓨트가 돈 프레임의 주 시점에서만 읽는다(추가 뷰는 볼륨을 계산하지 않는다). 상수는 이 패스의 복사본에만 쓴다.
        const bool bVolume      = _fog.isFogVolumeActive() && _pActiveView == &_state._mainView && _fog.getFogVolumeBuffer()._srv != kInvalidDescriptorIndex;
        float4     volumeParams = _fog.getFrameFog()._volumeParams;
        volumeParams._x         = bVolume ? 1.0f : 0.0f;
        context._passValues.setFloat4( passConstantNames()._fogVolumeParams, volumeParams );

        if ( beginColorPass( context, target.view(), "", getTargetClearColor( target.view() ), colorLoadFor( target ), RHIRenderPassLoadOp::Load ) == false )
            return;
        // drawFullscreen 과 같은 순서다. 볼륨 버퍼는 PSO 와 공통 바인딩(bindForDraw) 뒤에 건다 — setPipelineState 가 슬롯 상태를 비운다.
        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
        context._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        context._pCmd->setPipelineState( pso );
        bindForDraw( context, pso, kInvalidDescriptorIndex );
        if ( bVolume )
            context._pCmd->bindStructuredBuffer( _fog.getFogVolumeBuffer()._srv, shaderslot::kPassBuffer );
        context._pCmd->draw( 3, 0 );
        context._pCmd->endRenderPass();
    }

    void FramePassExecutor::executeTAAPass( const PassExecuteInput& input )
    {
        // 타깃은 선언한 첫 컬러 출력이다(풀스크린 패스와 같은 규칙이며 히스토리도 같은 규칙으로 만든다. ensureTAAHistory).
        FramePassContext&      context   = input._context;
        const AttachmentNames& names     = attachmentNames();
        const hashed_string&   taaTarget = input._declaredColor[0]._attachment;
        const utf8*            pSrcName{ nullptr };
        // 후처리를 끈 뷰는 셋업이 출력 이름을 원본에 이었다(`applyViewPassSelection`, kBypassesToSource). 기록하지 않고 히스토리도 건드리지 않는다
        // — 원본을 TAAColor 로 복사하거나 셰이더로 옮기던 길은 지웠다. 다시 켠 첫 프레임은 `_bTAAHistoryReset` 이 기록을 버린다.
        if ( activePool().isForwarded( taaTarget.view() ) )
            return;
        registerDeclaredInputs( context, input._passDesc );
        for ( const RenderGraphPassDesc::ResolvedAttachment& sourceInput : input._passDesc._listResolvedInput )
        {
            if ( static_cast<RenderPassInputRole>( sourceInput._role ) == RenderPassInputRole::SourceColor )
                pSrcName = sourceInput._attachment.c_str();
        }
        // 히스토리 생성과 bindless 등록은 ensureTAAHistory() 가 셋업 단계에서 끝냈다. 이 콜백은
        // 병렬 기록에서 태스크 스레드가 실행하므로 여기서 레지스트리를 건드리면 안 된다. 히스토리는 뷰마다 하나다.
        const FrameViewTarget& activeView = *_pActiveView;
        // 기록을 버리는 프레임(처음 그리는 프레임 · 컷 · 후처리를 다시 켠 첫 프레임 — `FrameViewTarget::_bTAAHistoryReset`)에서는 지난 화면의 히스토리를 버린다. 이번 프레임은 히스토리 슬롯에 **이번 원본**을
        // 바인딩해 섞어도 이번 이미지만 남게 한다. 원본을 히스토리 텍스처에 복사하지 않는 것은 포맷이 다를 수 있어서다(원본 R8G8B8A8, 히스토리 R16G16B16A16이며
        // D3D 의 복사는 같은 포맷만 받는다). 패스 끝의 복사(TAA 출력에서 히스토리로)가 히스토리를 새로 채운다.
        const bool bCut = activeView._bTAAHistoryReset == SW_TRUE && pSrcName != nullptr;
        if ( bCut )
            registerPassTexture( context, names._gbufferAlbedo, pSrcName );
        else if ( activeView._taaHistory != 0 )
            context._resourceRegistry.registerTexture( names._gbufferAlbedo, activeView._taaHistory, activeView._taaHistorySRV );
        if ( beginColorPass( context, taaTarget.view(), "", _state._clearColor, colorLoadFor( taaTarget ), RHIRenderPassLoadOp::Load ) )
        {
            // TAA PSO 가 없으면(셋업이 알렸다) 원본을 복사해 TAA 없이 넘기지 않는다 — drawFullscreen 이 그리지 않는다.
            drawFullscreen( context, _psoManager.getEnginePSO( RenderPassType::TAA ) );
            context._pCmd->endRenderPass();
        }

        const RHITextureHandle taaOut              = findTransient( taaTarget.view() );
        const bool             bTAAHistoryWritable = taaOut != 0 && activeView._taaHistory != 0;
        if ( bTAAHistoryWritable )
            context._pCmd->blitTexture( taaOut, activeView._taaHistory );
    }

    void FramePassExecutor::executePresentPass( const PassExecuteInput& input )
    {
        FramePassContext&          context  = input._context;
        const RenderGraphPassDesc& passDesc = input._passDesc;
        const string_view          srcName  = resolvePresentSource();
        const RHITextureHandle     src      = srcName.empty() ? 0 : findTransient( srcName );
        // 출력은 뷰가 정한다(resolvePresentTarget. Canvas 와 같은 판단). 스크린샷 실행이면 백버퍼 대신 캡처 텍스처에 그리고,
        // Swapchain 을 쓰는 마지막 패스(보통 Canvas) 끝에서 백버퍼로 복사한다. 여기서 복사하면 뒤의 UI 가 캡처에 없다.
        const FrameViewTarget& activeView     = *_pActiveView;
        const PresentTarget    target         = resolvePresentTarget();
        const bool             bOwnOutput     = target._bOwnOutput == SW_TRUE;
        const bool             bCaptureToBack = target._bCaptureToBack == SW_TRUE && isLastSwapchainWriter( passDesc );
        const RHITextureHandle dstTarget      = target._texture;
        const uint32           outputWidth    = target._width;
        const uint32           outputHeight   = target._height;
        // 정수 업스케일(픽셀 퍼펙트)은 주 출력에만 있다 — 원본(사각형 픽셀 / 배율)을 배율만큼 키워 사각형 가운데에 놓으므로 사각형 전체가 아니다.
        const uint32 upscaleZoom = bOwnOutput ? 0u : static_cast<uint32>( activeView._settings._upscaleZoom );
        const bool   bFullRect   = ( bOwnOutput || activeView._settings.isFullRect() ) && upscaleZoom == 0;
        // 주 시점이 사각형 하나만 쓰면 바깥은 지운다(업스케일의 남는 띠는 검게). 화면 사각형 뷰는 주 시점 위에 겹치므로 남긴다.
        const RHIRenderPassLoadOp outputLoad  = ( isRenderingExtraView() && bOwnOutput == false ) ? RHIRenderPassLoadOp::Load
                                              : bFullRect                                         ? RHIRenderPassLoadOp::DontCare
                                                                                                  : RHIRenderPassLoadOp::Clear;
        const float4&             outputClear = upscaleZoom > 0 ? FrameRendererUtil::kBlackClear : _state._clearColor;
        // PSO 는 대상의 실제 포맷으로 고른다. 렌더 타깃 포맷은 PSO 의 일부라 대상마다 PSO 가 다르다. 셋업에 없으면 findOutputPSO 가 멈춘다(복사로 대신하지 않는다).
        const RHIPipelineStateHandle psoBlit = _psoManager.findOutputPSO( RenderPassType::Present, target._format );
        if ( src != 0 )
        {
            registerPassTexture( context, attachmentNames()._sourceColor, srcName );
            // 선언한 입력을 **모두** 바인딩한다. Present 가 후처리 체인을 겸하면 깊이(외곽선)와 AO 가 필요하고,
            // 그냥 블릿이면 선언이 컬러 하나뿐이라 위 등록을 덮어쓸 뿐이다.
            registerDeclaredInputs( context, passDesc );
            // 후처리 체인이 톤맵까지 하면 자동 노출을 이 그리기 앞에서 측정한다(Tonemap 패스와 같은 자리).
            _autoExposure.recordAutoExposure( context, *_pActiveView, passDesc );
            // FSR 1 로 키우는 뷰는 렌더 패스를 열기 전에 EASU 를 중간 RT 에 그리고, 아래에서 블릿 대신 RCAS 로 그린다(prepareUpscale 이 셋업에서 정했다).
            const bool             bUpscale = _upscale.recordUpscaleEasu( context, *_pActiveView, srcName );
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo._bBindColor        = SW_TRUE;
            beginInfo._arrColorTarget[0] = dstTarget;
            beginInfo._colorTargetCount  = 1;
            beginInfo._arrLoadOp[0]      = outputLoad;
            beginInfo._arrClearColor[0]  = outputClear;
            beginInfo._width             = outputWidth;
            beginInfo._height            = outputHeight;
            context._pCmd->beginRenderPass( beginInfo );
            // 사각형이면 뷰포트로 그 안에만 그린다. 전체 화면 삼각형이 사각형을 채우고 원본 전체를 그 안에 늘인다.
            // 정수 업스케일이면 원본 크기 × 배율을 사각형 가운데에 놓는다 — 셰이더가 점 샘플(SW_PASS_FLAG_POINT_UPSCALE)이라 원본 픽셀이 배율 × 배율 칸이다.
            if ( bFullRect == false )
            {
                const float4& rect = activeView._settings._screenRect;
                RHIViewport   viewport{};
                viewport._x      = rect._x * static_cast<float32>( outputWidth );
                viewport._y      = rect._y * static_cast<float32>( outputHeight );
                viewport._width  = rect._z * static_cast<float32>( outputWidth );
                viewport._height = rect._w * static_cast<float32>( outputHeight );
                if ( upscaleZoom > 0 )
                {
                    const TransientAttachmentPool& pool = activePool();
                    viewport._x                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._x ), static_cast<uint32>( viewport._width ), pool.getWidth(), upscaleZoom ) );
                    viewport._y                         = static_cast<float32>( FrameRendererUtil::computeUpscaleOffset(
                        static_cast<uint32>( viewport._y ), static_cast<uint32>( viewport._height ), pool.getHeight(), upscaleZoom ) );
                    viewport._width                     = static_cast<float32>( pool.getWidth() * upscaleZoom );
                    viewport._height                    = static_cast<float32>( pool.getHeight() * upscaleZoom );
                }
                context._pCmd->setViewport( viewport );
            }
            if ( bUpscale )
            {
                const float4& rect      = activeView._settings._screenRect;
                const float32 viewportX = bFullRect ? 0.0f : rect._x * static_cast<float32>( outputWidth );
                const float32 viewportY = bFullRect ? 0.0f : rect._y * static_cast<float32>( outputHeight );
                _upscale.drawUpscaleRcas( context, *_pActiveView, target._format, viewportX, viewportY );
            }
            else
            {
                drawFullscreen( context, psoBlit );
            }
            context._pCmd->endRenderPass();
            if ( bCaptureToBack )
                context._pCmd->blitTexture( dstTarget, 0 );
        }
        else
        {
            // 원본이 없으면 PSO 없이(백엔드 기본 파이프라인으로) 대신 그리지 않는다. 출력 대상의 상태 전이만 위해 패스를 열고 닫는다.
            SW_LOG_ERROR_ONCE( "Present source color has no transient (its allocation failed) - the output is not drawn" );
            RHIRenderPassBeginInfo beginInfo{};
            beginInfo.setColorTarget( dstTarget, _state._clearColor, RHIRenderPassLoadOp::Load );
            beginInfo._bBindColor = SW_TRUE;
            context._pCmd->beginRenderPass( beginInfo );
            context._pCmd->endRenderPass();
        }
        if ( target._bCaptureFromOutput == SW_TRUE && isLastSwapchainWriter( passDesc ) )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
    }

    void FramePassExecutor::executeCanvasPass( const PassExecuteInput& input )
    {
        // 화면 2D 를 주 출력(Present 가 그린 것)에 기존 내용을 유지한 채(Load) 그린다. 주 출력에 **마지막으로 그리는 뷰**만 그린다
        // (`_pMainOutputCanvasView` — 화면 사각형 뷰가 있으면 그 마지막 것, 없으면 주 시점). 그래야 PiP · 분할 화면이 UI 를 덮지 않는다.
        // 렌더 텍스처 · 호스트 타깃 뷰에는 그리지 않는다. 그릴 것이 없으면 렌더 패스를 열지 않는다. 대상 전체가 뷰포트다(화면 사각형 설정과 무관).
        FramePassContext&   context = input._context;
        const PresentTarget target  = resolvePresentTarget();
        const bool          bDraw   = _pActiveView == _pMainOutputCanvasView && _state._canvasFrame._mainOutput.isEmpty() == false && target._width > 0 && target._height > 0;
        if ( bDraw )
        {
            const RHIPipelineStateHandle psoCanvas = _psoManager.findOutputPSO( RenderPassType::Canvas, target._format ); // 셋업에 없으면 멈춘다(다른 포맷의 PSO 로 대신하지 않는다)
            RHIRenderPassBeginInfo       beginInfo{};
            beginInfo._bBindColor        = SW_TRUE;
            beginInfo._arrColorTarget[0] = target._texture;
            beginInfo._colorTargetCount  = 1;
            beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load;
            beginInfo._width             = target._width;
            beginInfo._height            = target._height;
            context._pCmd->beginRenderPass( beginInfo );
            (void)_state._canvasRenderer.drawList( *context._pCmd, _state._canvasFrame._mainOutput, _state._canvasRenderer.getMainQuadBase(), psoCanvas, target._width, target._height,
                                                   _pDevice->supportsNativeBindlessSampling(), _state._canvasFrame._colorVisionMode );
            context._pCmd->endRenderPass();
        }
        const bool bLastWriter = isLastSwapchainWriter( input._passDesc );
        if ( target._bCaptureToBack == SW_TRUE && bLastWriter )
            context._pCmd->blitTexture( target._texture, 0 );
        if ( target._bCaptureFromOutput == SW_TRUE && bLastWriter )
            _capture.copyPresentFromOutput( *context._pCmd, target._texture, target._width, target._height, target._format );
    }

    bool FramePassExecutor::beginColorPass( FramePassContext& context, string_view colorName, string_view depthName, const float4& clearColor,
                                            RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad )
    {
        const string_view         arrName[]  = { colorName };
        const float4              arrClear[] = { clearColor };
        const RHIRenderPassLoadOp arrLoad[]  = { colorLoad };
        return beginColorPassMrt( context, arrName, arrClear, arrLoad, 1, depthName, depthLoad );
    }

    bool FramePassExecutor::beginColorPassMrt( FramePassContext& context, const string_view* pColorNames, const float4* pTargetClearColor, const RHIRenderPassLoadOp* pColorLoad,
                                               uint32 colorCount, string_view depthName, RHIRenderPassLoadOp depthLoad )
    {
        const bool bInvalidPassTargets = _pDevice == nullptr || context._pCmd == nullptr || pColorNames == nullptr || colorCount == 0;
        if ( bInvalidPassTargets )
            return false;

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor       = SW_TRUE;
        beginInfo._depthLoadOp      = depthLoad;
        beginInfo._clearDepth       = 1.0f;
        beginInfo._depthTarget      = depthName.empty() ? 0 : findTransient( depthName );
        beginInfo._width            = activePool().getWidth();
        beginInfo._height           = activePool().getHeight();
        beginInfo._colorTargetCount = colorCount > kMaxColorAttachments ? kMaxColorAttachments : colorCount;
        for ( uint32 colorTargetIndex = 0; colorTargetIndex < beginInfo._colorTargetCount; ++colorTargetIndex )
        {
            const TransientAttachmentPool::Attachment target = findTransientAttachment( pColorNames[colorTargetIndex] );
            beginInfo._arrColorTarget[colorTargetIndex]      = target._texture;
            // 렌더 패스(뷰포트, 시저)는 타깃의 크기를 따른다. 나눗수가 있는 첨부는 프레임보다 작다. 한 패스의 타깃은 크기가 같다(검증).
            if ( colorTargetIndex == 0 && target._texture != 0 )
            {
                beginInfo._width  = target._width;
                beginInfo._height = target._height;
            }
            // 핸들 0은 백버퍼다. 없는 첨부를 그대로 열면 패스가 화면에 그린다. 열지 않고 알린다(패스 경로라 한 번만).
            if ( beginInfo._arrColorTarget[colorTargetIndex] == 0 )
            {
                if ( _bMissingColorTargetLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
                    SW_LOG_ERROR( "Color target '%#' does not exist this frame - skipping the pass (handle 0 of a missing attachment is the back buffer, so opening it as is would draw to the screen). "
                                  "Check _listOutput and _listAttachment of the pipeline.",
                                  pColorNames[colorTargetIndex] );
                return false;
            }
            beginInfo._arrLoadOp[colorTargetIndex] = pColorLoad != nullptr ? pColorLoad[colorTargetIndex] : RHIRenderPassLoadOp::Clear;
            if ( pTargetClearColor != nullptr )
                beginInfo._arrClearColor[colorTargetIndex] = pTargetClearColor[colorTargetIndex];
        }
        context._pCmd->beginRenderPass( beginInfo );
        return true;
    }

    void FramePassExecutor::beginDepthOnlyPass( FramePassContext& context, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad )
    {
        if ( context._pCmd == nullptr )
            return;
        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor                           = SW_FALSE;
        const TransientAttachmentPool::Attachment depth = findTransientAttachment( depthName );
        beginInfo._colorTargetCount                     = 0;
        beginInfo._depthTarget                          = depth._texture;
        beginInfo._depthLoadOp                          = depthLoad;
        beginInfo._clearDepth                           = clearDepth;
        beginInfo._width                                = depth._texture != 0 ? depth._width : activePool().getWidth();
        beginInfo._height                               = depth._texture != 0 ? depth._height : activePool().getHeight();
        context._pCmd->beginRenderPass( beginInfo );
    }

    uint32 FramePassExecutor::computeShadowTileCountPerView() const
    {
        return _state._frameShadow._layout.getCascadeCount() + static_cast<uint32>( _state._frameShadow._listLocalTile.size() );
    }

    void FramePassExecutor::drawShadowTiles( FramePassContext& context, bool bTransparentBatch )
    {
        // 그림자는 모든 타일을 덮는 상자로 거른 목록을 쓴다(언리얼의 뷰별 인스턴스 컬링에 해당한다). 끝나면 지금 뷰의 슬롯으로 돌아간다.
        const ViewShadowData&        shadow   = _pActiveView->_shadow;
        const RHIPipelineStateHandle pso      = _psoManager.getEnginePSO( RenderPassType::Shadow );
        const uint32                 viewCull = context._cullViewIndex;
        context._cullViewIndex                = _pActiveView->_shadowCullSlot;

        // 캐스케이드 하나가 아틀라스 전체이고 점광 · 스폿 타일이 없으면 아틀라스가 생기기 전과 같다 — 뷰포트도 행렬도 바꾸지 않는다.
        const bool bWholeAtlas = shadow._cascadeCount <= 1 && _state._frameShadow._listLocalTile.empty() &&
                                 shadow._arrCascadeTile[0]._cell._size == _state._frameShadow._layout.getAtlasResolution();
        if ( bWholeAtlas )
        {
            drawSceneMeshes( context, pso, bTransparentBatch );
        }
        else
        {
            // 타일마다 그 칸을 뷰포트로 잡고(가위도 같이 잡힌다) 그 타일의 행렬로 씬을 그린다. 상수가 바뀌므로 드로우는 새 상수 버퍼 슬롯을 받는다.
            const PassConstantNames& names     = passConstantNames();
            const uint32             tileCount = shadow._cascadeCount + static_cast<uint32>( _state._frameShadow._listLocalTile.size() );
            for ( uint32 tileIndex = 0; tileIndex < tileCount; ++tileIndex )
            {
                const ShadowTileDraw& tile = tileIndex < shadow._cascadeCount ? shadow._arrCascadeTile[tileIndex] : _state._frameShadow._listLocalTile[tileIndex - shadow._cascadeCount];
                RHIViewport           viewport{};
                viewport._x      = static_cast<float32>( tile._cell._x );
                viewport._y      = static_cast<float32>( tile._cell._y );
                viewport._width  = static_cast<float32>( tile._cell._size );
                viewport._height = static_cast<float32>( tile._cell._size );
                context._pCmd->setViewport( viewport );
                // 타일마다 새 상수 슬롯을 잡는다. 슬롯은 패스당 하나라서 같은 슬롯에 행렬만 다시 올리면, GPU 가 나중에 읽는 DX12 · Vulkan 에서는
                // 모든 타일이 마지막에 쓴 행렬을 본다(DX11 · GL 은 업데이트가 명령 순서대로 들어가 가려진다).
                if ( acquirePassCb( context ) == false ) // 링 고갈 — 이 타일은 그리지 않는다(앞 타일의 행렬로 그리지 않는다)
                    continue;
                context._passValues.setMatrix( names._lightViewProj, tile._viewProj );
                // 주 시점의 캐스케이드 타일은 자기 컬링 슬롯의 목록을 그린다(그 캐스케이드 절두체로만 거른 것). 점광 · 스폿 타일과 추가 뷰는 합친 칸이다.
                const bool bCascadeSlot = _pActiveView == &_state._mainView && tileIndex < _state._cascadeCullCount;
                context._cullViewIndex  = bCascadeSlot ? _state._cascadeCullSlotBase + tileIndex : _pActiveView->_shadowCullSlot;
                drawSceneMeshes( context, pso, bTransparentBatch );
            }
            // 이 패스 컨텍스트는 패스마다 시드의 사본이지만, 기준 행렬로 되돌려 두면 뒤에 무엇이 붙어도 셰이딩과 같은 행렬을 본다.
            context._passValues.setMatrix( names._lightViewProj, shadow._lightViewProj );
        }
        context._cullViewIndex = viewCull;
    }

    void FramePassExecutor::registerPassTexture( FramePassContext& context, const hashed_string& canonicalName, string_view attachmentName )
    {
        const TransientAttachmentPool::Attachment attachment = findTransientAttachment( attachmentName );
        if ( attachment._texture != 0 && context._pCmd != nullptr )
            context._pCmd->prepareTextureForShaderRead( attachment._texture );
        context._resourceRegistry.registerTexture( canonicalName, attachment._texture, attachment._srv );
        // 원본을 비켜 읽는 효과(블룸)는 그 원본의 텍셀 크기로 비켜야 한다. 반해상도 첨부면 프레임 텍셀의 두 배다. context 는 패스마다 시드의 사본이라 다음 패스로 새지 않는다.
        const bool bSourceColorAttachment = canonicalName == attachmentNames()._sourceColor && attachment._width > 0 && attachment._height > 0;
        if ( bSourceColorAttachment )
        {
            const float32 width  = static_cast<float32>( attachment._width );
            const float32 height = static_cast<float32>( attachment._height );
            context._passValues.setFloat4( passConstantNames()._sourceTexel, float4{ 1.0f / width, 1.0f / height, width, height } );
        }
    }

    void FramePassExecutor::commitBindlessTextureBindings( FramePassContext& context )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        // 주의: 여기서 updatePassConstants 를 부르지 않는다. 이 함수는 드로우 루프 안에서 불리므로 그러면
        // 드로우마다 라이트와 뷰 행렬을 다시 만들고(정규화, 외적, 4x4 곱 두 번) 카메라를 다시 찾고
        // hashed_string 을 여덟 개씩 intern 한다. 그 값들은 모두 프레임 상수라 execute/executePacket
        // 이 프레임 시드(_state._frameContext)에 한 번만 채우면 되고, 패스 컨텍스트는 그 시드를 복사해 간다.
        // 드로우마다 바뀌는 것은 g_World 하나뿐이고 그것은 bindForDraw 가 넣는다.

        // DX11 과 GL 은 패스가 이름으로 건 엔진 텍스처를 역할마다 제 칸에 바인딩한다(bindless 에뮬레이션 — shaderslot::emulated 표).
        // DX12 와 VK 는 셰이더가 힙이나 배열을 직접 인덱싱한다. 리플렉션 바인더가 g_<Name>Index 를 채운다.
        if ( _pDevice->supportsNativeBindlessSampling() )
            return;

        // 역할 하나가 칸 하나라 고를 것이 없다 — 한 패스가 어떤 역할을 함께 읽어도 겹치지 않는다(예전에는 네 칸을 나눠 써 선언 밖에서 거는 쿠키 · 노출이 부딪혔다).
        // 이 함수는 드로우 루프 안에서 불린다. 이름은 처음 한 번 intern 한다 — 문자열로 찾으면 **드로우마다** intern 하게 된다(FNV 해시 + 샤드 뮤텍스).
        static const auto s_arrName = []
        {
            array<hashed_string, shaderslot::kEngineTextureCount> arrName{};
            for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
                arrName[ordinal] = hashed_string( shaderslot::emulated::kEngineTextureNames[ordinal] );
            return arrName;
        }();
        for ( uint32 ordinal = 0; ordinal < shaderslot::kEngineTextureCount; ++ordinal )
        {
            const RegisteredTexture* pTex = context._resourceRegistry.findTexture( s_arrName[ordinal] );
            if ( pTex != nullptr && pTex->_srv != kInvalidDescriptorIndex )
                context._pCmd->bindShaderResource( pTex->_srv, shaderslot::kEngineTexture0 + ordinal );
        }
    }

    FramePassExecutor::PresentTarget FramePassExecutor::resolvePresentTarget() const
    {
        // 렌더 텍스처와 호스트 타깃 뷰는 자기 출력 전체, 주 시점과 화면 사각형 뷰는 주 출력(백버퍼, 게임 뷰 RT)이다. 스크린샷 실행이면 백버퍼 대신 캡처
        // 텍스처에 그린다. 백버퍼는 핸들이 없어 읽을 수 없고, 후처리와 UI 가 끝난 최종 화면을 볼 방법이 그것뿐이다. 캡처는 주 출력만 받는다.
        const FrameViewTarget& activeView = *_pActiveView;
        RHITextureHandle       ownOutput{ 0 };
        const bool             bExtraViewOwnOutput = isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::RenderTexture && activeView._pOutputTexture != nullptr;
        if ( bExtraViewOwnOutput )
            ownOutput = activeView._pOutputTexture->getHandle();
        else if ( isRenderingExtraView() && activeView._outputKind == RenderViewOutputKind::HostTarget )
            ownOutput = activeView._hostTarget;
        const bool bOwnOutput = ownOutput != 0;
        const bool bCapture   = bOwnOutput == false && ( _state._outputRenderTarget == 0 ) && _capture.isPresentActive();
        // 캡처를 백버퍼로 옮기는 것은 출력이 곧 백버퍼일 때만이다. 크기를 덮어쓴 출력(초상화 베이크)은 화면에 나가지 않는다(크기가 다르면 복사도 안 된다).
        const bool bCaptureToBack = bCapture && _state._outputWidth == _pDevice->getBackBufferWidth() && _state._outputHeight == _pDevice->getBackBufferHeight();
        // 출력이 RT(에디터 게임 뷰)면 그 RT 에 그대로 그리고 끝에 캡처로 복사한다. 화면 사각형 뷰도 같은 RT 에 겹쳐 그리므로 뷰마다 복사하면
        // 마지막 복사가 화면과 같다.
        const bool bCaptureFromOutput = bOwnOutput == false && _state._outputRenderTarget != 0 && _capture.isPresentActive();

        PresentTarget target{};
        target._texture = bOwnOutput ? ownOutput : bCapture ? _capture.getPresentTexture()
                                                            : _state._outputRenderTarget;
        target._width   = bOwnOutput ? activeView._outputWidth : _state._outputWidth;
        target._height  = bOwnOutput ? activeView._outputHeight : _state._outputHeight;
        // 백버퍼는 디바이스가 채택한 포맷(Vulkan 은 서피스 협상 결과)이고, 텍스처는 그 텍스처가 기록한 포맷이다.
        target._format             = ( target._texture == 0 ) ? _pDevice->getBackBufferFormat() : _pDevice->getResourceFactory()->getTextureFormat( target._texture );
        target._bOwnOutput         = bOwnOutput ? SW_TRUE : SW_FALSE;
        target._bCapture           = bCapture ? SW_TRUE : SW_FALSE;
        target._bCaptureToBack     = bCaptureToBack ? SW_TRUE : SW_FALSE;
        target._bCaptureFromOutput = bCaptureFromOutput ? SW_TRUE : SW_FALSE;
        return target;
    }

    bool FramePassExecutor::isLastSwapchainWriter( const RenderGraphPassDesc& passDesc ) const
    {
        return _pLastSwapchainWriter == nullptr || &passDesc == _pLastSwapchainWriter;
    }

    const RenderGraphPassDesc* FramePassExecutor::findPassDescByType( RenderPassType passType ) const
    {
        // 표기 흔들림은 로드 시점의 _resolvedType 이 이미 흡수했다. 여기서는 값만 비교하면 된다.
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == passType )
                return &pass;
        }
        return nullptr;
    }

    void FramePassExecutor::registerInstanceBuffer( FramePassContext& context )
    {
        if ( _state._gpuScene.getInstanceBuffer() == 0 || _state._gpuScene.getInstanceSRV() == kInvalidDescriptorIndex )
            return;
        // 이름 "SwInstances" ↔ binding.hlsli 의 g_SwInstances / PassCB g_SwInstancesIndex (canonical 매칭).
        context._resourceRegistry.registerBuffer( passConstantNames()._swInstances,
                                                  _state._gpuScene.getInstanceBuffer(), _state._gpuScene.getInstanceSRV() );
        context._passValues.setUint( passConstantNames()._swInstanceCount, static_cast<uint32>( _state._gpuScene.getInstances().size() ) );

        // 배치 표. **패스당 한 번** 건다. 배치마다 다른 값(인스턴스 시작 · 모프 풀 시작 · 정점 풀 시작)이 모두 여기 있어
        // 드로우는 배치 번호만 실어 나른다. 그래서 같은 PSO 의 배치들을 멀티 드로우 하나로 낼 수 있다.
        if ( _state._gpuScene.getBatchInfoBuffer() != 0 && _state._gpuScene.getBatchInfoSRV() != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swBatches, _state._gpuScene.getBatchInfoBuffer(), _state._gpuScene.getBatchInfoSRV() );
            context._passValues.setUint( passConstantNames()._swBatchCount, _state._gpuScene.getIndirectCommandCount() );
        }

        // 모프 결과 풀. **패스당 한 번** 건다. 배치는 시작 오프셋을 배치 표에 싣는다(드로우 사이에
        // 바인딩이 바뀌지 않는다는 이 엔진의 규약). 안 걸리면 셰이더가 g_SwMorphVerticesIndex 로 알아채고
        // 입력 스트림을 그대로 쓴다.
        const RHIStructuredBufferSlot& morphBuffer = ( _state._deform._bMorphBindsRest != SW_FALSE ) ? _state._deform._meshMorphPool.getRestBuffer()
                                                                                                     : _state._deform._meshMorphPool.getMorphBuffer();
        if ( morphBuffer._buffer != 0 && morphBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swMorphVertices, morphBuffer._buffer, morphBuffer._srv );
            // 진단(레스트를 그대로 물림)이면 레스트 버퍼는 모프 구간만 담는다 — 스킨 구간 번호는 범위 밖으로 걸러 입력 스트림을 쓴다.
            const uint32 morphElementCount = ( _state._deform._bMorphBindsRest != SW_FALSE ) ? _state._deform._meshMorphPool.getMorphVertexCount() : _state._deform._meshMorphPool.getVertexCount();
            context._passValues.setUint( passConstantNames()._swMorphVertexCount, morphElementCount );
        }

        // 정점 애니메이션(VAT) 표 — 패스당 한 번. 시계는 게임 스레드의 군중 시계(스냅샷)라 인스턴스 시각 오프셋과 더하면 CPU 의 클립 시각이다.
        const RHIStructuredBufferSlot& vertexAnimationBuffer = _state._deform._vertexAnimationPool.getBuffer();
        if ( vertexAnimationBuffer._buffer != 0 && vertexAnimationBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVertexAnimation, vertexAnimationBuffer._buffer, vertexAnimationBuffer._srv );
            context._passValues.setUint( passConstantNames()._swVertexAnimationCount, _state._deform._vertexAnimationPool.getWordCount() );
            context._passValues.setFloat( passConstantNames()._swVertexAnimationTime, _state._gpuScene.getVertexAnimationTime() );
        }
        // 머티리얼 애니메이션 시계(툰 UV 애니메이션) — VAT 와 같은 장면 애니메이션 시계지만 VAT 표와 무관하게 늘 건다.
        context._passValues.setFloat( passConstantNames()._swMaterialTime, _state._gpuScene.getVertexAnimationTime() );

        // 컬링이 실제로 목록을 만들었을 때만 건다. 안 걸리면 셰이더가 g_SwVisibleInstanceIDsIndex 로 알아채고
        // 배치 시작 + 서수를 쓴다(컬링 없음 경로). 반대로 목록만 걸고 컬링을 안 돌리면 **비어 있는
        // 목록**을 읽어 모두 0 번 인스턴스를 그린다. 그래서 둘은 반드시 같이 켜지고 같이 꺼진다.
        const GPUCullViewResources& cullView                  = _state._gpuScene.getCullView( context._cullViewIndex );
        const bool                  bVisibleInstanceListReady = _prepass.isGPUCullingActive() != SW_FALSE && cullView._visibleInstances._buffer != 0 &&
                                               cullView._visibleInstances._srv != kInvalidDescriptorIndex;
        if ( bVisibleInstanceListReady )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVisibleInstanceIDs,
                                                      cullView._visibleInstances._buffer, cullView._visibleInstances._srv );
            context._passValues.setUint( passConstantNames()._swVisibleSlotCount, _state._gpuScene.getVisibleSlotCount() );
        }
    }

    void FramePassExecutor::registerLightBuffer( FramePassContext& context )
    {
        // 2D 빛 쿠키 아틀라스 — 쿠키를 쓴 빛이 있는 프레임에만 역할 텍스처로 건다(셰이더는 g_Light2DCookieAtlasIndex 로 걸렸는지 본다).
        const Texture2D* pCookieAtlas = _state._cookieAtlas.hasCookies() ? _state._cookieAtlas.findAtlas() : nullptr;
        if ( pCookieAtlas != nullptr )
            context._resourceRegistry.registerTexture( attachmentNames()._light2DCookieAtlas, pCookieAtlas->getHandle(), pCookieAtlas->getSRV() );
        // 개수는 버퍼가 없어도 채운다. 셰이더는 0 이면 빛을 돌지 않는다(앰비언트만).
        context._passValues.setUint( passConstantNames()._swLightCount, _state._lightBuffer.isBindable() ? _state._lightBuffer.getCount() : 0u );
        // 지금 그리는 뷰의 라이트 그리드(t12). 없으면(gv_lightCulling=None, 투영이 깨진 뷰) 셰이더는 목록 전체를 돈다.
        // 그리드 첫 단어와 버퍼는 같이 켜지고 같이 꺼진다 — 첫 단어만 있고 버퍼가 없으면 셰이더가 빈 슬롯을 읽는다(LightGrid::hasGrid 가 둘 다 본다).
        const RHIStructuredBufferSlot* pGridBuffer = _state._lightBuffer.isBindable() ? _state._lightBuffer.findGridBuffer() : nullptr;
        const bool                     bGridReady  = pGridBuffer != nullptr && pGridBuffer->_buffer != 0 && pGridBuffer->_srv != kInvalidDescriptorIndex;
        const uint32                   gridBase    = bGridReady ? _state._lightBuffer.getGridBase( _pActiveView->_lightGridIndex ) : invalid_index::kUint32;
        context._passValues.setUint( passConstantNames()._swLightGridBase, gridBase );
        if ( _state._lightBuffer.isBindable() == false )
            return;
        const RHIStructuredBufferSlot& buffer = _state._lightBuffer.getBuffer();
        context._resourceRegistry.registerBuffer( passConstantNames()._swLights, buffer._buffer, buffer._srv );
        if ( bGridReady )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightGrid, pGridBuffer->_buffer, pGridBuffer->_srv );
        // 라이트 프로브 SH9(t13) — 볼륨 원소가 라이트 목록에 있는 프레임에만 있다. 셰이더는 g_SwLightProbesIndex 로 걸렸는지 본다.
        const RHIStructuredBufferSlot* pProbeBuffer = _state._lightBuffer.findProbeBuffer();
        if ( pProbeBuffer != nullptr )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightProbes, pProbeBuffer->_buffer, pProbeBuffer->_srv );
    }

    void FramePassExecutor::registerMaterialBuffer( FramePassContext& context, const GPUMeshBatch& batch, RHIPipelineStateHandle pso )
    {
        // 배치의 셰이더 타입에 해당하는 머티리얼 데이터 버퍼. 이름 "SwMaterials" ↔ binding.hlsli 의 g_SwMaterials(t5).
        // 바인더가 리플렉션 슬롯에 걸고, 셰이더는 인스턴스의 materialIndex 로 원소를 읽는다(드로우별 CB 바인딩 없음).
        if ( batch._materialBuffer == 0 || batch._materialSRV == kInvalidDescriptorIndex )
        {
            // 머티리얼 없는 배치. 빈 슬롯으로 그리지 않는다(Vulkan 은 partially-bound 슬롯을 실제로 읽으면 정의되지 않는다).
            // 폴백은 **이 PSO 셰이더의 레이아웃**(stride · 텍스처 인덱스 필드)으로 만든 것을 고른다. 공용 256 바이트를 걸면 DX11 이 드로우마다
            // "structure stride 256 vs 24" 를 낸다(SRV 의 구조 stride 는 셰이더 선언과 같아야 한다).
            const ShaderBindingLayout* pLayout = ( context._lastLayoutPSO == pso ) ? context._pLastLayout : _psoManager.findLayout( pso );
            const ShaderBindingSlot*   pSlot   = ( pLayout != nullptr ) ? pLayout->find( passConstantNames()._swMaterials ) : nullptr;
            if ( pSlot == nullptr || pSlot->_elementStride == 0 )
                return; // 셰이더가 머티리얼 버퍼를 선언하지 않았다. 걸 것도 없다.
            const RHIStructuredBufferSlot* pFallback            = _psoManager.findMaterialFallback( pLayout );
            const bool                     bUseFallbackMaterial = pFallback != nullptr && pFallback->isValid() && pFallback->_srv != kInvalidDescriptorIndex;
            if ( bUseFallbackMaterial )
            {
                context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, pFallback->_buffer, pFallback->_srv );
                context._drawMaterialCount = 1u;
            }
            else if ( _bMaterialFallbackMissingLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
            {
                // 여기서 조용히 나가면 t5 가 빈 채로 드로우가 나가고 Vulkan 은 디바이스를 잃는다.
                // 막는 것은 ensureMaterialPSOs 쪽이고, 그래도 새면 원인을 알 수 있게 남긴다.
                SW_LOG_ERROR( "No material fallback buffer for stride %# - this draw goes out with g_SwMaterials empty.",
                              pSlot->_elementStride );
            }
            return;
        }
        context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, batch._materialBuffer, batch._materialSRV );
        context._drawMaterialCount = batch._materialCount;
    }

    void FramePassExecutor::bindForDraw( FramePassContext& context, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                                         const RHIDescriptorIndex* pMaterialTexSRV )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        context._passValues.setMatrix( passConstantNames()._world, context._world );

        // 같은 PSO 로 연속 드로우하는 것이 흔한 패턴이라, 패스 로컬 1칸 캐시로
        // FramePSOManager::findLayout() 의 뮤텍스 + 해시맵 조회를 매 드로우 반복하지 않게 한다.
        if ( context._lastLayoutPSO != pso )
        {
            context._pLastLayout   = _psoManager.findLayout( pso );
            context._lastLayoutPSO = pso;
        }
        const ShaderBindingLayout* pLayout = context._pLastLayout;
        if ( pLayout == nullptr || pLayout->isEmpty() )
            return; // 레이아웃을 못 얻었다(컴파일 실패 등). 조용히 건너뛴다

        // 배치마다 바뀌는 값은 **루트/푸시 상수**로 싣는다. 커맨드 리스트에 값이 그대로 들어가므로 드로우끼리
        // 덮어쓸 수 없다. 그래서 PassCB 는 패스당 하나면 충분하다(PassCB 에 넣으면 드로우마다 버퍼를 새로 잡아야
        // 한다). 언리얼의 드로우별 느슨한 파라미터와 같은 자리다.
        const uint32 arrDrawRootConstant[] = { context._drawMaterialCount };
        context._pCmd->setGraphicsRootConstants( 0, static_cast<uint32>( sizeof( arrDrawRootConstant ) / sizeof( arrDrawRootConstant[0] ) ),
                                                 arrDrawRootConstant );

        const EngineConstantBufferSlot engineCb{ context._passCb, context._passCbIndex };

        // 엔진 상수버퍼를 이 드로우에서 다시 만들 필요가 있나. 값 · 레지스트리 버전과 버퍼가 모두 그대로면 없다.
        // **PSO 도 같아야 한다.** 이 플래그는 상수버퍼 재업로드만이 아니라 리소스 재바인딩까지 건너뛰게 하는데,
        // 슬롯 상태는 PSO 가 바뀌는 순간 백엔드가 비우기 때문이다(FramePassContext::_lastBindPSO 참고).
        const uint32 valuesVersion   = context._passValues.getVersion();
        const uint32 registryVersion = context._resourceRegistry.getVersion();
        const bool   bUpToDate       = ( context._lastBindPSO == pso ) && ( context._lastCbBuffer == engineCb._buffer ) &&
                               ( context._lastCbValuesVersion == valuesVersion ) && ( context._lastCbRegistryVersion == registryVersion );

        const BindlessIndexCheckSite  indexCheck{ _pDevice, context._passName };
        const BindlessIndexCheckSite* pIndexCheck = &indexCheck;
        ShaderParameterBinder::bindGraphics( *context._pCmd, *pLayout, context._resourceRegistry, context._passValues, engineCb, materialCb,
                                             _pDevice->supportsNativeBindlessSampling(), pMaterialTexSRV, bUpToDate, pIndexCheck );

        context._lastBindPSO           = pso;
        context._lastCbBuffer          = engineCb._buffer;
        context._lastCbValuesVersion   = valuesVersion;
        context._lastCbRegistryVersion = registryVersion;
    }

    void FramePassExecutor::drawSceneMeshes( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        if ( _state._gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, bTransparentPass, nullptr );
            return;
        }

        // 그릴 메시가 하나도 없다. 상태만 맞춰 두고 나간다.
        //
        // 주의: 씬을 다시 순회하는 폴백이나 배치마다 drawInstanced 를 부르는 두 번째 드로우 루프를 두지 않는다.
        // 네 백엔드가 모두 인다이렉트 드로우를 지원하고, 경로가 둘이면 새 기능(컬링 · 정렬 · 인스턴스 애니메이션)이
        // 한쪽에만 들어가 다른 쪽이 조용히 다른 그림을 낸다.
        if ( pso != 0 )
            context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
    }

    void FramePassExecutor::drawTransparentQueue( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;
        const vector<GPUTransparentParticleSlot>& listParticleSlot = findActiveParticleSlots();
        if ( _state._gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, true, &listParticleSlot );
            return;
        }
        // 메시가 없으면 입자만 이 뷰의 순서로 그린다.
        drawSceneMeshes( context, pso, true );
        (void)drawParticleSlots( context, vector_reference<const GPUTransparentParticleSlot>( listParticleSlot ) );
    }

    const vector<GPUTransparentParticleSlot>& FramePassExecutor::findActiveParticleSlots() const
    {
        if ( isRenderingExtraView() && _pActiveView->_bHasParticleOrder == SW_TRUE )
            return _pActiveView->_listParticleSlot;
        return _state._gpuScene.getParticleSlots();
    }

    uint32 FramePassExecutor::countDrawGroup( const BatchDrawState& state, RenderViewMode drawMode, uint32 orderIndex, bool bDrawsParticles ) const
    {
        const uint32 batchCount = static_cast<uint32>( state._batches.size() );
        const uint32 batchIndex = ( state._pOrder != nullptr ) ? ( *state._pOrder )[orderIndex] : orderIndex;
        uint32       groupCount = 1;
        if ( state._bMerge == false )
            return groupCount;
        const GPUMeshBatch& head = state._batches[batchIndex];
        while ( orderIndex + groupCount < batchCount )
        {
            const uint32 nextBatchIndex = ( state._pOrder != nullptr ) ? ( *state._pOrder )[orderIndex + groupCount] : orderIndex + groupCount;
            // 다음 입자의 자리가 묶음 안에 들면 멀티 드로우 묶음을 그 자리에서 끊는다.
            const bool bParticleInside = bDrawsParticles && state._particleSlotCursor < state._listParticleSlot.size() &&
                                         state._listParticleSlot[state._particleSlotCursor]._drawPosition <= orderIndex + groupCount;
            const bool bGroupEnds = nextBatchIndex != batchIndex + groupCount || bParticleInside;
            if ( bGroupEnds || isSameDrawGroup( state._pso, state._passType, drawMode, head, state._batches[nextBatchIndex] ) == false )
                break;
            ++groupCount;
        }
        return groupCount;
    }

    void FramePassExecutor::drawBatchRound( FramePassContext& context, BatchDrawState& inoutState, RenderViewMode drawMode, bool bDrawsParticles )
    {
        const uint32 batchCount = static_cast<uint32>( inoutState._batches.size() );
        uint32       orderIndex{ 0 };
        while ( orderIndex < batchCount )
        {
            if ( bDrawsParticles && drawParticleRun( context, inoutState._listParticleSlot, inoutState._particleSlotCursor, orderIndex, inoutState._slotStream ) )
            {
                inoutState._boundPSO          = 0;
                inoutState._boundVertexBuffer = 0;
            }
            const uint32        batchIndex       = ( inoutState._pOrder != nullptr ) ? ( *inoutState._pOrder )[orderIndex] : orderIndex;
            const GPUMeshBatch& head             = inoutState._batches[batchIndex];
            const bool          bHeadNotDrawable = head._vertexBuffer == 0 || head.hasDrawSlots() == false || drawsBatchInPass( inoutState._passType, head ) == false;
            if ( bHeadNotDrawable )
            {
                ++orderIndex;
                continue;
            }
            const uint32 groupCount = countDrawGroup( inoutState, drawMode, orderIndex, bDrawsParticles );
            // **이 패스의 뷰**가 만든 인자를 쓴다. 그림자 패스가 메인 카메라 인자를 쓰면 화면 밖에서
            // 화면 안으로 그림자를 드리우는 물체가 사라진다.
            if ( drawBatchGroup( context, inoutState._pso, drawMode, head, inoutState._boundPSO, inoutState._boundVertexBuffer, inoutState._argsBuffer, inoutState._batchOffset + batchIndex,
                                 groupCount ) )
                ++inoutState._drawCallCount;
            orderIndex += groupCount;
        }
    }

    void FramePassExecutor::drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass,
                                            const vector<GPUTransparentParticleSlot>* pParticleSlot )
    {
        SW_PROFILE_SCOPE( "RT.Draw.gpuBatches" );

        // 패스 PSO 가 없으면(셋업이 만들지 못하고 알렸다) 앞서 묶인 파이프라인으로 대신 그리지 않는다.
        const bool bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || _state._gpuScene.isUploaded() == false || pso == 0;
        if ( bCannotDraw )
            return;

        context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );

        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _state._gpuScene.getInstanceSRV() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( context );

        BatchDrawState state{};
        state._pso      = pso;
        state._boundPSO = pso;
        state._batches  = bTransparentPass ? _state._gpuScene.getTransparentBatches() : _state._gpuScene.getOpaqueBatches();
        // 시간만 보면 무엇이 비싼지 알 수 없다. 배치가 몇 개로 묶였는지가 해석의 전제다.
        SW_PROFILE_COUNT( "RT.Draw.gpuBatchCount", state._batches.size() );

        // 간접 인자 슬롯은 업로드 순서대로 불투명 다음 투명으로 놓인다.
        state._batchOffset = bTransparentPass ? static_cast<uint32>( _state._gpuScene.getOpaqueBatches().size() ) : 0u;

        // **같은 PSO · 정점 버퍼 · 머티리얼(버퍼 · CB · 텍스처 · 원소 수)의 연속 배치는 drawIndirect 한 번(멀티 드로우)이다.** 배치마다
        // 다른 값은 배치 표(g_SwBatches)와 인스턴스 슬롯 스트림(슬롯 1)이 준다. 간접 인자의 startInstance 가 배치 시작이라 입력
        // 어셈블러가 인스턴스마다 자기 전역 자리를 넘기고, 정점은 그 인스턴스의 meshBatchIndex 로 표를 읽는다. 드로우 ID 도 루트
        // 상수 주입도 없다(DX12 커맨드 시그니처에 루트 상수를 넣으면 ExecuteIndirect 가 두 배 느려진다. 실측이다). 멀티 드로우가 없는
        // 백엔드(DX11)는 하나씩 부른다. 드로우 루프의 비용은 호출 수라, 배치가 많은 씬에서는 RT 프레임의 큰 몫이 된다.
        state._bMerge = isDrawMergeEnabled() && _pDevice->getCapabilities()._bMultiDrawIndirect != SW_FALSE;
        // 패스가 머티리얼로 배치를 거르면(메시 외곽선 — 외곽선을 켠 머티리얼만) 그 밖의 배치는 그리지도 묶지도 않는다.
        state._passType = context._passType;

        state._argsBuffer = _state._gpuScene.getCullView( context._cullViewIndex )._indirectArgs._buffer;
        // 추가 뷰의 투명 패스는 그 뷰의 순서로 배치를 돈다(GPUViewTransparentOrder). 묶기는 그 순서에서 이웃이고 번호도 이어진 것만 — 간접 인자는 번호 순으로 놓여 있다.
        const bool bExtraView     = isRenderingExtraView();
        const bool bUsesViewOrder = bTransparentPass && bExtraView && _pActiveView->_listTransparentBatchOrder.size() == state._batches.size();
        if ( bUsesViewOrder )
            state._pOrder = &_pActiveView->_listTransparentBatchOrder;
        // 인스턴스 슬롯 스트림(슬롯 1). 패스당 한 번 건다. 씬 드로우는 모두 이 스트림에서 자기 자리를 읽는다.
        // 컬링 없는 백엔드의 추가 뷰는 꼬리를 그 뷰 순서로 다시 놓은 자기 스트림을 쓴다(GPU 정렬 백엔드는 늘 공용 스트림).
        state._slotStream = ( bExtraView && _pActiveView->_bUsesViewSlotStream == SW_TRUE ) ? _pActiveView->_instanceSlotStream : _state._gpuScene.getInstanceSlotStream();
        if ( state._slotStream != 0 )
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, state._slotStream, constant::kInstanceSlotStreamStride, 0 );
        // 투명 큐의 입자는 배치 순서의 자기 자리(`GPUTransparentParticleSlot::_drawPosition`) 앞에 끼운다. 입자 그리기가 PSO 와 정점 슬롯을 바꾸므로
        // 메시로 돌아올 때 PSO · 정점 버퍼를 다시 건다(`drawParticleRun` 이 슬롯 스트림과 에뮬 텍스처 슬롯을, bindForDraw 가 PSO 가 바뀐 바인딩을 다시 건다).
        if ( pParticleSlot != nullptr )
            state._listParticleSlot = vector_reference<const GPUTransparentParticleSlot>( *pParticleSlot );

        // 겹침 모드(ShadedWireframe)는 배치를 두 번 그린다 — Lit PSO 로 한 번, 그 모드의 PSO(선 · 깊이 바이어스)로 한 번. 입자는 첫 번째에만 끼운다.
        // 보기 모드는 지금 그리는 뷰의 설정이다(씬 뷰만 ShadedWireframe 이고 게임 뷰는 Lit 일 수 있다).
        const RenderViewMode viewMode       = _pActiveView->_settings._viewMode;
        const bool           bOverlayDraw   = getRenderViewModeInfo( viewMode )._bOverlay && FrameRendererUtil::appliesViewMode( state._passType );
        const RenderViewMode arrDrawMode[]  = { bOverlayDraw ? RenderViewMode::Lit : viewMode, viewMode };
        const uint32         drawRoundCount = bOverlayDraw ? 2u : 1u;
        for ( uint32 drawRound = 0; drawRound < drawRoundCount; ++drawRound )
        {
            drawBatchRound( context, state, arrDrawMode[drawRound], drawRound == 0 );
        }
        // 모든 배치보다 가까운 입자(자리 = 배치 수)를 마저 그린다.
        (void)drawParticleRun( context, state._listParticleSlot, state._particleSlotCursor, MathUtil::kMaxUInt32, state._slotStream );
        SW_PROFILE_COUNT( "RT.Draw.indirectCalls", state._drawCallCount );
        _indirectDrawCallCount.fetch_add( state._drawCallCount, std::memory_order_relaxed );
    }

    bool FramePassExecutor::layoutBindsMaterialCb( RHIPipelineStateHandle batchPSO ) const
    {
        // 머티리얼 CB 는 그 슬롯을 실제로 거는 셰이더에서만 병합 키다(`GPUMeshBatch::canShareMaterialBinding`).
        const ShaderBindingLayout* pLayout = _psoManager.findLayout( batchPSO );
        if ( pLayout == nullptr )
            return false;
        static const hashed_string s_materialCbName{ shaderslot::cbname::kMaterial };
        for ( const ShaderBindingSlot& slot : pLayout->getSlots() )
        {
            if ( slot._name == s_materialCbName )
                return true;
        }
        return false;
    }

    bool FramePassExecutor::isSameDrawGroup( RHIPipelineStateHandle pso, RenderPassType passType, RenderViewMode drawMode, const GPUMeshBatch& head,
                                             const GPUMeshBatch& other ) const
    {
        const bool bOtherBatchNotDrawable = other._vertexBuffer == 0 || other.hasDrawSlots() == false || drawsBatchInPass( passType, other ) == false;
        if ( bOtherBatchNotDrawable )
            return false;
        if ( other._vertexBuffer != head._vertexBuffer || _psoManager.psoForBatch( pso, other, drawMode ) != _psoManager.psoForBatch( pso, head, drawMode ) )
            return false;
        // 레이아웃을 보는 것은 CB 가 다를 때만(대부분 같다).
        const bool bCbDiffers = other._materialCb != head._materialCb;
        return GPUMeshBatch::canShareMaterialBinding( head, other, bCbDiffers && layoutBindsMaterialCb( _psoManager.psoForBatch( pso, head, drawMode ) ) );
    }

    bool FramePassExecutor::drawBatchGroup( FramePassContext& context, RHIPipelineStateHandle pso, RenderViewMode drawMode, const GPUMeshBatch& head,
                                            RHIPipelineStateHandle& ioBoundPSO,
                                            RHIBufferHandle& ioBoundVertexBuffer, RHIBufferHandle argsBuffer, uint32 argIndex, uint32 groupCount )
    {
        // 정점 풀 하나라 보통 패스당 한 번 걸린다. 풀 밖 메시(예산 초과)만 자기 버퍼를 건다.
        if ( head._vertexBuffer != ioBoundVertexBuffer )
        {
            context._pCmd->setVertexBuffer( 0, head._vertexBuffer, sizeof( RHIVertex ), 0 );
            ioBoundVertexBuffer = head._vertexBuffer;
        }

        // **이 머티리얼의 퍼뮤테이션**으로 그린다. 주의: 패스 PSO 하나로 모두 그리면 머티리얼이 선언한
        // 정적 스위치(유리의 MATERIAL_BLEND_TRANSLUCENT 같은)가 쿠킹되기만 하고 한 번도 걸리지 않는다.
        // 캐시는 ensureMaterialPSOs 가 기록 전에 채운다. 여기서는 조회만 한다.
        RenderPSOCache::MaterialPSOState psoState = RenderPSOCache::MaterialPSOState::Ready;
        const RHIPipelineStateHandle     batchPSO = _psoManager.psoForBatch( pso, head, drawMode, &psoState );
        // 변형이 `Ready` 가 아닌 묶음(0)은 그리지 않는다 — 앞 묶음의 PSO 로 그리면 틀린 그림이다. 실패는 만들 때 오류를 남겼고, 만드는 중이면 이번 프레임만 건너뛴다.
        if ( batchPSO == 0 )
        {
            if ( psoState == RenderPSOCache::MaterialPSOState::Building )
                SW_PROFILE_COUNT( "RT.PSO.pendingBatches", 1 );
            return false;
        }
        if ( batchPSO != ioBoundPSO )
        {
            context._pCmd->setPipelineState( batchPSO );
            ioBoundPSO = batchPSO;
        }

        // 루트 상수 = { 머티리얼 원소 수 }. 그룹 안에서 같다.
        registerMaterialBuffer( context, head, batchPSO );
        bindForDraw( context, batchPSO, head._materialCb, head._arrMaterialTexSRV );
        context._pCmd->drawIndirect( argsBuffer, argIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), groupCount );
        return true;
    }

    bool FramePassExecutor::drawParticleRun( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot, uint32& inOutCursor, uint32 position,
                                             RHIBufferHandle slotStream )
    {
        uint32 slotEnd = inOutCursor;
        while ( slotEnd < listSlot.size() && listSlot[slotEnd]._drawPosition <= position )
        {
            ++slotEnd;
        }
        if ( slotEnd == inOutCursor )
            return false;
        const vector_reference<const GPUTransparentParticleSlot> listRun = listSlot.subspan( inOutCursor, slotEnd - inOutCursor );
        inOutCursor                                                      = slotEnd;
        if ( drawParticleSlots( context, listRun ) == false )
            return false;
        // 메시 드로우가 읽는 패스 단위 상태를 되돌린다 — 입자 그리기가 인스턴스 슬롯 스트림을 비웠고 PSO 가 바뀌어 에뮬 텍스처 슬롯이 비었다.
        if ( slotStream != 0 )
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, slotStream, constant::kInstanceSlotStreamStride, 0 );
        commitBindlessTextureBindings( context );
        return true;
    }

    bool FramePassExecutor::drawParticleSlots( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot )
    {
        const vector<GPUParticleBatch>& listBatch   = _state._gpuScene.getParticleBatches();
        const RHIPipelineStateHandle    pso         = _psoManager.findParticlePSO();
        const bool                      bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || listBatch.empty() || listSlot.empty() || pso == 0;
        if ( bCannotDraw )
            return false;
        // 정점은 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼 슬롯과 인스턴스 슬롯 스트림은 비운다(캔버스 · 풀스크린 패스와 같다).
        context._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        context._pCmd->setPipelineState( pso );
        setIdentityWorld( context );
        // PassCB(g_ViewProj · g_InvViewProj)를 건다. 입자 버퍼 · 텍스처 · 루트 상수는 그 뒤에 입자 렌더러가 건다.
        bindForDraw( context, pso, kInvalidDescriptorIndex );
        (void)_state._particleRenderer.drawBatches( *context._pCmd, listBatch, listSlot, _pDevice->supportsNativeBindlessSampling() );
        return true;
    }

    void FramePassExecutor::drawLight2DCookieAtlas()
    {
        const vector<Light2DCookieAtlas::CellDraw>& listDraw       = _state._cookieAtlas.getPendingDraws();
        const Texture2D*                            pAtlas         = _state._cookieAtlas.findAtlas();
        const bool                                  bNothingToDraw = listDraw.empty() || pAtlas == nullptr || _pDevice == nullptr || _pCmd == nullptr;
        if ( bNothingToDraw )
        {
            _state._cookieAtlas.clearPendingDraws();
            return;
        }

        // 칸에 옮겨 그리는 PSO(전체 화면 블릿 셰이더, 아틀라스 포맷)는 PSO 관리자가 저장소에 둔다 — 디바이스를 바꾸면 저장소가 비고 다시 만든다.
        const RHIPipelineStateHandle pso = _psoManager.findOrCreateCookieAtlasPSO( pAtlas->getFormat() );
        if ( pso == 0 )
        {
            _state._cookieAtlas.clearPendingDraws();
            return;
        }

        // 상태 전이는 렌더 패스 밖에서 — 쿠키는 읽기, 아틀라스는 그리기.
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            _pCmd->prepareTextureForShaderRead( draw._pTexture->getHandle() );
        }
        _pCmd->prepareTextureForRenderTarget( pAtlas->getHandle() );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = pAtlas->getHandle();
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load; // 다른 칸은 지난 프레임에 그린 그대로다
        beginInfo._depthTarget       = 0;
        beginInfo._depthLoadOp       = RHIRenderPassLoadOp::DontCare;
        beginInfo._width             = shaderslot::kLight2DCookieAtlasSize;
        beginInfo._height            = shaderslot::kLight2DCookieAtlasSize;
        _pCmd->beginRenderPass( beginInfo );

        // 칸마다 뷰포트를 그 칸으로 잡고 쿠키를 원본(SourceColor)으로 걸어 블릿한다. 원본이 칸마다 달라 상수 슬롯도 칸마다 새로 잡는다(그림자 타일과 같은 이유).
        FramePassContext context = _state._frameContext;
        context._pCmd            = _pCmd;
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            uint32 cellX{ 0 };
            uint32 cellY{ 0 };
            uint32 cellSize{ 0 };
            Light2DCookieAtlas::computeCellRect( draw._cell, cellX, cellY, cellSize );
            RHIViewport viewport{};
            viewport._x      = static_cast<float32>( cellX );
            viewport._y      = static_cast<float32>( cellY );
            viewport._width  = static_cast<float32>( cellSize );
            viewport._height = static_cast<float32>( cellSize );
            _pCmd->setViewport( viewport );
            context._resourceRegistry.reset();
            context._resourceRegistry.registerTexture( attachmentNames()._sourceColor, draw._pTexture->getHandle(), draw._pTexture->getSRV() );
            if ( acquirePassCb( context ) == false ) // 링 고갈 — 이 쿠키 칸은 그리지 않는다
                continue;
            drawFullscreen( context, pso );
        }
        _pCmd->endRenderPass();
        _pCmd->prepareTextureForShaderRead( pAtlas->getHandle() );
        _state._cookieAtlas.clearPendingDraws();
    }

    void FramePassExecutor::drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        // PSO 가 없으면 그리지 않는다 — 앞서 묶인 파이프라인이나 백엔드 기본 파이프라인으로 대신 그리지 않는다(없는 이유는 셋업이 알렸다).
        if ( context._pCmd == nullptr || pso == 0 )
            return;
        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
        context._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        context._pCmd->setPipelineState( pso );
        bindForDraw( context, pso, kInvalidDescriptorIndex );
        context._pCmd->draw( 3, 0 );
    }

    bool ExtensionPassContext::drawFullscreen( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawModuleFullscreen( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    bool ExtensionPassContext::drawProxyElements( vector_reference<const ExtensionConstant> constants )
    {
        return _pExecutor->drawExtensionElements( *static_cast<FramePassContext*>( _pPassContext ), *static_cast<const ExtensionFramePass*>( _pPass ), constants );
    }

    void FramePassExecutor::executeExtensionPass( FramePassContext& context, const ExtensionFramePass& pass )
    {
        if ( context._pCmd == nullptr )
            return;
        GPUBreadcrumbs::mark( *context._pCmd, pass._graphName.view() );
        context._pCmd->beginEventMarker( pass._graphName.c_str() );
        context._resourceRegistry.reset();
        context._passType = RenderPassType::Invalid;
        registerLightBuffer( context );
        ExtensionViewInfo view{};
        view._bMainView = _pActiveView == &_state._mainView;
        view._width     = activePool().getWidth();
        view._height    = activePool().getHeight();
        ExtensionPassContext moduleContext( *this, &context, &pass, view );
        pass._execute( moduleContext );
        context._pCmd->endEventMarker();
    }

    bool FramePassExecutor::drawModuleFullscreen( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            FrameExtensionHost::logFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            FrameExtensionHost::logFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            // 이 뷰에서 빠진 첨부(꺼진 효과의 출력)는 걸지 않는다 — 셰이더는 그 역할의 기본값을 쓴다.
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ExtensionConstantScope<FramePassContext> constantScope( context );
        constantScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        drawFullscreen( context, pass._pso );
        context._pCmd->endRenderPass();
        return true;
    }

    bool FramePassExecutor::drawExtensionElements( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant )
    {
        if ( pass._pso == 0 || context._pCmd == nullptr )
        {
            FrameExtensionHost::logFailureOnce( string( "nodraw:" ) + string( pass._graphName.view() ), "pass has no program - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( listConstant.size() > kExtensionMaxConstantCount )
        {
            FrameExtensionHost::logFailureOnce( string( "constants:" ) + string( pass._graphName.view() ), "pass has too many constants - nothing is drawn", pass._graphName.view() );
            return false;
        }
        if ( _extensionHost.getDrawElements().empty() )
            return true; // 그릴 것이 없으면 렌더 패스를 열지 않는다(열면 타깃의 첫 접근이 클리어로 소비된다)
        for ( const ExtensionPassRead& read : pass._params._listRead )
        {
            if ( activePool().resolveName( read._attachment ).empty() == false )
                registerPassTexture( context, inputRoleName( read._role ), read._attachment );
        }
        ExtensionConstantScope<FramePassContext> passScope( context );
        passScope.apply( listConstant.data(), static_cast<uint32>( listConstant.size() ) );
        const float4              targetClear = getTargetClearColor( pass._colorTarget.view() );
        const RHIRenderPassLoadOp depthLoad   = pass._depthTarget.empty() ? RHIRenderPassLoadOp::Load : colorLoadFor( pass._depthTarget );
        if ( beginColorPass( context, pass._colorTarget.view(), pass._depthTarget.view(), targetClear, colorLoadFor( pass._colorTarget ), depthLoad ) == false )
            return false;
        bool bAllDrawn{ true };
        for ( const ExtensionDrawElement& element : _extensionHost.getDrawElements() )
        {
            // 메시 표는 셋업(`FrameExtensionHost::collectFrame`)이 채웠다 — 기록 중에는 읽기만 한다. 없으면 셋업이 이유를 남겼다.
            const Mesh* pMesh = _extensionHost.findMesh( element._mesh );
            if ( pMesh == nullptr || pMesh->isRHIValid() == false )
            {
                bAllDrawn = false;
                continue;
            }
            const Mesh&  mesh        = *pMesh;
            const uint32 vertexCount = mesh.getVertexCount();
            if ( vertexCount == 0 )
                continue;
            // 요소마다 상수가 다르다 — 새 패스 상수 슬롯을 잡는다(앞 요소의 슬롯을 덮으면 GPU 가 나중에 읽는 백엔드에서 모두 마지막 값을 본다). 링이 바닥나면 이 요소부터 그리지 않는다.
            if ( acquirePassCb( context ) == false )
            {
                bAllDrawn = false;
                break;
            }
            ExtensionConstantScope<FramePassContext> elementScope( context );
            elementScope.apply( element._arrConstant, element._constantCount );
            context._world = element._world;
            context._pCmd->setVertexBuffer( 0, mesh.getVertexBuffer(), sizeof( RHIVertex ), 0 );
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
            context._pCmd->setPipelineState( pass._pso );
            commitBindlessTextureBindings( context );
            bindForDraw( context, pass._pso, kInvalidDescriptorIndex );
            context._pCmd->drawInstanced( vertexCount, element._instanceCount );
        }
        setIdentityWorld( context );
        context._pCmd->endRenderPass();
        return bAllDrawn;
    }
} // namespace sw
