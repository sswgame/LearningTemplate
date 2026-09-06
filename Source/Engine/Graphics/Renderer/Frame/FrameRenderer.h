/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML을 로드하고 RenderGraph를 만든 뒤 Shadow/Forward/Deferred/Post를 실행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Frame/FrameResourceRegistry.h"
#include "Engine/Graphics/Renderer/Frame/PassConstantValues.h"
#include "Engine/Graphics/Renderer/Graph/RenderGraph.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
#include "Engine/Graphics/Shader/ShaderBindingLayoutCache.h"

namespace sw
{
    struct RenderFramePacket;
    struct ShaderCompileResult;

    class CameraComponent;
    class IRHICommandList;
    class IRHIDevice;
    class Material;
    class MaterialInstance;
    class Scene;
    class ShaderBindingLayout;
    class TaskArgs;
    class TaskManager;

    /** @brief FrameRenderer 초기화/파이프라인 상태. */
    enum class FrameRendererStatus : uint8
    {
        Uninitialized = 0, ///< initialize 미호출
        Ready,             ///< 파이프라인 로드·그래프 준비 완료
        Failed             ///< initialize/loadPipeline 실패 (getStatusMessage)
    };

    /**
     * @class FrameRenderer
     * @brief 프레임 경로: RenderPipeline XML → RenderGraph → GpuScene / MeshComponents → IRHIDevice
     */
    class SW_API FrameRenderer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 수명 — 디바이스·파이프라인 XML, 핫패스 서비스
        // ------------------------------------------------------------------------------
        /** @brief 빈 렌더러. initialize 전에 사용하지 마세요. */
        FrameRenderer();
        /** @brief GPU 자원을 해제합니다. */
        ~FrameRenderer();

        /** @brief 복사를 금지합니다. */
        FrameRenderer( const FrameRenderer& ) = delete;
        /** @brief 대입을 금지합니다. */
        FrameRenderer& operator=( const FrameRenderer& ) = delete;

        /** @brief 디바이스와 파이프라인 XML로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, string_view pipelineXmlPath = {} );
        /** @brief 디바이스·TaskManager·파이프라인 XML로 초기화합니다. */
        bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
                         string_view pipelineXmlPath = {} );
        /** @brief 핫패스 서비스(TaskManager)를 연결합니다. */
        void bindServices( TaskManager* pTaskManager );
        /** @brief GPU 자원을 해제하고 종료합니다. */
        void shutdown();

        // ------------------------------------------------------------------------------
        // 2) 파이프라인 · 실행 — XML 로드, execute / executePacket
        // ------------------------------------------------------------------------------
        /** @brief RenderPipeline XML에서 그래프를 다시 만듭니다 (동기 로드). 패스 콜백은 한 번 바인딩합니다. */
        bool loadPipeline( string_view pipelineXmlPath );
        /** @brief 컴파일된 그래프를 실행합니다. scene이 있으면 GpuScene을 구축합니다. */
        bool execute( IRHIDevice* pDevice, Material* pMaterial = nullptr, Scene* pScene = nullptr );
        /** @brief 렌더 스레드 경로: 미리 만든 packet.GpuScene을 씁니다 (Scene 미접근). */
        bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );

        // ------------------------------------------------------------------------------
        // 3) 조회
        // ------------------------------------------------------------------------------
        /** @brief Ready 상태면 true. */
        bool isReady() const { return _status == FrameRendererStatus::Ready; }
        /** @brief 초기화/파이프라인 상태를 반환합니다. */
        FrameRendererStatus getStatus() const { return _status; }
        /** @brief Failed일 때 원인 메시지 (그 외 empty). */
        const string& getStatusMessage() const { return _statusMessage; }
        /** @brief 렌더 그래프를 반환합니다. */
        const RenderGraph& getGraph() const { return _graph; }
        /** @brief GpuScene을 반환합니다. */
        const GpuScene& getGpuScene() const { return _gpuScene; }
        GpuScene&       getGpuScene() { return _gpuScene; }
        /** @brief 셰이더 핫리로드 알림 — 영향받는 PSO 바인딩 레이아웃을 다시 만든다. */
        void onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result );

    private:
        /**
         * @brief 패스 하나를 기록하는 동안의 로컬 상태입니다.
         * @details 병렬 기록에서는 패스마다 하나씩 존재합니다. 예전에는 이 값들이 전부
         *          FrameRenderer 멤버였고 onGraphPassExecute 가 _pCmd 를 저장/복원했는데,
         *          그건 "한 번에 한 패스만 돈다" 는 전제라 병렬 기록에서 서로를 덮어썼습니다.
         *          상수 버퍼도 패스마다 따로 있어야 합니다. 하나를 공유하면 기록은 지연이고
         *          버퍼 쓰기는 즉시라, replay 시점엔 마지막 writer 의 값만 남습니다.
         */
        struct FramePassContext
        {
            IRHICommandList* _pCmd{ nullptr };
            /** @brief 엔진 PassCB 값 (이름 기반). ShaderBindingBinder 가 리플렉션 오프셋에 기록. */
            PassConstantValues _passValues{};
            /** @brief 이번 드로우의 월드 행렬 (드로우마다 `g_World` 로 push, 인스턴스 경로에선 폴백용). */
            float4x4           _world{};
            RHIBufferHandle    _passCb{ 0 };
            RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
            Material*          _pBoundMaterial{ nullptr };
            /** @brief 패스 스코프 이름→리소스 레지스트리. 패스 시작마다 새로 시작(reset) — 병렬 기록 시
             *         패스마다 독립이어야 하므로 FrameRenderer 공유 멤버가 아니라 여기 둔다. */
            FrameResourceRegistry _resourceRegistry{};
            /** @brief bindForDraw가 마지막으로 조회한 PSO→레이아웃. 같은 PSO로 연속 드로우할 때
             *         layoutForPso()의 뮤텍스+해시맵 조회를 스킵하는 패스-로컬 1-entry 캐시. */
            RHIPipelineStateHandle     _lastLayoutPso{ 0 };
            const ShaderBindingLayout* _pLastLayout{ nullptr };
        };

        // ------------------------------------------------------------------------------
        // 4) 패스 자원 · 콜백 · 드로우
        // ------------------------------------------------------------------------------
        /** @brief 패스용 상주 GPU 자원을 확보합니다. */
        void ensurePassResources();
        /** @brief 패스용 상주 GPU 자원을 해제합니다. */
        void releasePassResources();
        /**
         * @brief 파이프라인 XML 에 선언된 첨부의 포맷을 돌려줍니다 (없으면 fallback).
         * @details 첨부와 같은 크기·포맷이어야 하는 보조 텍스처(TAA 히스토리 등)를 만들 때 쓴다 —
         *          포맷을 상수로 박아 두면 파이프라인이 HDR 첨부를 쓰는 순간 어긋난다.
         */
        RHIFormat attachmentFormatOrDefault( string_view attachmentName, RHIFormat fallback ) const;
        /**
         * @brief 이 첨부를 이번 프레임에 처음 건드리는 것이면 표시하고 true 를 돌려줍니다.
         * @details 반환값이 곧 "Clear 로 열어도 되는가" 다. 같은 웨이브의 패스들이 동시에 부르므로
         *          조회와 표시가 한 임계구역이어야 한다 — 나눠 놓으면 두 패스가 같은 첨부를 둘 다
         *          Clear 로 열어 앞 패스의 결과를 지운다.
         */
        bool markAttachmentCleared( const hashed_string& key );
        /** @brief 이번 프레임의 클리어 기록을 비웁니다 (프레임 시작). */
        void resetClearedAttachments();
        /**
         * @brief 이번 프레임의 패스 상수 버퍼 슬롯을 하나 집어 ctx 에 붙입니다.
         * @details 커맨드 기록은 지연이고 상수 버퍼 쓰기는 즉시라, 패스마다 별도 버퍼를
         *          써야 재생 시점에 각 패스의 상수가 살아남습니다. 커서는 원자적이라
         *          병렬 기록에서도 안전합니다. 슬롯이 모자라면 마지막 슬롯을 공유합니다.
         */
        void acquirePassCb( FramePassContext& ctx );
        /** @brief 프레임 시작마다 패스 상수 슬롯 커서를 되감고 시드를 0번 슬롯에 맞춥니다. */
        void resetPassCbRing();
        /** @brief 일시 텍스처를 확보합니다. */
        void ensureTransientResources( uint32 overrideWidth = 0, uint32 overrideHeight = 0 );
        /**
         * @brief TAA 히스토리 텍스처를 **셋업 단계에서** 만들어 둡니다.
         * @details 예전엔 TAA 패스 콜백 안에서 처음 만들고 bindless 에 등록했다. 그 콜백은 병렬
         *          기록에서 태스크 스레드가 돌리므로, 기록 중에 bindless 레지스트리가 resize 되는
         *          셈이었다 — 다른 스레드가 같은 레지스트리를 읽고 있는 와중에. 파이프라인이 TAA 를
         *          선언했는지, 대상 첨부의 포맷이 무엇인지는 셋업 시점에 이미 다 알 수 있다.
         */
        void ensureTaaHistory();
        /** @brief 일시 텍스처를 해제합니다. */
        void releaseTransientResources();
        /** @brief 그래프 패스 콜백을 한 번 바인딩합니다. */
        void bindPassCallbacks();
        /** @brief RenderGraph 패스 실행 콜백. */
        void onGraphPassExecute( const RenderGraphPassContext& ctx );

        /**
         * @brief 웨이브를 병렬 기록하기 **직전에** 그 웨이브가 만질 자원의 배리어를 미리 발행합니다.
         * @details 자원 이름을 실제 텍스처로 풀어 `prepareTextureForShaderRead` /
         *          `prepareTextureForRenderTarget` 을 프레임 스트림에 기록한다. 프레임 스트림은 이
         *          웨이브의 패스 리스트보다 먼저 제출되므로 GPU 타임라인에서도 앞선다.
         *
         *          이렇게 하면 패스 콜백은 이미 맞는 상태를 보게 되어 기록 중에 리소스 상태를 바꾸지
         *          않는다 — 배리어를 병렬 기록 스레드가 정하던 구조는 이 프로젝트에서 실제로 여러 번
         *          깨졌다(중복 배리어, 레이아웃 불일치).
         */
        void onGraphWavePrologue( const RenderGraphWaveContext& ctx );
        /** @brief 패스 타입에 맞는 실행을 수행합니다. */
        void executePass( FramePassContext& ctx, RenderPassType passType, string_view passName, const hashed_string& depthAttachment );
        /** @brief 패스 상수 값(PassConstantValues)을 채웁니다. 업로드/바인딩은 ShaderBindingBinder 가 합니다. */
        void updatePassConstants( FramePassContext& ctx );
        /** @brief 카메라에서 뷰/투영을 적용합니다. */
        void applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera );
        /** @brief 키라이트 뷰-투영 행렬을 만듭니다. */
        void buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const;
        /** @brief 카메라 뷰-투영 행렬을 만듭니다. */
        void buildViewProj( float4x4& outMat ) const;
        /** @brief 월드 행렬을 항등으로 둡니다. */
        void setIdentityWorld( FramePassContext& ctx );
        /** @brief 드로우 직전 리플렉션 구동 바인딩을 수행합니다 (PassCB/MaterialCB/텍스처/인스턴스 버퍼). */
        void bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb );
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr. */
        const ShaderBindingLayout* layoutForPso( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 핸들에 매핑합니다. */
        void registerPsoLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief GPUScene 인스턴스 구조버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& ctx );
        /** @brief 씬 메시를 직접 그립니다. */
        void drawSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief GpuScene CPU 스냅샷을 배치당 drawInstanced 로 그립니다 (GPU-driven 꺼짐). */
        void drawGpuSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief GpuScene 배치를 간접 드로우로 그립니다. */
        void drawGpuBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
        /** @brief 풀스크린 삼각형을 그립니다. */
        void drawFullscreen( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex );
        /** @brief 일시 텍스처를 할당합니다. */
        void allocTransient( string_view name, RHIFormat format, bool bDepth, const float4& clearColor );
        /** @brief 컬러(+깊이) 패스를 시작합니다. */
        void beginColorPass( FramePassContext& ctx, string_view colorName, string_view depthName, const float4& clearColor,
                             RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
        /** @brief MRT 컬러 패스를 시작합니다. */
        void beginColorPassMRT( FramePassContext& ctx, const string_view* pColorNames, const float4* pTargetClearColor,
                                const RHIRenderPassLoadOp* pColorLoad, uint32 colorCount, string_view depthName,
                                RHIRenderPassLoadOp depthLoad );
        /** @brief 깊이 전용 패스를 시작합니다. */
        void beginDepthOnlyPass( FramePassContext& ctx, string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
        /**
         * @brief 일시 텍스처를 리소스 레지스트리에 canonicalName 으로 등록합니다 (bindless SRV 자동 매칭).
         * @param canonicalName **미리 intern 된** 이름. 문자열을 받으면 패스마다 다시 intern 하게 된다 —
         *        attachmentNames() 의 캐시를 넘기세요.
         */
        void registerPassTexture( FramePassContext& ctx, const hashed_string& canonicalName, string_view attachmentName );
        /** @brief 바인들리스 텍스처 바인딩을 커밋합니다 (PassConstantValues 갱신 + 에뮬 백엔드 폴백 바인딩). */
        void commitBindlessTextureBindings( FramePassContext& ctx );

        // ------------------------------------------------------------------------------
        // 5) 커맨드 리스트 · 그래프 제출
        // ------------------------------------------------------------------------------
        /**
         * @brief execute() / executePacket() 공통: commandList를 준비하고 device에 연결합니다.
         * @param pCallerName 오류 로그 식별용 호출자 이름
         * @return 성공 시 true; false면 호출자가 즉시 반환해야 합니다.
         */
        bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

        /**
         * @brief execute() / executePacket() 공통: graph를 실행하고 commandList를 제출합니다.
         * @return graph.execute() 결과
         */
        bool submitGraph( IRHIDevice* pDevice );

        // ------------------------------------------------------------------------------
        // 6) 어태치먼트 · PSO
        // ------------------------------------------------------------------------------
        /** @brief 어태치먼트 클리어 색을 찾습니다. */
        bool tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const;
        /** @brief 어태치먼트 클리어 색을 반환하거나, 없으면 기본값을 반환합니다. */
        float4 getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const;
        /** @brief 일시 텍스처 핸들을 찾습니다. */
        RHITextureHandle findTransient( string_view name ) const;
        /** @brief 일시 텍스처 SRV를 찾습니다. */
        RHIDescriptorIndex findTransientSrv( string_view name ) const;
        /** @brief 포맷 이름을 RHIFormat으로 해석합니다. */
        RHIFormat parseAttachmentFormat( string_view formatName ) const;
        /** @brief Present 소스 어태치먼트 이름을 결정합니다. */
        string resolvePresentSource() const;
        /** @brief 패스 타입으로 파이프라인 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;

        /** @brief 엔진 기본 PSO를 만듭니다. */
        RHIPipelineStateHandle createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets = 1,
                                                const RHIFormat* pRtvFormats = nullptr, bool bBlend = false,
                                                bool bDepthWrite = true );
        /** @brief 파이프라인 XML 패스 레시피로 PSO를 만들고, 없으면 타입 기본값을 씁니다. */
        RHIPipelineStateHandle createPsoForPassType( RenderPassType passType, string_view defaultShader,
                                                     bool bDepthTest, uint32 numRenderTargets = 1,
                                                     const RHIFormat* pRtvFormats = nullptr, bool bDefaultBlend = false,
                                                     bool                  bDefaultDepthWrite = true,
                                                     const vector<string>* pExtraDefines      = nullptr );
        /** @brief passType 키로 엔진 내장 PSO를 조회합니다. 없으면 0 반환. */
        RHIPipelineStateHandle getEnginePso( RenderPassType passType ) const;

    private:
        /** @brief TaskArgs: passType, defaultShader, depth, numRT, rtvFormats, blend, depthWrite, defines, cacheKey. */

    private:
        IRHIDevice*                               _pDevice;
        IRHIDevice*                               _pCmdOwnerDevice;
        unique_ptr<IRHICommandList>               _frameCmd;
        IRHICommandList*                          _pCmd;
        Scene*                                    _pScene;
        TaskManager*                              _pTaskManager;
        GpuScene                                  _gpuScene;
        RenderPipelineResource                    _pipelineResource;
        RenderGraph                               _graph;
        string                                    _pipelinePath;
        float4                                    _clearColor;
        unordered_map<string, RHITextureHandle>   _mapTransient;
        unordered_map<string, RHIDescriptorIndex> _mapTransientSrv;
        /// @brief 이번 프레임에 이미 클리어한 첨부들. 병렬 패스가 동시에 갱신하므로 _clearedMutex 로 보호한다.
        vector<hashed_string> _listClearedThisFrame;
        mutable mutex         _clearedMutex;
        /**
         * @brief 프레임 단위 패스 상태(직렬 경로에서 사용 + 병렬 패스의 시드).
         * @details 병렬 기록에서는 패스마다 이걸 복사해 각자의 커맨드 리스트/상수 버퍼를 붙입니다.
         */
        FramePassContext _frameCtx;
        /** @brief 한 프레임이 쓸 수 있는 패스 상수 버퍼 슬롯 수. */
        static constexpr uint32 _s_kPassCbSlotCount = 64;
        /** @brief 패스별 상수 버퍼 슬롯. 병렬 기록에서 패스마다 하나씩 집어간다. */
        struct PassCbSlot
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
        };
        vector<PassCbSlot>  _listPassCbSlot;
        std::atomic<uint32> _passCbCursor{ 0 };
        /// @brief PassCB 슬롯 고갈 경고를 프레임당 한 번만 남기기 위한 래치.
        std::atomic<uint8> _bPassCbExhaustedLogged{ 0 };
        RHIBufferHandle    _gpuCullCb;
        RHIDescriptorIndex _gpuCullCbIndex;
        /** @brief (셰이더 경로+define+백엔드) → ShaderBindingLayout 캐시. 리플렉션 구동 바인딩의 핵심. */
        ShaderBindingLayoutCache                                          _bindingLayoutCache;
        unordered_map<RHIPipelineStateHandle, const ShaderBindingLayout*> _mapPsoLayout;
        unordered_map<RHIPipelineStateHandle, RHIPipelineStateDesc>       _mapPsoDesc;
        mutable mutex                                                     _psoLayoutMutex;
        /** @brief 엔진(PassCB) 상수 버퍼 슬롯 크기. 리플렉션이 실제 쓰는 만큼만 채우므로 여유있게 잡는다. */
        static constexpr uint32 _s_kEnginePassCbSize = 512;
        /// @brief 엔진이 만들어 둔 패스별 PSO. 예전엔 string 키라 조회마다 string 을 만들었다.
        unordered_map<RenderPassType, RHIPipelineStateHandle> _mapEnginePso;
        unordered_map<hashed_string, uint32>                  _mapPassNameToIndex;
        uint32                                                _transientWidth;
        uint32                                                _transientHeight;
        RHITextureHandle                                      _outputRenderTarget;
        RHITextureHandle                                      _taaHistory;    ///< TAA resolve history (ping copy of last TaaColor)
        RHIDescriptorIndex                                    _taaHistorySrv; ///< `_taaHistory` bindless SRV (프레임마다 재등록하지 않음)
        FrameRendererStatus                                   _status;
        string                                                _statusMessage;
        uint8                                                 _bCallbacksBound     : 1;
        uint8                                                 _bPassResourcesReady : 1;
        uint8                                                 _bUseGpuDriven       : 1;
        [[maybe_unused]] uint8                                _reservedFlags       : 5;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 웨이브끼리 병렬로 돈다
        // (RenderGraph::executeParallel). 비트필드로 두면 인접 비트를 쓰는 다른 패스와
        // 같은 바이트를 read-modify-write 해서 서로의 값을 날린다 — 독립 원자 변수로 뺀다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행됐는가 (ForwardOpaque 의 PSO 선택에 쓴다).
        std::atomic<uint8>          _bHasExecutedDepthPrepass{ 0 };
        RenderGraphExecutionContext _graphContext;
    };
} // namespace sw
