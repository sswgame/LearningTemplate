/**
 * @file FramePSOManager.h
 * @brief 렌더러 하나가 쓰는 PSO 를 만들고 찾는 자리입니다 — 엔진 패스 PSO, 출력(Present · Canvas · RCAS) 포맷 변형, 머티리얼 · 보기 모드 · 컬 반전 변형, 입자 · 쿠키 아틀라스 · 확장 프로그램 PSO, 머티리얼 폴백 버퍼.
 * @details `FrameRenderer` 가 소유하고 패스 실행 · 드로우가 조회만 합니다. 만드는 일은 모두 기록 전(셋업)이고, 기록 중에는 찾기만 합니다(패스가 병렬로 기록된다).
 *          저장소는 `RenderPSOCache` 이고, 미리보기 씬 렌더러는 주 렌더러의 저장소를 빌려 씁니다(`shareCacheOf`).
 *          언리얼의 `FGraphicsPipelineStateInitializer` 를 패스 · 머티리얼 · 뷰 모드로 조립하는 `FMeshPassProcessor` 와 PSO 캐시 자리를 한데 모은 것이다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    struct ExtensionProgramDesc;

    class GPUScene;
    class IRHIDevice;
    class Material;
    class RenderPipelineAsset;
    class ShaderBindingLayout;

    /**
     * @class FramePSOManager
     * @brief 렌더러 하나의 PSO 를 만드는 정책(셋업에서만 · 비동기 빌더 · 변형 키)과 그 저장소를 가집니다.
     * @details 디바이스 · 파이프라인 에셋(패스 서술 · 첨부 포맷) · GPUScene(퍼뮤테이션 · 배치)은 렌더러의 것을 읽기만 한다.
     */
    class SW_API FramePSOManager
    {
    public:
        /**
         * @brief 렌더러의 디바이스 변수 · 파이프라인 에셋 · GPUScene 을 읽는 관리자를 만듭니다. 셋은 이 관리자보다 오래 산다(렌더러의 멤버).
         * @details 디바이스는 렌더러의 `_pDevice` **변수**를 참조한다(`FrameCapture` 와 같다) — 렌더러가 디바이스를 바꿀 때 따로 알릴 일이 없다.
         */
        FramePSOManager( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const GPUScene& gpuScene );
        ~FramePSOManager();
        FramePSOManager( const FramePSOManager& )            = delete;
        FramePSOManager& operator=( const FramePSOManager& ) = delete;

        /**
         * @brief @p owner 의 저장소를 빌려 씁니다(미리보기 씬 렌더러). 해제 · 레이아웃 무효화는 소유자만 합니다.
         * @details 미리보기 씬은 주 렌더러와 같은 디바이스 · 파이프라인 XML 로 그리므로 PSO 가 같다. 나눠 두면 창을 열 때마다 엔진 PSO 를 모두 다시 만들어 첫 프레임이 끊긴다.
         */
        void shareCacheOf( FramePSOManager& owner ) { _pCache = owner._pCache; }
        /** @brief 이 관리자가 저장소를 소유하면 true 입니다(빌려 쓰면 false). */
        bool ownsCache() const { return _pCache == &_cache; }
        /** @brief 지금 쓰는 저장소입니다(소유하거나 빌린 것). 캔버스 렌더러가 출력 PSO 를 찾을 때 씁니다. */
        RenderPSOCache&       getCache() { return *_pCache; }
        const RenderPSOCache& getCache() const { return *_pCache; }

        /**
         * @brief 엔진 패스 PSO(패스 종류 표를 enum 순서로), 출력 패스의 포맷별 변형, 머티리얼 폴백 버퍼를 셋업에서 만듭니다. 저장소에 이미 있는 것은 그대로 둡니다.
         * @details 출력 패스 PSO 를 하나라도 못 만들면 멈춘다(`SW_FATAL`) — 화면에 낼 수 없고 다른 PSO 로 대신하지 않는다.
         *          그래픽스 패스는 desc 를 먼저 모아 셰이더를 워커가 나눠 데우고(`prewarmPassShaders`), PSO 는 이 스레드가 차례로 만든다(백엔드 교체 뒤 셋업이 이 길을 다시 탄다).
         */
        void createPassPSOs();
        /** @brief 소유한 저장소의 PSO · 레이아웃과 폴백 버퍼를 놓습니다. 빌린 저장소는 놓지 않습니다(소유자의 몫). 디바이스가 없으면 핸들만 잊습니다. */
        void release();
        /** @brief 셰이더 핫 리로드 — 소유한 저장소에서 그 셰이더의 바인딩 레이아웃을 무효화합니다. */
        void onShaderRecompiled( string_view shaderPath );

        /** @brief 패스 타입에 해당하는 엔진 PSO 입니다. 없으면 0 입니다. */
        RHIPipelineStateHandle getEnginePSO( RenderPassType passType ) const;
        /**
         * @brief 주 출력에 그리는 패스(Present, Canvas, RCAS)의 PSO 를 **대상 포맷별로** 찾습니다.
         * @details **조회만 한다.** 패스 실행(태스크 워커) 중에 불리므로 없다고 만들면 안 된다. 셋업에 없는 포맷이면 대신 그리지 않고 멈춘다(`SW_FATAL`) —
         *          돌려준 PSO 는 0 이 아니다. 언리얼이 PSO 초기화자의 RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같다.
         */
        RHIPipelineStateHandle findOutputPSO( RenderPassType passType, RHIFormat targetFormat ) const;
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* findLayout( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 를 만들 때 쓴 디스크립터를 반환합니다. 모르는 PSO 면 false 입니다. */
        [[nodiscard]] bool findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief 이 레이아웃의 머티리얼 폴백 버퍼입니다(`ensureMaterialFallbackBuffers` 가 셋업에서 만든다). 없으면 nullptr 입니다. */
        const RHIStructuredBufferSlot* findMaterialFallback( const ShaderBindingLayout* pLayout ) const;

        /**
         * @brief 패스 종류 테이블(RenderPassTypeInfo)과 파이프라인 XML 의 패스 서술로 패스 PSO 를 만듭니다.
         * @param pRTVFormatOverride 컬러 RT 포맷을 이 배열로 고정합니다(Present 변형). nullptr 이면 테이블의 고정 포맷을 쓰고, 없으면 출력 선언 순서를 따릅니다.
         */
        RHIPipelineStateHandle createPSOForPassType( RenderPassType passType, const RHIFormat* pRTVFormatOverride = nullptr );
        /** @brief `createPSOForPassType` 의 몸체입니다. 패스 서술을 직접 받습니다 — 같은 종류의 패스가 여럿인 모듈 패스가 패스마다 부릅니다. */
        RHIPipelineStateHandle createPSOForPass( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride );
        /**
         * @brief 모듈 확장 패스의 프로그램(`ExtensionProgramDesc`) PSO 를 셋업에서 찾거나 만듭니다(키 = 셰이더 · 고정 상태 · 타깃 포맷). 기록 중에는 부르지 않습니다.
         * @return 0 이면 만들지 못한 것입니다(오류를 남겼다 — 부르는 쪽은 그 패스를 그리지 않는다).
         */
        RHIPipelineStateHandle findOrCreateExtensionProgramPSO( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest );
        /** @brief 2D 빛 쿠키 아틀라스 칸에 옮겨 그리는 PSO(전체 화면 블릿 셰이더, 아틀라스 포맷)를 찾거나 만듭니다. 기록 전(프레임 프리패스)에만 부릅니다. 못 만들면 0 입니다. */
        RHIPipelineStateHandle findOrCreateCookieAtlasPSO( RHIFormat atlasFormat );

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 **기록 시작 전에** 모두 만들어 둡니다.
         * @param viewModeMask 이번 프레임에 그리는 뷰들의 보기 모드 비트 집합(`FrameRenderer::collectFrameViewModeMask`)
         */
        void ensureMaterialPSOs( uint32 viewModeMask );
        /**
         * @brief 고정한 머티리얼(`MaterialCache::pinAsset` — 프리로드 세트)의 PSO 를 **기록 시작 전에** 미리 만듭니다(계획 PreloadAndLOD P6). 주 렌더러만 부릅니다.
         * @details 아직 씬에 없는 머티리얼(다음 레벨 · 컷에 나올 것)만 이 길로 데운다 — 그 머티리얼이 씬에 들어오는 프레임에 DX12 PSO 생성 히치가 없게.
         */
        void warmPinnedMaterialPSOs( uint32 viewModeMask );
        /**
         * @brief 머티리얼 변형을 빌더 스레드가 만들게 하는지 반환합니다(`-gv_psoAsync` 가 켜져 있고 백엔드가 `_bThreadSafeResourceCreation` 이면 true).
         * @details 결정적 실행(`AssetManager::isFinishingAssetWorkEachFrame`)은 빌더 스레드를 쓰지 않는다. false 이면 렌더 스레드가 같은 줄을 셋업 구간에서 직접 만든다.
         */
        [[nodiscard]] bool usesAsyncMaterialPSOBuilder() const;
        /** @brief 빌더가 끝낸 머티리얼 변형을 저장소에 합치고(스레드를 쓰지 않으면 이 자리에서 만들고), 새로 `Ready` 가 된 것이 있으면 폴백 버퍼를 점검합니다. 셋업 구간에서만 부릅니다. */
        void pumpMaterialPSOBuilds();
        /** @brief 입자 그리기가 있는 프레임에 입자 PSO 를 **기록 시작 전에** 만들어 둡니다(투명 패스 PSO 의 렌더 상태를 물려받고 셰이더만 바꾼다). */
        void ensureParticlePSO();
        /** @brief 입자 PSO 입니다. 투명 패스 PSO 가 없거나 만들지 못했으면 0 입니다. */
        RHIPipelineStateHandle findParticlePSO() const;
        /**
         * @brief 이 배치를 그릴 PSO 입니다. 머티리얼 퍼뮤테이션, 뷰 모드, 컬 반전을 적용한 변형이 있으면 그것을, 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 변형이 `Ready` 가 아니면(만드는 중 · 실패 · 없음) 0 이고, 이유는 @p pOutState 가 답한다.
         */
        RHIPipelineStateHandle psoForBatch( RHIPipelineStateHandle passPSO, const GPUMeshBatch& batch, RenderViewMode viewMode,
                                            RenderPSOCache::MaterialPSOState* pOutState = nullptr ) const;

    private:
        /** @brief 이번 프레임 배치가 쓰는 PSO 변형 하나의 조건(퍼뮤테이션, 컬 반전)입니다. 퍼뮤테이션이 없으면 kInvalidShaderPermutation 입니다. */
        struct MaterialPSORequest
        {
            uint32 _shaderPermutation{ kInvalidShaderPermutation };
            uint8  _bReverseCulling{ SW_FALSE };
        };
        /**
         * @brief 머티리얼이 없는 배치에 바인딩하는 원소 하나짜리 구조 버퍼입니다. **내용이 다른 원소마다 하나**씩 있습니다.
         * @details DX12 에서 아무것도 바인딩하지 않은 t5 를 읽으면 GPU 폴트(디바이스 제거)가 난다. 어떤 드로우도 빈 슬롯으로 나가지 않도록 항상 유효한 버퍼를 바인딩한다
         *          (언리얼의 기본 머티리얼 · RDG 더미 버퍼 `CreateStructuredDesc( sizeof( FElement ), 1 )`). 원소 하나를 모든 셰이더에 공용으로 걸면 셰이더의 `MaterialData`
         *          stride 와 어긋나 DX11 디버그 레이어가 드로우마다 경고를 낸다. 원소는 0 이지만 텍스처 인덱스 필드만 `kInvalidDescriptorIndex` 다(`MaterialUtil::makeFallbackElement`).
         */
        struct MaterialFallback
        {
            vector<uint8>           _bytes;
            RHIStructuredBufferSlot _buffer;
        };

        /** @brief 셋업이 만들 그래픽스 패스 PSO 하나입니다. `_outputFormat` 이 `Unknown` 이면 엔진 패스 PSO, 아니면 그 대상 포맷의 출력 패스 변형입니다. */
        struct PassPSOJob
        {
            RHIPipelineStateDesc _desc{};
            RenderPassType       _passType{ RenderPassType::Invalid };
            RHIFormat            _outputFormat{ RHIFormat::Unknown };
        };

        /** @brief 출력 패스(Present, Canvas, RCAS)가 그릴 수 있는 대상 포맷(백버퍼, 오프스크린, 캡처)의 PSO 를 @p inoutListJob 에 더합니다. desc 를 정하지 못하면 멈춥니다. */
        void collectOutputPSOJobs( vector<PassPSOJob>& inoutListJob );
        /**
         * @brief 패스 종류 표와 파이프라인 XML 의 패스 서술로 패스 PSO 의 desc 를 정합니다(PSO 는 만들지 않는다). 컬러 포맷을 정할 근거가 없으면 오류를 남기고 false 입니다.
         * @param pRTVFormatOverride 컬러 RT 포맷을 이 배열로 고정합니다(출력 변형). nullptr 이면 표의 고정 포맷을 쓰고, 없으면 출력 선언 순서를 따릅니다.
         */
        [[nodiscard]] bool makePassPSODesc( RenderPassType passType, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormatOverride, RHIPipelineStateDesc& outDesc ) const;
        /**
         * @brief @p listJob 의 셰이더를 워커가 나눠 읽거나 컴파일해 `ShaderCache` 에 올립니다(PSO 는 만들지 않는다 — 뒤의 생성이 캐시에서 받는다).
         * @details 셰이더 바이트코드는 디바이스를 쓰지 않아 백엔드와 상관없이 병렬로 된다. PSO 생성은 이 스레드가 차례로 한다(`createPassPSOS`).
         */
        void prewarmPassShaders( const vector<PassPSOJob>& listJob ) const;
        /**
         * @brief 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 마다 폴백 버퍼를 만듭니다(셋업 전용).
         * @details 기록 중에는 만들 수 없다. 버퍼 생성과 `registerBindlessBufferSRV` 는 bindless 레지스트리를 바꾸는데, 패스 콜백은 태스크 워커에서 병렬로 돈다.
         */
        void ensureMaterialFallbackBuffers();
        /** @brief PSO 생성 desc 로 레이아웃을 만들고 PSO 핸들에 연결합니다. */
        void registerLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc );
        /** @brief 패스 타입으로 파이프라인의 패스 서술을 찾습니다. */
        const RenderGraphPassDesc* findPassDescByType( RenderPassType passType ) const;
        /**
         * @brief 패스 PSO 의 컬러 RT 수와 포맷을 정합니다. 고정 배열이 있으면 그것을, 없으면 출력 선언 순서를 씁니다.
         * @return 컬러 RT 가 있는데 포맷을 정할 근거가 없거나 출력 선언에 포맷을 모르는 출력(Swapchain)이 있으면 false 입니다 — RGBA8 로 짐작하지 않는다.
         */
        [[nodiscard]] bool fillPassRenderTargetFormats( RHIPipelineStateDesc& desc, const RenderGraphPassDesc* pPassDesc, const RHIFormat* pRTVFormats, uint32 numRenderTargets ) const;
        /** @brief 이 패스 종류의 컬러 포맷을 정할 근거(표의 고정 포맷 · 컬러 없음 · 파이프라인의 출력 선언)가 있는지 반환합니다. 없으면 이 파이프라인에서 실행되지 않는 패스입니다. */
        bool hasRenderTargetFormatSource( RenderPassType passType ) const;
        /**
         * @brief 패스 PSO 에 머티리얼 퍼뮤테이션, 뷰 모드, 컬 반전을 적용한 변형을 만듭니다. 적용할 것이 없으면 패스 PSO 를 그대로 반환합니다.
         * @details 렌더 상태(블렌드, 뎁스, RT 포맷)는 **패스의 것을 그대로 물려받고** 셰이더만 교체한다. 예외는 컬 모드 하나다(`bReverseCulling` 이면 Back 과 Front 를 맞바꾼다).
         */
        RenderPSOCache::MaterialPSOEntry createMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation,
                                                                   RenderViewMode viewMode, bool bReverseCulling, RHIPipelineStateDesc* pOutDeferredDesc );
        /** @brief 배치 목록에서 (퍼뮤테이션, 컬 반전) 조합을 중복 없이 모읍니다. 퍼뮤테이션이 없는 배치도 넣습니다. */
        static void collectMaterialPSORequests( vector_reference<const GPUMeshBatch> listBatch, vector<MaterialPSORequest>& outListRequest );
        /** @brief 요청 하나에 대해 이번 프레임의 보기 모드마다 변형을 만들어 둡니다. @return 새 변형을 하나라도 만들었으면 true 입니다. */
        [[nodiscard]] bool ensureRequestMaterialPSOs( RHIPipelineStateHandle passPSO, RenderPassType passType, const MaterialPSORequest& request, uint32 viewModeMask );
        /**
         * @brief 변형 하나가 저장소에 없으면 만들어 넣습니다. 셰이더를 컴파일해야 하는 변형은 `Building` 으로 올리고 빌더 줄에 넣습니다(`pumpMaterialPSOBuilds` 가 합칩니다).
         * @param bUrgent 이번 프레임 배치가 필요한 키면 true, 미리 만들기(`warmPinnedMaterialPSOs`)는 false 입니다.
         * @return 이 호출에서 `Ready` · `Failed` 로 끝난 변형이 있으면 true 입니다(`Building` 으로 넘긴 것은 false).
         */
        [[nodiscard]] bool ensureMaterialPSOVariant( RHIPipelineStateHandle passPSO, RenderPassType passType, const GPUShaderPermutation* pPermutation, uint64 permutationHash,
                                                     bool bReverseCulling, RenderViewMode viewMode, bool bUrgent = true );

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고). nullptr 이면 아무것도 만들지 않는다
        const RenderPipelineAsset& _pipeline;
        const GPUScene&            _gpuScene;
        /// @brief 이 관리자가 소유하는 저장소입니다(엔진 패스 PSO, 출력 PSO, 머티리얼 변형과 그 바인딩 레이아웃). 빌려 쓰는 관리자는 이것을 비워 둔다.
        RenderPSOCache _cache;
        /// @brief 찾고 넣는 저장소입니다. 주 렌더러는 자기 `_cache`, 미리보기 씬 렌더러는 주 렌더러의 것입니다(`shareCacheOf`).
        RenderPSOCache* _pCache;
        /// @brief `ensureMaterialPSOs` 가 이번 프레임 배치에서 모으는 변형 조건입니다. 중복이 없고, 프레임마다 다시 채웁니다.
        vector<MaterialPSORequest> _listOpaquePSORequestScratch;
        vector<MaterialPSORequest> _listTransparentPSORequestScratch;
        /// @brief `warmPinnedMaterialPSOs` 가 줄에서 꺼낸 머티리얼입니다. 프레임마다 비운다(저장소만 재사용한다).
        vector<shared_ptr<Material>> _listPSOWarmMaterialScratch;
        vector<MaterialFallback>     _listMaterialFallback;
        /// @brief 바인딩 레이아웃 → `_listMaterialFallback` 순번입니다. 레이아웃은 저장소가 소유하며 PSO 마다 하나입니다.
        unordered_map<const ShaderBindingLayout*, uint32> _mapLayoutMaterialFallback;
        /// @brief 이번 프레임 입자 PSO 의 변형 키입니다(`ensureParticlePSO` 가 기록 전에 정한다). 입자가 없거나 투명 패스가 없으면 0 입니다.
        uint64 _particlePSOKey{ 0 };
    };
} // namespace sw
