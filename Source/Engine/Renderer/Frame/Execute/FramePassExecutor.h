/**
 * @file FramePassExecutor.h
 * @brief 한 프레임의 기록과 제출입니다 — 컴퓨트 프리패스(변형 · 컬링 · 라이트 그리드 · 깊이 히스토리 · 가림 타일), 그래프 패스 콜백과 패스별 실행, 드로우, 뷰별 커맨드 리스트 제출.
 * @details 렌더러(`FrameRenderer`)가 셋업(스냅샷 받기 · 업로드 · 뷰 준비 · PSO 준비)을 끝낸 뒤 `submitGraph` 를 부르면 여기서 기록이 시작된다.
 *          프레임 데이터(`FrameRenderState`)는 렌더러가 소유하고 이 객체는 참조로 읽는다. 언리얼 `FSceneRenderer::Render` 가 뷰마다 RDG 패스(람다)를 더하고
 *          실행하는 자리와 같다 — 패스는 패스 컨텍스트(`FramePassContext`)와 지금 그리는 뷰(`FrameViewTarget`)만 본다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Debug/GPUAssert.h"
#include "Engine/Graphics/Debug/GPUNanCheck.h"
#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Frame/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/FrameRenderState.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"

namespace sw
{
    class FrameCapture;
    class FramePostProcess;
    class FramePSOManager;
    class IRHICommandList;
    class IRHIDevice;
    class RenderPipelineAsset;

    /**
     * @class FramePassExecutor
     * @brief 렌더러가 소유하는 패스 실행기입니다. 프레임 데이터와 협력 객체(PSO · 후처리 · 확장 · 캡처)를 참조로 받고, 기록에만 쓰는 상태(커맨드 리스트 · 패스 컨텍스트 슬롯 ·
     *        래치 · 컬링 자리표 · 가림 되읽기 · GPU assert · NaN 감시)를 가집니다.
     * @details 디바이스는 렌더러의 `_pDevice` 변수를 참조한다(다른 협력 객체와 같다). 그래프 콜백은 `RenderGraphPassContext` 만 받으므로 지금 그리는 뷰는
     *          이 객체가 기억한다(`_pActiveView` — 프레임 밖에서는 늘 주 시점). 뷰를 바꾸는 곳은 `renderExtraView` 하나다.
     *          셋업이 쓰는 뷰 상수(`applyViewPassConstants` · `applyViewProjection`)는 뷰를 인자로 받는다.
     */
    class SW_API FramePassExecutor
    {
    public:
        FramePassExecutor( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FrameRenderState& state, FramePSOManager& psoManager,
                           FramePostProcess& postProcess, FrameExtensionHost& extensionHost, FrameCapture& capture );
        ~FramePassExecutor(); ///< 밖에 둔다 — 내보내는 클래스의 소멸자는 쓰는 곳마다 만들어져 앞선 선언만 본 `unique_ptr<IRHICommandList>` 을 지우지 못한다
        FramePassExecutor( const FramePassExecutor& )            = delete;
        FramePassExecutor& operator=( const FramePassExecutor& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 수명: 상주 자원과 디바이스
        // ------------------------------------------------------------------------------
        /** @brief 기록이 쓰는 상주 자원(GPU assert 링 · NaN 감시 · 정렬 · 컬링 자리표)을 만듭니다. assert 링을 만들지 못하면 멈춥니다(폴백 없음). 엔진 PSO 보다 먼저 부릅니다. */
        void ensureResources();
        /** @brief 상주 자원과 가림 되읽기 · 우편함을 놓습니다. 디바이스가 없으면 핸들만 잊습니다. */
        void releaseResources();
        /** @brief 디바이스가 바뀌거나 내려갈 때 프레임 리스트와 그래프 실행 상태(이름별 리소스 상태)를 버립니다 — 새 디바이스의 텍스처는 다시 Undefined 다. */
        void forgetDevice();

        // ------------------------------------------------------------------------------
        // 2) 셋업이 부르는 준비(기록 전)
        // ------------------------------------------------------------------------------
        /**
         * @brief 파이프라인 패스와 모듈 확장 패스를 그래프 노드로 묶고 컴파일합니다. 컴파일했으면 true 입니다(렌더러가 "묶였다" 를 적는다).
         * @details 그래프 콜백은 이 객체의 `onGraphPassExecute` · `onGraphLevelPrologue` 다.
         */
        [[nodiscard]] bool bindPassCallbacks();
        /** @brief 이번 프레임에 컴퓨트로 드로우 커맨드를 만들 예정인지 반환합니다. 업로드 전에 GPUScene 에 알리는 데 씁니다. */
        bool usesGPUGeneratedCommands() const;
        /** @brief `gv_meshMorphDebugMode` 가 모드 열거자인지 봅니다. 에디터 · 콘솔이 숫자로 넣은 범위 밖 값이면 오류를 남기고 false 입니다(`initialize` 가 멈춘다). */
        [[nodiscard]] static bool isMeshMorphDiagValid();
        /**
         * @brief 모프 풀을 이번 프레임의 배치 메시에 맞추고 배치에 풀 오프셋을 기록합니다. **업로드 전에** 부릅니다.
         * @details 오프셋은 배치 테이블(g_SwBatches)에 실려 upload() 가 올리므로 테이블은 업로드 시점에 완성되어 있어야 한다. 디스패치(dispatchMeshMorph)는 커맨드 리스트가 열린 뒤 따로 돈다.
         */
        void prepareMeshMorphPool();
        /** @brief GPU 입자 시뮬레이션의 이번 프레임 입력(PSO · 주 시점 깊이 히스토리 · 인스턴스 버퍼)입니다. GPUScene 을 올린 뒤 부릅니다. */
        VFXSimulationFrameInput makeVFXSimulationInput() const;
        /** @brief 이번 프레임 한 뷰의 그림자 패스가 그리는 타일 수입니다(상수 버퍼 링 용량 산정). */
        uint32 computeShadowTileCountPerView() const;
        /**
         * @brief 풀스크린 패스가 이 역할의 입력을 바인딩하지 **않게** 합니다. 언리얼의 r.AmbientOcclusion.Levels=0 같은 쇼 플래그입니다.
         * @details 셰이더는 그 인덱스를 kInvalidIndex 로 읽고 폴백한다(AO 는 1). 같은 프레임 안에서 "이 입력이 실제로 이미지를 바꾸는가" 를 비교할 수 있다.
         *          입력을 매 프레임 계산하기만 하고 화면에는 쓰지 않는 결함을 픽셀로 잡는다.
         */
        void setInputRoleEnabled( RenderPassInputRole role, bool bEnabled );
        /**
         * @brief 이 패스가 이 배치를 그리는지 반환합니다. 패스가 머티리얼 define 으로 배치를 거르면(메시 외곽선) 퍼뮤테이션에 그 define 이 있어야 합니다.
         * @details 판정은 `FrameRendererUtil::drawsMaterialInPass` 하나로 한다. 머티리얼 PSO 변형과 셰이더 쿠커도 같은 판정을 쓴다.
         */
        bool drawsBatchInPass( RenderPassType passType, const GPUMeshBatch& batch ) const;

        // ------------------------------------------------------------------------------
        // 3) 뷰 패스 상수(셋업의 주 시점 시드와 추가 뷰 기록이 함께 쓴다)
        // ------------------------------------------------------------------------------
        /** @brief 뷰 하나의 방향광 그림자 상수(`g_LightViewProj` · `g_ShadowParams` · `g_ShadowCascadeLayout` · `g_ShadowCascade0..3`)를 패스 상수에 씁니다. */
        void applyViewShadowConstants( FramePassContext& context, const ViewShadowData& shadow );
        /** @brief 뷰 @p view 에 따라 달라지는 패스 상수를 채웁니다. 외곽선 텍셀 크기, 카메라 후처리 · 렌즈 값, 패스 플래그(후처리 끄기)가 해당합니다. */
        void applyViewPassConstants( FramePassContext& context, FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 뷰-투영 행렬과 **그 역행렬**을 함께 적용합니다. 그 뷰의 컬링 입력과 지난 뷰-투영(`_prevViewProj`)도 함께 바꿉니다.
         * @details 둘을 따로 채우면 언젠가 한쪽만 갱신된다. 그러면 디퍼드가 복원한 월드 위치가
         *          지난 프레임의 카메라를 가리키고, 증상은 "빛이 한 프레임 늦게 따라온다" 로 나타난다.
         */
        void applyViewProjection( FramePassContext& context, FrameViewTarget& view, const float4x4& viewProj );
        /** @brief 월드 행렬을 단위 행렬로 설정합니다. */
        void setIdentityWorld( FramePassContext& context );
        /** @brief 주 시점의 이번 프레임 기록 상태(첨부 클리어 기록 · 깊이 프리패스 표시)를 비웁니다. 셋업이 프레임 시작에 부릅니다. */
        void resetViewRecordState();

        // ------------------------------------------------------------------------------
        // 4) 커맨드 리스트와 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() 와 executePacket() 이 공통으로 쓰는 함수입니다. commandList 를 준비하고 device 에 연결합니다.
         * @param pCallerName 오류 로그에 찍을 호출자 이름
         * @return 성공하면 true 입니다. false 면 호출자가 바로 반환해야 합니다.
         */
        [[nodiscard]] bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );
        /**
         * @brief 컴퓨트 프리패스를 기록해 내고, 렌더 텍스처 뷰 · 주 시점 · 화면 사각형 뷰 · 호스트 타깃 뷰 순서로 그래프를 기록해 제출합니다.
         * @return 주 시점 그래프 실행의 결과
         */
        [[nodiscard]] bool submitGraph( IRHIDevice* pDevice );

        // ------------------------------------------------------------------------------
        // 5) 조회
        // ------------------------------------------------------------------------------
        /** @brief 주 시점 가림 타일을 몇 프레임 늦게 되읽은 가장 최근 사본입니다(`FrameRenderer::getOcclusionSnapshot`). 어느 스레드에서 불러도 됩니다. */
        shared_ptr<const OcclusionTileSnapshot> getOcclusionSnapshot() const { return _occlusionMailbox.getLatest(); }
        /** @brief 지난 `submitGraph` 가 낸 씬 간접 드로우 호출 수입니다(모든 패스 합). */
        uint32 getIndirectDrawCallCount() const { return _indirectDrawCallCount.load( std::memory_order_relaxed ); }
        /** @brief 지난 `submitGraph` 가 실제로 그린 추가 뷰 수입니다. */
        uint32 getLastRenderedExtraViewCount() const { return _lastRenderedExtraViewCount; }

    private:
        friend class ExtensionPassContext;

        /** @brief 주 출력에 그리는 패스(Present, Canvas)의 대상입니다. 지금 뷰가 정하며 `resolvePresentTarget` 이 구합니다. */
        struct PresentTarget
        {
            RHITextureHandle _texture{ 0 }; ///< 0이면 백버퍼
            uint32           _width{ 0 };
            uint32           _height{ 0 };
            RHIFormat        _format{ RHIFormat::Unknown };   ///< 대상의 실제 포맷(PSO 를 고를 때 쓴다)
            uint8            _bOwnOutput{ SW_FALSE };         ///< 추가 뷰의 자기 출력 전체(렌더 텍스처, 호스트 타깃)
            uint8            _bCapture{ SW_FALSE };           ///< 백버퍼 대신 스크린샷 캡처 텍스처에 그린다.
            uint8            _bCaptureToBack{ SW_FALSE };     ///< Swapchain 을 쓰는 마지막 패스 뒤에 캡처를 백버퍼로 복사한다(출력이 곧 백버퍼일 때).
            uint8            _bCaptureFromOutput{ SW_FALSE }; ///< Swapchain 을 쓰는 마지막 패스 뒤에 출력 RT(에디터 게임 뷰)를 캡처로 복사한다.
        };
        /** @brief 패스 하나를 실행할 때 패스별 실행 함수가 함께 쓰는 입력입니다. `executePass` 가 한 번 정합니다. */
        struct PassExecuteInput
        {
            FramePassContext&                                      _context;
            RenderPassType                                         _passType;
            const RenderGraphPassDesc&                             _passDesc;          ///< 패스 서술
            const vector<RenderGraphPassDesc::ResolvedAttachment>& _declaredColor;     ///< 선언한 컬러 출력(선언 순서). kDrawsDeclaredColor 패스는 비어 있지 않다
            hashed_string                                          _passDepth;         ///< 바인딩할 뎁스. 선언이 없거나 이번 프레임에 없으면 빈 이름
            bool                                                   _bTransparentBatch; ///< 이 패스가 투명 배치를 그리는지(테이블 플래그)
        };
        /** @brief `drawGPUBatches` 가 한 패스 동안 들고 가는 값입니다 — 그릴 배치 · 입자 자리, 지금 걸린 PSO · 정점 버퍼, 입자 커서, 그린 호출 수. */
        struct BatchDrawState
        {
            vector_reference<const GPUMeshBatch>               _batches{};
            vector_reference<const GPUTransparentParticleSlot> _listParticleSlot{};
            const vector<uint32>*                              _pOrder{ nullptr }; ///< 추가 뷰의 투명 패스가 도는 배치 순서(없으면 배치 번호 순서)
            RHIPipelineStateHandle                             _pso{ 0 };
            RHIPipelineStateHandle                             _boundPSO{ 0 }; ///< 지금 커맨드 리스트에 걸린 PSO(배치마다 다를 수 있어 바뀔 때만 다시 건다)
            RHIBufferHandle                                    _boundVertexBuffer{ 0 };
            RHIBufferHandle                                    _slotStream{ 0 }; ///< 인스턴스 슬롯 스트림(슬롯 1)
            RHIBufferHandle                                    _argsBuffer{ 0 }; ///< 이 패스의 뷰가 만든 간접 인자
            RenderPassType                                     _passType{};
            uint32                                             _batchOffset{ 0 }; ///< 간접 인자 슬롯의 시작(불투명 다음 투명)
            uint32                                             _particleSlotCursor{ 0 };
            uint32                                             _drawCallCount{ 0 };
            bool                                               _bMerge{ false }; ///< 연속 배치를 멀티 드로우 한 번으로 묶는지
        };
        /// @brief 주 시점 가림 타일의 되읽기 고리, 게임 스레드가 가져가는 우편함, 요청 때의 메타데이터(요청 번호 % 개수 로 찾는다)입니다. 요청한 프레임과 사본이 도착하는 프레임은 다르다.
        struct OcclusionReadbackMeta
        {
            float4x4 _viewProj{};
            uint32   _columns{ 0 };
            uint32   _rows{ 0 };
            float32  _depthWidth{ 0.0f };
            float32  _depthHeight{ 0.0f };
            uint8    _bValid{ SW_FALSE }; ///< 이 요청의 타일이 판정에 쓸 수 있는 프레임이었다(기록을 버리는 프레임이 아니었다)
        };
        static constexpr uint32 kOcclusionReadbackMetaCount = 8;

        // ------------------------------------------------------------------------------
        // 그래프 콜백과 패스별 실행(FramePassExecutorPasses.cpp)
        // ------------------------------------------------------------------------------
        /** @brief RenderGraph 가 패스를 실행할 때 부르는 콜백입니다. */
        void onGraphPassExecute( const RenderGraphPassContext& context );
        /**
         * @brief 레벨을 기록하기 **직전에** 그 레벨이 만질 자원의 배리어를 미리 발행합니다. 직렬 경로에서는 패스마다 발행합니다.
         * @details 자원 이름으로 실제 텍스처를 찾아 `prepareTextureForShaderRead` 와
         *          `prepareTextureForRenderTarget` 을 레벨 첫 패스 리스트의 앞머리에 기록한다(직렬 경로에서는
         *          그 패스의 리스트). 같은 레벨의 다른 리스트는 큐 순서상 그 뒤에 실행되므로 GPU 타임라인에서도 배리어가 앞선다.
         *
         *          이렇게 하면 패스 콜백이 이미 맞는 상태를 보므로 기록 중에 리소스 상태를 바꾸지
         *          않는다. 주의: 병렬 기록 스레드가 배리어를 정하게 하면 중복 배리어와 레이아웃 불일치가 생긴다.
         */
        void onGraphLevelPrologue( const RenderGraphLevelContext& context );
        /**
         * @brief 패스 타입에 맞는 실행 함수를 부릅니다. 공통 입력(`PassExecuteInput`)을 한 번 정하고 패스별 `execute*Pass` 로 나눠 줍니다.
         * @details 패스 서술을 참조로 받는다 — 서술 없이 원본 이름(SceneColor · TAAColor 등)으로 타깃을 짐작하던 길을 형식으로 막았다.
         *          컬러 타깃은 서술의 `_listResolvedColorOutput` 이고 로드가 그 선언을 요구한다(`RenderPassTraitFlag::kDrawsDeclaredColor`).
         */
        void executePass( FramePassContext& context, const RenderGraphPassDesc& passDesc );
        /** @brief 첨부의 이번 프레임 첫 사용이면 Clear, 아니면 Load 입니다(`markAttachmentCleared`). */
        RHIRenderPassLoadOp colorLoadFor( const hashed_string& name );
        /** @brief @p targetName 하나에 풀스크린 삼각형을 그립니다. 열지 못하면 아무것도 하지 않습니다. */
        void executeFullscreenInto( FramePassContext& context, RHIPipelineStateHandle pso, const hashed_string& targetName, const float4& passClearColor );
        /** @brief 일반 풀스크린 패스(테이블의 kGenericFullscreen)입니다. 선언한 입력을 모두 바인딩하고 선언한 첫 컬러 출력에 그립니다. 이 뷰에서 빠진 패스(`FrameViewResources::applyViewPassSelection`)는 기록하지 않습니다. */
        void executeGenericFullscreenPass( const PassExecuteInput& input );
        /** @brief ForwardOpaque · ForwardOnlyOpaque 입니다. 그림자 맵 입력을 바인딩하고 선언한 첫 컬러 출력에 그립니다. */
        void executeForwardOpaquePass( const PassExecuteInput& input );
        /** @brief G버퍼(알베도 · 노멀 MRT)입니다. 노멀 출력이 없거나 PSO 가 없으면 그리지 않습니다. */
        void executeGBufferPass( const PassExecuteInput& input );
        /** @brief 투명 큐(메시 투명 배치와 입자)입니다. */
        void executeTransparentPass( const PassExecuteInput& input );
        /** @brief 장면 색 사본입니다. 원본 색 입력을 선언한 컬러 출력으로 복사합니다(반투명 앞 — 물의 굴절이 읽는다). 셰이더 · PSO 가 없습니다. */
        void executeSceneColorCopyPass( const PassExecuteInput& input );
        /** @brief 인버티드 헐 외곽선입니다. 와이어프레임 · 표면 값 보기에서는 빠집니다. */
        void executeMeshOutlinePass( const PassExecuteInput& input );
        /** @brief 인스턴스 강조 외곽선 패스(HighlightOutline · HighlightSense)를 실행합니다 — 패스의 강조 방식인 인스턴스만 정점이 남습니다. */
        void executeHighlightPass( const PassExecuteInput& input );
        /** @brief 하늘입니다. 씬에 하늘이 없거나 뎁스 · 컬러 출력이 없으면 그리지 않습니다. */
        void executeSkyPass( const PassExecuteInput& input );
        /** @brief 안개 패스(`RenderPassType::Fog`)를 실행합니다. 안개가 없거나 표면 값 보기에서는 그리지 않습니다. 안개 상수 · 볼륨 버퍼는 `FramePostProcess` 가 정해 둔 것을 읽습니다. */
        void executeFogPass( const PassExecuteInput& input );
        /** @brief TAA 와 히스토리 갱신입니다. 후처리를 끈 뷰와 컷 프레임은 원본을 넘깁니다. */
        void executeTAAPass( const PassExecuteInput& input );
        /** @brief 최종 출력(백버퍼 · 캡처 · 렌더 텍스처)으로의 블릿입니다. 화면 사각형 · 정수 업스케일 뷰포트를 적용합니다. */
        void executePresentPass( const PassExecuteInput& input );
        /** @brief 화면 2D 를 주 출력에 그립니다. 주 출력에 마지막으로 그리는 뷰만 그립니다. */
        void executeCanvasPass( const PassExecuteInput& input );
        /**
         * @brief XML 이 선언한 입력을 **역할 이름으로** 모두 바인딩합니다. 풀스크린 패스가 공통으로 씁니다.
         * @details 역할은 로드 시점에 해석해 둔다(`_listResolvedInput`). 여기서 바인딩하는 목록과 검증이 대조한 목록이 같으므로
         *          "선언은 했는데 바인딩되지 않는 입력" 이 생기지 않는다. 쇼 플래그로 끈 역할은 건너뛴다.
         */
        void registerDeclaredInputs( FramePassContext& context, const RenderGraphPassDesc& passDesc );
        /** @brief 역할의 셰이더 이름입니다. intern 된 hashed_string 입니다. */
        const hashed_string& inputRoleName( RenderPassInputRole role ) const;
        /**
         * @brief 그림자 패스 하나를 그립니다 — 지금 뷰의 캐스케이드 타일과 프레임의 점광 · 스폿 타일을 아틀라스의 자기 칸(뷰포트)에 그립니다.
         * @details 타일마다 `g_LightViewProj` 를 그 타일의 행렬로 바꿔 씬을 한 번씩 그린다. 컬링은 모든 타일을 덮는 상자(`ViewShadowData::_cullViewProj`) 하나다.
         */
        void drawShadowTiles( FramePassContext& context, bool bTransparentBatch );
        /** @brief 컬러(및 깊이) 패스를 시작합니다. 열지 못하면 false 를 반환하며, 그때는 그리지도 닫지도 않습니다(`beginColorPassMrt` 도 같습니다). */
        bool beginColorPass( FramePassContext& context, string_view colorName, string_view depthName, const float4& clearColor,
                             RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief MRT 컬러 패스를 시작합니다.
         * @return 열었으면 true 입니다. 컬러 타깃 이름 중 이번 프레임에 없는 것이 있으면 **열지 않고** false 를 반환하며 오류를 한 번 남깁니다.
         *         없는 첨부의 핸들 0 은 백버퍼를 가리키므로, 그대로 열면 패스가 화면에 그립니다.
         */
        bool beginColorPassMrt( FramePassContext& context, const string_view* pColorNames, const float4* pTargetClearColor,
                                const RHIRenderPassLoadOp* pColorLoad, uint32 colorCount, string_view depthName,
                                RHIRenderPassLoadOp depthLoad );
        /** @brief 깊이 전용 패스를 시작합니다. */
        void beginDepthOnlyPass( FramePassContext& context, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief 트랜지언트 텍스처를 리소스 레지스트리에 canonicalName 으로 등록합니다(bindless SRV 자동 매칭).
         * @param canonicalName **미리 intern 해 둔** 이름입니다. 문자열을 받으면 패스마다 다시 intern 하게 되므로
         *        attachmentNames() 의 캐시를 넘기세요.
         */
        void registerPassTexture( FramePassContext& context, const hashed_string& canonicalName, string_view attachmentName );
        /** @brief bindless 텍스처 바인딩을 커밋합니다. PassConstantValues 를 갱신하고, 에뮬레이션 백엔드에는 폴백 바인딩을 합니다. */
        void commitBindlessTextureBindings( FramePassContext& context );
        /**
         * @brief 지금 뷰의 주 출력 대상을 고릅니다. 렌더 텍스처 뷰는 자기 텍스처를, 주 시점과 화면 사각형 뷰는 주 출력(백버퍼 또는 게임 뷰 RT)을 씁니다.
         *        스크린샷 실행이면 캡처 텍스처를 씁니다. Present 와 Canvas 가 같은 판단을 씁니다.
         */
        PresentTarget resolvePresentTarget() const;
        /** @brief 이 패스가 Swapchain 을 쓰는 마지막 패스면 true 입니다. 스크린샷 캡처를 백버퍼로 복사하는 일을 이 패스 끝에서 합니다. */
        bool isLastSwapchainWriter( const RenderGraphPassDesc& passDesc ) const;
        /** @brief 패스 타입으로 파이프라인의 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;

        // ------------------------------------------------------------------------------
        // 드로우(FramePassExecutorDraw.cpp)
        // ------------------------------------------------------------------------------
        /** @brief 드로우 직전에 리플렉션 기반으로 바인딩합니다. PassCB, MaterialCB, 텍스처, 인스턴스 버퍼가 대상입니다. */
        void bindForDraw( FramePassContext& context, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                          const RHIDescriptorIndex* pMaterialTexSRV = nullptr );
        /** @brief GPUScene 인스턴스 구조 버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& context );
        /**
         * @brief 씬 라이트 구조 버퍼를 "SwLights" 이름으로 등록합니다. **모든 패스**에 등록합니다.
         * @details 인스턴스 버퍼와 달리 지오메트리 패스 전용이 아니다. 디퍼드 조명은 풀스크린
         *          패스라 `registerInstanceBuffer` 를 거치지 않는데, 라이트는 바로 거기서 필요하다.
         */
        void registerLightBuffer( FramePassContext& context );
        /** @brief 배치의 머티리얼 데이터 버퍼(GPUScene)를 패스 레지스트리에 "SwMaterials" 이름으로 등록합니다. */
        void registerMaterialBuffer( FramePassContext& context, const GPUMeshBatch& batch, RHIPipelineStateHandle pso );
        /** @brief 씬 메시를 그립니다. GPUScene 이 올라가 있으면 drawGPUBatches 로 넘기고, 아니면 상태만 맞춥니다. */
        void drawSceneMeshes( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass );
        /**
         * @brief GPUScene 배치를 간접 드로우로 그립니다.
         * @param pParticleSlot 투명 패스의 입자 자리(`findActiveParticleSlots`). 주면 배치 순서의 그 자리마다 입자를 끼우고 멀티 드로우 묶음을 그 자리에서 끊으며,
         *                      남은 입자는 모든 배치 뒤에 그린다. 투명 큐가 아닌 패스(외곽선 · 그림자 등)는 nullptr 이다.
         */
        void drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass, const vector<GPUTransparentParticleSlot>* pParticleSlot );
        /** @brief 보기 모드 한 바퀴를 그립니다. 첫 바퀴(@p bDrawsParticles)만 배치 순서의 자기 자리에 입자를 끼웁니다. */
        void drawBatchRound( FramePassContext& context, BatchDrawState& inoutState, RenderViewMode drawMode, bool bDrawsParticles );
        /** @brief @p orderIndex 의 배치와 한 멀티 드로우 묶음이 되는 연속 배치 수를 셉니다(묶지 않으면 1, 다음 입자 자리가 묶음 안에 들면 그 자리에서 끊는다). */
        uint32 countDrawGroup( const BatchDrawState& state, RenderViewMode drawMode, uint32 orderIndex, bool bDrawsParticles ) const;
        /** @brief 그 PSO 의 레이아웃이 머티리얼 CB 슬롯을 거는지입니다. 거는 셰이더에서만 머티리얼 CB 가 멀티 드로우 병합 키다. */
        bool layoutBindsMaterialCb( RHIPipelineStateHandle batchPSO ) const;
        /** @brief @p other 가 @p head 와 같은 멀티 드로우 묶음에 들 수 있는지입니다(그릴 수 있고 정점 버퍼 · PSO · 머티리얼 바인딩이 같다). */
        bool isSameDrawGroup( RHIPipelineStateHandle pso, RenderPassType passType, RenderViewMode drawMode, const GPUMeshBatch& head, const GPUMeshBatch& other ) const;
        /**
         * @brief 묶음 하나를 그립니다 — 바뀐 정점 버퍼 · 배치 PSO 만 다시 걸고, 머티리얼 바인딩 뒤 간접 인자 @p argIndex 부터 @p groupCount 개를 부릅니다.
         * @return 그렸으면 true 입니다. 그 보기 모드의 머티리얼 변형이 없으면(만들 때 오류를 남겼다) 그리지 않고 false 입니다.
         */
        bool drawBatchGroup( FramePassContext& context, RHIPipelineStateHandle pso, RenderViewMode drawMode, const GPUMeshBatch& head, RHIPipelineStateHandle& ioBoundPSO,
                             RHIBufferHandle& ioBoundVertexBuffer, RHIBufferHandle argsBuffer, uint32 argIndex, uint32 groupCount );
        /** @brief 투명 큐(메시 투명 배치와 입자 그리기를 한 순서로)를 지금 열린 투명 패스에 그립니다. */
        void drawTransparentQueue( FramePassContext& context, RHIPipelineStateHandle pso );
        /** @brief 지금 그리는 뷰의 입자 자리입니다. 추가 뷰는 자기 순서가 있으면 그것, 없으면 주 시점의 것(그 뷰도 주 배치 순서로 그린다)입니다. */
        const vector<GPUTransparentParticleSlot>& findActiveParticleSlots() const;
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso );
        /**
         * @brief 입자 그리기 구간 @p listSlot 을 지금 열린 투명 패스에 그 순서로 그립니다.
         * @details 입자 PSO(`FramePSOManager::ensureParticlePSO`)를 걸고 PassCB 를 바인딩한 뒤 `VFXParticleRenderer::drawBatches` 로 넘긴다. PSO · 정점 버퍼 슬롯을 바꾸므로
         *          메시 드로우로 돌아가는 쪽(`drawGPUBatches`)이 그 상태를 다시 건다.
         * @return 입자 상태를 걸었으면 true 입니다(그릴 것이 없거나 PSO 가 없으면 false — 상태도 그대로다)
         */
        bool drawParticleSlots( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot );
        /**
         * @brief @p listSlot 의 @p inOutCursor 부터 자리가 @p position 이하인 입자를 그리고 커서를 그 뒤로 옮깁니다. 메시 드로우로 돌아갈 패스 상태(인스턴스 슬롯
         *        스트림 @p slotStream, 에뮬 텍스처 슬롯)를 다시 겁니다.
         * @return 입자 상태를 걸었으면 true 입니다 — 부르는 쪽은 자기가 기억한 PSO · 정점 버퍼를 잊어야 한다
         */
        bool drawParticleRun( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot, uint32& inOutCursor, uint32 position,
                              RHIBufferHandle slotStream );
        /** @brief 새로 칸을 받은 쿠키를 아틀라스 칸에 그립니다(프레임 프리패스 리스트, `submitGraph`). 그릴 칸이 없으면 아무것도 하지 않습니다. */
        void drawLight2DCookieAtlas();
        /** @brief 씬 배치를 멀티 드로우로 묶을지 반환합니다. `setDrawMergeEnabled` 로 받은 값을 쓰고, 없으면 전역 `gv_drawMerge` 를 씁니다. */
        bool isDrawMergeEnabled() const;

        // ------------------------------------------------------------------------------
        // 모듈 확장 패스의 기록(FramePassExecutorExtension.cpp) — 받아들이기는 `FrameExtensionHost`
        // ------------------------------------------------------------------------------
        /** @brief 모듈 확장 패스 하나를 실행합니다(그래프 노드 콜백이 부른다). 모듈의 실행 콜백에 `ExtensionPassContext` 를 넘깁니다. */
        void executeExtensionPass( FramePassContext& context, const ExtensionFramePass& pass );
        /** @brief `ExtensionPassContext::drawFullscreen` 의 몸체입니다. */
        bool drawModuleFullscreen( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant );
        /** @brief `ExtensionPassContext::drawProxyElements` 의 몸체입니다. */
        bool drawExtensionElements( FramePassContext& context, const ExtensionFramePass& pass, vector_reference<const ExtensionConstant> listConstant );

        // ------------------------------------------------------------------------------
        // 컴퓨트 프리패스(FramePassExecutorCompute.cpp · FramePassExecutorOcclusion.cpp · FramePassExecutorNanCheck.cpp)
        // ------------------------------------------------------------------------------
        /**
         * @brief 인스턴스 애니메이션 컴퓨트를 기록합니다(instanceanimation.hlsl).
         * @details **컬링보다 먼저** 돌아야 한다. 순서가 뒤집히면 컬링이 이번 프레임에 움직이기 전의
         *          바운드로 판정한다. 회전을 요청한 인스턴스가 없으면 통째로 건너뛴다.
         */
        void dispatchInstanceAnimation( uint32 instanceCount );
        /**
         * @brief 모프를 요청한 메시들의 정점을 GPU 가 변형합니다(레스트에서 결과로).
         * @details **컬링보다 앞**이어야 한다. 모프는 회전과 달리 실제로 형태와 바운드를 바꾸므로,
         *          뒤에 두면 컬링이 한 프레임 늦은 형태로 판정한다.
         */
        void dispatchMeshMorph();
        /**
         * @brief 스킨드 메시들의 정점을 GPU 가 본 팔레트로 블렌딩합니다(바인드 포즈에서 모프 풀의 스킨 구간으로). 모프와 같이 **컬링보다 앞**이어야 합니다.
         * @details 디스패치 하나가 모든 스킨드 메시를 처리한다. 정점마다의 팔레트 행 번호에 메시의 팔레트 시작이 이미 더해져 있다.
         */
        void dispatchMeshSkin();
        /** @brief 지금 적용되는 모프 진단 모드입니다. 오버라이드가 있으면 그 값을, 없으면 `gv_meshMorphDebugMode` 를 씁니다. */
        MeshMorphDiagMode getEffectiveMeshMorphDiag() const;
        /**
         * @brief 뷰마다 컬링을 돌리고, 압축된 투명 목록을 CPU 정렬 순서로 되돌립니다(gpucull/instancesort.hlsl).
         * @details 컬링 결과는 절두체에 종속되므로 뷰(메인, 그림자)마다 자기 인자와 목록을 따로 만든다.
         */
        void dispatchCullAndSort( uint32 instanceCount );
        /**
         * @brief 컬링 슬롯 하나를 컬링하고 정렬합니다. 그 뷰의 절두체와 자기 상수 버퍼를 씁니다. 디스패치를 돌렸으면 true 입니다.
         * @param pExtraView 추가 뷰면 그 뷰(투명 순번 테이블을 정렬에 바인딩합니다), 고정 뷰(주 시점, 그림자)면 nullptr 입니다.
         */
        bool dispatchCullView( uint32 cullViewIndex, const RenderView& renderView, uint32 instanceCount, const FrameViewTarget* pExtraView );
        /** @brief 이번 프레임의 LOD 전환 진행량(`_lodFadeStep`)을 렌더 프레임 간격에서 정합니다. 컬링 디스패치 앞에서 한 번 부른다. */
        void computeLODFadeStep();
        /**
         * @brief GPU 생산자로 뷰마다의 라이트 그리드를 채웁니다(lightgrid.hlsl, 그룹 하나 = 뷰 하나). 생산자가 GPU 가 아니면 아무것도 하지 않습니다.
         * @details 그리기 전에, 그래프 밖의 컴퓨트 프리패스로 기록한다(렌더 그래프에 컴퓨트 패스 상태가 없다). 끝나면 그리드 버퍼를 SRV 로 돌린다.
         */
        void dispatchLightGrid();
        /** @brief 실시간 프로브 갱신 컴퓨트를 기록합니다(라이트 그리드 · 볼륨 안개 뒤, 조명 패스 앞). `prepareRealtimeProbes` 가 켠 프레임에만 돕니다. */
        void dispatchRealtimeProbes();
        /** @brief 뷰 @p view 의 지난 깊이를 이번 프레임에 히스토리로 옮길 수 있으면 true 입니다(지난 깊이 · 히스토리 · PSO 가 모두 있다). */
        bool canCopyDepthHistory( const FrameViewTarget& view ) const;
        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰마다 지난 장면 깊이를 깊이 히스토리로 옮기는 풀스크린 하나를 기록합니다(depthhistory.hlsl).
         * @details GPU 입자 시뮬레이션보다 먼저 기록한다 — 깊이 충돌이 이 히스토리를 읽는다. 지금 읽는 쪽은 주 시점뿐이지만 히스토리는 뷰마다 둔다.
         */
        void recordDepthHistories();
        /** @brief 뷰 @p target 하나의 깊이 히스토리 복사를 기록합니다(`recordDepthHistories` 가 뷰마다 부른다). */
        void recordDepthHistory( FrameViewTarget& target );
        /** @brief 이번 프레임에 그린 뷰마다 그 뷰-투영과 눈 위치를 적어 둡니다(다음 프레임의 깊이 히스토리가 짝이 된다). 모든 뷰를 그린 뒤 부릅니다. */
        void rememberDepthHistoryViews();
        /**
         * @brief 컴퓨트 프리패스 리스트에 뷰 @p target 의 가림 타일(깊이 히스토리 → 타일마다 최대 깊이)을 기록합니다(occlusiontiles.hlsl). 깊이 히스토리를 옮긴 뒤 · 컬링 앞입니다.
         * @details `gv_occlusionCulling` 이 꺼졌거나 이번 프레임에 옮긴 지난 깊이가 없으면 짓지 않는다 — 그 뷰의 컬링은 가림을 보지 않는다.
         */
        void recordOcclusionTiles( FrameViewTarget& target );
        /**
         * @brief 주 시점 가림 타일을 되읽는 복사를 컴퓨트 프리패스 리스트에 넣고, 이미 도착한 사본이 있으면 우편함에 올립니다(`getOcclusionSnapshot`).
         * @details `recordOcclusionTiles` 직후에 부른다. 이번 프레임에 타일을 짓지 않았거나 기록을 버리는 프레임이면 우편함을 비운다(그 타일은 지난 장면의 것이라 맞지 않는다).
         *          요청 때의 뷰-투영 · 격자는 `_arrOcclusionReadbackMeta` 에 적어 두었다가 사본이 도착하면 붙인다.
         */
        void requestOcclusionReadback( const FrameViewTarget& view );
        /**
         * @brief 뷰 @p view 의 컬링 상수에 가림 판정 값을 채웁니다. 이번 프레임에 타일을 지었고 기록을 버리는 프레임(처음 · 컷)이 아닐 때만 켭니다.
         * @details 지난 프레임 깊이로 이번 경계를 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나온다(언리얼의 지난 프레임 HZB 와 같은 한계). 컷 프레임은 지난 깊이가
         *          다른 장면이라 보지 않는다.
         */
        void fillOcclusionParams( const FrameViewTarget& view, FrameRendererUtil::GPUCullParams& inoutParams ) const;
        /**
         * @brief 그래프가 끝난 뒤 주 시점의 부동소수 컬러 패스 출력을 NaN · Inf 감시 컴퓨트로 훑습니다(`GPUNanCheck`). 꺼져 있으면(`-gv_nanCheck` 없음 · 에뮬 백엔드) 아무것도 하지 않는다.
         * @details 패스 이름 + 첨부 이름이 로그의 이름이다. 같은 텍스처를 가리키는 출력은 한 번만 훑는다. 텍스처는 훑기 위해 셰이더 읽기 상태로 옮기고 렌더 타깃 상태로 되돌린다.
         */
        void scanPassOutputsForNan( IRHIDevice* pDevice );

        // ------------------------------------------------------------------------------
        // 뷰와 첨부(FramePassExecutor.cpp)
        // ------------------------------------------------------------------------------
        /** @brief 이번 프레임에 그릴 추가 뷰 가운데 출력 종류가 @p outputKind 인 것을 그리고 그린 수를 돌려줍니다. */
        uint32 renderExtraViewsOfKind( IRHIDevice* pDevice, RenderViewOutputKind outputKind );
        /** @brief 주 시점의 그래프를 실행합니다. 병렬 기록이 되면 레벨마다 여러 리스트로, 아니면 주 리스트 하나로 직렬 기록해 제출합니다. */
        [[nodiscard]] bool executeMainGraph( IRHIDevice* pDevice );
        /** @brief 추가 뷰 하나를 그래프로 그립니다(직렬, 자기 리스트). 지금 뷰를 그 뷰로 바꿨다가 주 시점으로 되돌립니다. */
        void renderExtraView( IRHIDevice* pDevice, FrameViewTarget& view );
        /**
         * @brief 이번 프레임의 패스 상수 버퍼 슬롯을 하나 골라 context 에 붙입니다.
         * @details 커맨드 기록은 지연되고 상수 버퍼 쓰기는 즉시 일어난다. 그래서 패스마다 별도 버퍼를
         *          써야 재생 시점에 각 패스의 상수가 남는다. 커서는 원자적이라
         *          병렬 기록에서도 안전하다. 슬롯이 모자라면 false 이고 부르는 쪽은 그리지 않는다(앞 슬롯을 공유하지 않는다).
         */
        [[nodiscard]] bool acquirePassCb( FramePassContext& context );
        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 반환합니다.
         * @details 반환값은 곧 "Clear 로 열어도 되는가" 다. 같은 레벨의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계 구역 안에 있어야 한다. 나누면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지운다.
         */
        bool markAttachmentCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다. 프레임 시작에 부릅니다. */
        void resetClearedAttachments();
        /**
         * @brief 패스 타깃의 클리어 색입니다. 파이프라인 XML 의 첨부 선언(풀이 든 값)이 원본이고, 풀에 없는 이름(백버퍼)은 렌더러의 클리어 색입니다.
         * @details 호출부가 기본값을 넘기지 않는다 — 패스마다 적던 기본값이 선언과 갈라지는 길을 형식으로 막는다.
         */
        float4 getTargetClearColor( string_view attachmentName ) const;
        /**
         * @brief 트랜지언트 텍스처와 그 SRV 를 한 번의 조회로 찾습니다. 없으면 빈 값입니다.
         * @details 이름 하나로 둘 다 필요한 곳(registerPassTexture)이 패스마다 여러 번 돈다. 조회를 한 번만 하므로 해시도 한 번이다.
         */
        TransientAttachmentPool::Attachment findTransientAttachment( string_view name ) const;
        /** @brief 트랜지언트 텍스처 핸들을 찾습니다. 없으면 0 입니다. */
        RHITextureHandle findTransient( string_view name ) const;
        /**
         * @brief Present 소스 어태치먼트 이름을 정합니다. 없으면 빈 뷰입니다.
         * @details 반환한 뷰는 파이프라인 선언(`_listResolvedInput`)이나 폴백 리터럴을 가리킨다. 매 프레임 불리므로 사본을 만들지 않는다.
         *          파이프라인을 바꾸기 전까지만 유효하므로 프레임을 넘겨 보관하지 않는다.
         */
        string_view resolvePresentSource() const;
        /** @brief 지금 그리는 뷰의 트랜지언트 풀입니다. */
        TransientAttachmentPool&       activePool() { return _pActiveView->_transientPool; }
        const TransientAttachmentPool& activePool() const { return _pActiveView->_transientPool; }
        /** @brief 지금 그리는 뷰가 추가 뷰면 true 입니다. */
        bool isRenderingExtraView() const { return _pActiveView != &_state._mainView; }

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고)
        const RenderPipelineAsset& _pipelineResource;
        FrameRenderState&          _state;
        FramePSOManager&           _psoManager;
        FramePostProcess&          _postProcess;
        FrameExtensionHost&        _extensionHost;
        FrameCapture&              _capture;
        /// @brief 지금 그리는 뷰입니다. 프레임 밖에서는 항상 `FrameRenderState::_mainView` 입니다. 바꾸는 곳은 `renderExtraView` 하나다.
        FrameViewTarget* _pActiveView;
        /// @brief 컴퓨트 프리패스 리스트와 그것을 만든 디바이스입니다. `_pCmd` 는 프리패스를 기록하는 동안만 그 리스트를 가리킨다.
        IRHIDevice*                 _pCmdOwnerDevice;
        unique_ptr<IRHICommandList> _frameCmd;
        IRHICommandList*            _pCmd;
        /**
         * @brief 패스 슬롯별 컨텍스트입니다. 프레임마다 `_frameContext` 를 **대입**해 씁니다. 용량이 남아 있어 힙을 건드리지 않습니다.
         * @details 패스마다 지역 복사본을 만들면 상수 값 목록과 레지스트리 맵이 프레임마다 패스 수만큼 새로
         *          늘어난다. 크기는 병렬 기록 **전**(`submitGraph`)에 맞춘다. 기록 중에 늘리면 워커끼리 경합한다.
         *          마지막 슬롯은 이름을 찾지 못한 패스가 쓴다.
         */
        vector<FramePassContext>             _listPassContext;
        unordered_map<hashed_string, uint32> _mapPassNameToIndex;
        /// @brief 모듈 확장 패스(`FrameExtensionHost::getPasses`)의 그래프 노드 이름 → 목록 번호입니다. `bindPassCallbacks` 가 채웁니다.
        unordered_map<hashed_string, uint32> _mapExtensionPassIndex;
        /// @brief 그래프 실행 상태(이름별 리소스 상태 추적)입니다. 디바이스가 바뀌면 버린다(`forgetDevice`).
        RenderGraphExecutionContext _graphContext;
        /// @brief 순번 테이블이 없는 뷰(주 시점, 그림자)의 정렬 디스패치가 t2 에 바인딩하는 원소 하나짜리 자리 표시 버퍼입니다. 셰이더는 플래그(`g_UseViewRank`)가 0 이면 읽지 않습니다.
        RHIStructuredBufferSlot _transparentRankPlaceholder;
        /// @brief 가림 타일이 없는 컬링(그림자 슬롯 · 가림을 보지 않는 뷰)이 t2 에 바인딩하는 원소 하나(깊이 1 — 아무것도 가리지 않는다)짜리 자리 표시 버퍼입니다. 셰이더는 가림이 꺼지면 읽지 않습니다.
        RHIStructuredBufferSlot _occlusionTilesPlaceholder;
        /// @brief 주 시점 가림 타일의 되읽기 고리, 게임 스레드가 가져가는 우편함, 요청 때의 메타데이터(요청 번호 % 개수 로 찾는다)입니다.
        AsyncBufferReadback   _occlusionReadback;
        OcclusionTileMailbox  _occlusionMailbox;
        OcclusionReadbackMeta _arrOcclusionReadbackMeta[kOcclusionReadbackMetaCount];
        uint64                _occlusionReadbackSerial;
        /// @brief 셰이더 assert 링(`-gv_gpuAssert`). 컴퓨트 프리패스의 assert 가 켜진 디스패치가 u4 로 쓰고, 프리패스 끝에 되읽기를 요청하며 다음 프레임들이 로그로 낸다. 꺼져 있으면 비어 있다.
        GPUAssertMonitor _gpuAssert;
        /// @brief 패스 출력 NaN · Inf 감시(`-gv_nanCheck`). 그래프가 끝난 뒤 `scanPassOutputsForNan` 이 자기 리스트(`_nanCheckCommandList`)에 기록한다. 꺼져 있으면 비어 있다.
        GPUNanCheck                 _nanCheck;
        unique_ptr<IRHICommandList> _nanCheckCommandList;
        /// @brief Swapchain 을 쓰는 마지막 패스입니다. 선언 순서 기준이며, 그래프가 같은 출력에 대한 쓰기를 선언 순서로 연결합니다. 콜백을 바인딩할 때 정합니다.
        const RenderGraphPassDesc* _pLastSwapchainWriter;
        /**
         * @brief 이번 프레임에 주 출력의 캔버스(화면 2D)를 그리는 뷰입니다. 주 출력에 마지막으로 그리는 뷰(마지막 화면 사각형 뷰, 없으면 주 시점)입니다.
         * @details 화면 사각형 뷰(PiP · 분할 화면)는 주 시점 뒤에 같은 출력에 겹쳐 그리므로, 주 시점의 Canvas 패스가 UI 를 그리면 그 뷰가 UI 를 덮는다.
         *          언리얼이 모든 장면 뷰 뒤에 Slate 를 한 번 그리는 것과 같은 순서로, 마지막 뷰의 Canvas 패스만 UI 를 그린다. `submitGraph` 가 기록 전에 정한다.
         */
        const FrameViewTarget* _pMainOutputCanvasView;
        /// @brief 이번 프레임의 씬 간접 드로우 호출 수입니다. 패스를 병렬로 기록하므로 atomic 으로 둡니다.
        atomic<uint32> _indirectDrawCallCount;
        /// @brief `setInputRoleEnabled( role, false )` 가 켠 비트입니다. 그 역할의 입력은 바인딩하지 않습니다.
        uint32 _disabledInputRoleMask;
        /// @brief 지난 프레임에 그린 추가 뷰 수입니다.
        uint32 _lastRenderedExtraViewCount;
        /// @brief 이번 프레임의 LOD 전환 진행량(0 ~ 255)입니다. 0 이면 크로스페이드 없이 바로 바꾼다(`dispatchCullAndSort` 가 정하고 `dispatchCullView` 가 컬링 상수로 싣는다).
        uint32 _lodFadeStep;
        /// @brief 지난 컬링 프레임의 시각(나노초)입니다. 전환 진행량의 프레임 간격을 잰다(0 이면 아직 없다).
        int64 _lodFadePreviousNanoseconds;
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는지 여부입니다. 가시 목록이 유효한지를 나타냅니다.
         * @details 드로우가 가시 목록을 바인딩할지 정하는 값이다. 목록을 바인딩했는데 컬링이 돌지 않았다면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 모두 같은 인스턴스를 그린다.
         */
        uint8 _bGPUCullingActive;
        /// @brief 머티리얼 폴백 stride 가 없다는 것을 한 번만 알리기 위한 래치입니다. 드로우 경로이므로 프레임마다 로그를 찍으면 안 됩니다.
        atomic<uint8> _bMaterialFallbackMissingLogged;
        /// @brief 컬러 타깃이 없어 패스를 건너뛴다는 것을 한 번만 알리기 위한 래치입니다. 패스 경로이므로 프레임마다 로그를 찍으면 안 됩니다.
        atomic<uint8> _bMissingColorTargetLogged;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 레벨끼리 병렬로 돈다(RenderGraph::executeParallel).
        // 비트필드로 두면 인접 비트를 쓰는 다른 패스와 같은 바이트를 read-modify-write 해서
        // 서로의 값을 덮어쓴다. 그래서 독립된 atomic 변수로 둔다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행되었는지 여부입니다. ForwardOpaque 의 PSO 를 고를 때 씁니다.
        atomic<uint8> _bHasExecutedDepthPrepass;
    };
} // namespace sw
