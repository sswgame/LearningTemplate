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
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 PPM(P6) 으로 저장합니다 — 백엔드별 시각 검증용.
         * @details 프레임 경로가 아니다(GPU 를 기다린다). 창 캡처가 백엔드마다 되고 안 되고가 갈려서,
         *          네 백엔드를 같은 기준으로 비교하려면 이쪽을 쓴다.
         */
        bool dumpTransientToPpm( string_view attachmentName, string_view outFilePath );
        /**
         * @brief 트랜지언트 첨부를 CPU 로 읽어 옵니다 (밉 0). 테스트가 백엔드 간 픽셀을 비교하는 데 쓴다 — GPU 를 기다린다.
         * @param outFormat 첨부의 RHIFormat (채널 순서 해석용).
         */
        bool readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat );

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
            /** @brief `g_World` — 인스턴스 버퍼가 없는 드로우(풀스크린·픽스처)의 월드 행렬. 씬 메시는 인스턴스 버퍼에서 읽는다. */
            float4x4           _world{};
            RHIBufferHandle    _passCb{ 0 };
            RHIDescriptorIndex _passCbIndex{ kInvalidDescriptorIndex };
            Material*          _pBoundMaterial{ nullptr };
            /** @brief 패스 스코프 이름→리소스 레지스트리. 패스 시작마다 새로 시작(reset) — 병렬 기록 시
             *         패스마다 독립이어야 하므로 FrameRenderer 공유 멤버가 아니라 여기 둔다. */
            FrameResourceRegistry _resourceRegistry{};
            /** @brief bindForDraw가 마지막으로 조회한 PSO→레이아웃. 같은 PSO로 연속 드로우할 때
             *         layoutForPso()의 뮤텍스+해시맵 조회를 스킵하는 패스-로컬 1-entry 캐시. */
            /// @brief 이 드로우의 루트 상수 값 — 배치마다 바뀐다(인스턴스 시작 오프셋, 머티리얼 원소 수).
            ///        PassCB 에 넣으면 한 패스의 드로우들이 서로를 덮어쓴다(binding.hlsli 1-0 참고).
            /// @brief 마지막으로 엔진 상수버퍼를 올린 시점의 (버퍼, 값 버전, 레지스트리 버전).
            ///        셋이 그대로면 그 드로우는 버퍼를 다시 만들 필요가 없다.
            RHIBufferHandle _lastCbBuffer{ 0 };
            uint32          _lastCbValuesVersion{ 0 };
            uint32          _lastCbRegistryVersion{ 0 };
            /**
             * @brief 이 패스가 어떤 뷰의 컬링 결과를 쓸지.
             * @details 그림자 패스만 Shadow 이고 나머지는 Main 이다. 컬링 결과는 절두체에 종속이라
             *          뷰를 잘못 고르면 그림자 드리우개가 사라지거나 화면 밖 물체를 그린다.
             */
            GpuCullView                _cullView{ GpuCullView::Main };
            uint32                     _drawInstanceBase{ 0 };
            uint32                     _drawMaterialCount{ 0 };
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

        /**
         * @brief 이번 프레임의 주광 값입니다. 패킷이 실어 주면 그 값, 아니면 기본값입니다.
         * @details 예전엔 방향·색·세기·그림자 볼륨이 전부 .cpp 안의 constexpr 이라 씬이 커져도
         *          그림자 볼륨이 2 유닛 그대로였다.
         */
        struct FrameLightState
        {
            float4   _dirIntensity{ -0.35f, -0.85f, -0.25f, 1.35f };
            float4   _colorAmbient{ 1.0f, 0.82f, 0.62f, 0.28f };
            float4x4 _shadowViewProj{};
            uint8    _bHasShadowViewProj{ 0 };
        };
        FrameLightState _frameLight;
        /** @brief 카메라에서 뷰/투영을 적용합니다. */
        void applyViewFromCamera( FramePassContext& ctx, CameraComponent* pCamera );
        /** @brief 키라이트 뷰-투영 행렬을 만듭니다. */
        void buildLightViewProj( const FramePassContext& ctx, float4x4& outMat ) const;
        /** @brief 카메라 뷰-투영 행렬을 만듭니다. */
        void buildViewProj( float4x4& outMat ) const;
        /** @brief 월드 행렬을 항등으로 둡니다. */
        void setIdentityWorld( FramePassContext& ctx );
        /** @brief 드로우 직전 리플렉션 구동 바인딩을 수행합니다 (PassCB/MaterialCB/텍스처/인스턴스 버퍼). */
        void bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                          const RHIDescriptorIndex* pMaterialTexSrv = nullptr );
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr. */
        const ShaderBindingLayout* layoutForPso( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 핸들에 매핑합니다. */
        void registerPsoLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief GPUScene 인스턴스 구조버퍼를 리소스 레지스트리에 "SwInstances" 이름으로 등록합니다. */
        void registerInstanceBuffer( FramePassContext& ctx );
        /** @brief 배치의 머티리얼 데이터 버퍼(GPUScene)를 패스 레지스트리에 "SwMaterials" 로 등록합니다. */
        void registerMaterialBuffer( FramePassContext& ctx, const GpuMeshBatch& batch, RHIPipelineStateHandle pso );
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
        /**
         * @brief 상수버퍼 슬롯을 하나 빌립니다 — **드로우마다** 하나씩. 없으면 false.
         * @details 슬롯을 드로우 단위로 나누는 이유: `updateConstantBuffer` 는 버퍼의 **프레임 슬롯 하나**에 쓰는데
         *          GPU 는 제출 뒤에 읽는다. 그래서 여러 드로우가 같은 버퍼를 쓰면 전부 마지막에 쓴 값을 본다.
         *          배치마다 `g_InstanceBase`·`g_SwMaterialCount` 가 다르므로, 한 패스에 드로우가 둘 이상이면
         *          앞 배치가 뒤 배치의 인스턴스를 읽어 엉뚱한 자리에 그려진다(RenderPassTest.MultiBatchPassKeepsPerBatchConstants).
         *          언리얼도 드로우별 느슨한 파라미터는 드로우마다 유니폼 버퍼를 따로 잡는다.
         */
        bool acquireCbSlot( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex );
        /**
         * @brief 상수버퍼 슬롯 수를 `needed` 이상으로 늘립니다 — **기록 시작 전에만** 부릅니다.
         * @details 버퍼 생성과 bindless 등록은 레지스트리를 바꾸므로 병렬 기록 중에는 할 수 없다
         *          (IRHIDevice::setParallelRecording). 그래서 배치 수를 아는 프레임 시작 지점에서 미리 키운다.
         */
        void ensurePassCbCapacity( uint32 needed );
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다 (셋업 전용).
         * @details 기록 중에는 만들 수 없다 — 버퍼 생성과 `registerBindlessResource` 는 bindless 레지스트리를 바꾸고,
         *          패스 콜백은 태스크 워커에서 병렬로 돈다(`checkRegistryMutableNow` 가 감시하는 규칙).
         *          그래서 PSO 를 다 등록한 뒤 여기서 한 번에 만든다.
         */
        void ensureMaterialFallbackBuffers();
        /**
         * @brief Present 패스 PSO 를 **대상 포맷별로** 얻습니다 (없으면 만든다).
         * @details Present 의 대상은 둘이다 — 백버퍼(포맷은 디바이스가 실제로 채택한 값, Vulkan 은 서피스가
         *          B8G8R8A8 만 줄 수 있다)와 에디터 GameView RT(R8G8B8A8). PSO 의 렌더타깃 포맷이 대상과
         *          다르면 Vulkan 은 렌더패스 비호환으로 검증 레이어가 매 프레임 운다. 언리얼이 PSO 초기화자의
         *          RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같은 방식이다 —
         *          여기서는 Present 하나만 그 키가 포맷이라 맵 하나로 충분하다.
         */
        RHIPipelineStateHandle ensurePresentPso( RHIFormat targetFormat );
        /** @brief Present 가 그릴 수 있는 대상 포맷(백버퍼·오프스크린)의 PSO 를 셋업에서 미리 만듭니다. */
        void buildPresentPsoVariants();

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
        /// @brief 상수버퍼 슬롯 최소 개수. 드로우마다 하나씩 나눠 주므로 배치 수에 따라 아래에서 더 키운다.
        static constexpr uint32 _s_kPassCbSlotCount = 64;
        /// @brief 배치 하나가 한 프레임에 몇 개의 지오메트리 패스에서 그려지는지 어림값 (그림자·프리패스·불투명·반투명).
        static constexpr uint32 _s_kDrawCbPassEstimate = 4;
        /// @brief 슬롯 상한. 넘으면 에러를 남기고 마지막 슬롯을 공유한다(그 프레임은 배치 상수가 섞인다).
        static constexpr uint32 _s_kMaxPassCbSlotCount = 4096;
        /** @brief 패스별 상수 버퍼 슬롯. 병렬 기록에서 패스마다 하나씩 집어간다. */
        struct PassCbSlot
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
        };
        vector<PassCbSlot> _listPassCbSlot;
        /// @brief 지금까지 한 프레임에서 쓴 슬롯 수의 최댓값 — 다음 프레임 용량 산정의 바닥값(단조 증가).
        std::atomic<uint32> _passCbHighWater{ 0 };
        std::atomic<uint32> _passCbCursor{ 0 };
        /// @brief PassCB 슬롯 고갈 경고를 프레임당 한 번만 남기기 위한 래치.
        std::atomic<uint8> _bPassCbExhaustedLogged{ 0 };
        RHIBufferHandle    _gpuCullCb;
        RHIDescriptorIndex _gpuCullCbIndex;
        RHIBufferHandle    _instanceAnimCb;
        RHIDescriptorIndex _instanceAnimCbIndex;
        RHIBufferHandle    _instanceSortCb;
        RHIDescriptorIndex _instanceSortCbIndex;
        /// @brief 이번 프레임 컬링·정렬에 쓸 카메라 위치 (정렬 키가 여기까지의 거리다).
        float3 _cullCameraPos{};
        /**
         * @brief 이번 프레임에 컬링 컴퓨트가 실제로 돌았는가 (가시 목록이 유효한가).
         * @details 드로우가 가시 목록을 걸지 말지 정하는 값이다. 목록을 걸었는데 컬링이 안 돌면 셰이더가
         *          갱신되지 않은(또는 0 으로 찬) 목록을 읽어 전부 같은 인스턴스를 그린다.
         */
        uint8 _bGpuCullingActive;

        /**
         * @brief 이번 프레임 컬링에 쓸 뷰 행렬 — 메인 카메라와 그림자 라이트.
         * @details `updatePassConstants` 가 상수버퍼에 넣는 값과 **같은 값**을 여기에도 둔다. 컬링은
         *          기록 시작 전에 도는데, 그때는 패스 상수 버퍼에서 도로 꺼낼 방법이 없다.
         */
        float4x4 _cullMainViewProj{};
        float4x4 _cullShadowViewProj{};

        /** @brief 컴퓨트가 드로우 커맨드를 만드는 경로를 이번 프레임에 쓸 생각인지 (업로드 전에 GpuScene 에 알린다). */
        bool wantsGpuGeneratedCommands() const;
        /**
         * @brief 인스턴스 애니메이션에 넣는 절대 시간(초).
         * @details 각도를 프레임마다 누적하지 않고 **이 절대 시간에서 매번 새로 만든다**. 누적하면 프레임
         *          간격의 흔들림이 그대로 쌓여 백엔드·실행마다 다른 각도가 나오고, 스크린샷 비교가 불가능해진다.
         *          렌더러가 자기 시계를 갖는다 — 델타를 여기까지 실어 나르지 않아도 되고, 렌더 스레드에서
         *          게임 시간을 만지지 않는다.
         */
        CpuTimer _animTimer;
        /**
         * @brief 머티리얼 데이터 버퍼가 없는 배치(머티리얼 없는 메시)에 거는 0 채운 원소 하나짜리 구조버퍼.
         * @details DX12 루트 SRV 는 경계 검사가 없어 안 걸린 t9 를 읽으면 GPU 폴트(디바이스 제거)다 — 어떤 드로우도 빈 슬롯으로 나가지 않게
         *          항상 유효한 버퍼를 건다 (언리얼의 기본 머티리얼과 같은 자리).
         */
        /**
         * @struct MaterialFallbackBuffer
         * @brief 머티리얼 없는 배치에 거는 0 채운 원소 하나짜리 구조버퍼 — **stride 마다 하나**.
         * @details 언리얼이 RDG 더미 버퍼를 `CreateStructuredDesc( sizeof( FElement ), 1 )` 로 만드는 것과 같다.
         *          예전엔 256 바이트 원소 하나를 모든 셰이더에 공용으로 걸었는데, 셰이더의 `SwMaterialData_t` 는
         *          24 바이트라 DX11 디버그 레이어가 드로우마다 "structure stride 256 vs 24" 를 냈다.
         */
        struct MaterialFallbackBuffer
        {
            RHIBufferHandle    _buffer{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
        };

        /// @brief stride → 폴백 버퍼. 셋업(ensureMaterialFallbackBuffers)에서만 만들고 기록 중에는 조회만 한다.
        unordered_map<uint32, MaterialFallbackBuffer> _mapMaterialFallback;
        /** @brief (셰이더 경로+define+백엔드) → ShaderBindingLayout 캐시. 리플렉션 구동 바인딩의 핵심. */
        ShaderBindingLayoutCache                                          _bindingLayoutCache;
        unordered_map<RHIPipelineStateHandle, const ShaderBindingLayout*> _mapPsoLayout;
        unordered_map<RHIPipelineStateHandle, RHIPipelineStateDesc>       _mapPsoDesc;
        mutable mutex                                                     _psoLayoutMutex;
        /** @brief 엔진(PassCB) 상수 버퍼 슬롯 크기. 리플렉션이 실제 쓰는 만큼만 채우므로 여유있게 잡는다. */
        static constexpr uint32 _s_kEnginePassCbSize = 512;
        /// @brief 엔진이 만들어 둔 패스별 PSO. 예전엔 string 키라 조회마다 string 을 만들었다.
        unordered_map<RenderPassType, RHIPipelineStateHandle> _mapEnginePso;
        /// @brief Present PSO 를 대상 렌더타깃 포맷별로 — 백버퍼와 GameView RT 는 포맷이 다를 수 있다 (ensurePresentPso).
        unordered_map<RHIFormat, RHIPipelineStateHandle> _mapPresentPso;
        /// @brief 셋업에 없는 Present 대상 포맷을 만났다고 한 번만 알리기 위한 래치.
        std::atomic<uint8>                   _bPresentPsoMissingLogged{ 0 };
        unordered_map<hashed_string, uint32> _mapPassNameToIndex;
        uint32                               _transientWidth;
        uint32                               _transientHeight;
        RHITextureHandle                     _outputRenderTarget;
        RHITextureHandle                     _taaHistory;    ///< TAA resolve history (ping copy of last TaaColor)
        RHIDescriptorIndex                   _taaHistorySrv; ///< `_taaHistory` bindless SRV (프레임마다 재등록하지 않음)
        FrameRendererStatus                  _status;
        string                               _statusMessage;
        uint8                                _bCallbacksBound     : 1;
        uint8                                _bPassResourcesReady : 1;
        uint8                                _bUseGpuDriven       : 1;
        [[maybe_unused]] uint8               _reservedFlags       : 5;

        // 아래는 패스 콜백 안에서 갱신되고, 패스 콜백은 같은 웨이브끼리 병렬로 돈다
        // (RenderGraph::executeParallel). 비트필드로 두면 인접 비트를 쓰는 다른 패스와
        // 같은 바이트를 read-modify-write 해서 서로의 값을 날린다 — 독립 원자 변수로 뺀다.
        /// @brief 이번 프레임에 DepthPrepass 가 실행됐는가 (ForwardOpaque 의 PSO 선택에 쓴다).
        std::atomic<uint8>          _bHasExecutedDepthPrepass{ 0 };
        RenderGraphExecutionContext _graphContext;
    };
} // namespace sw
