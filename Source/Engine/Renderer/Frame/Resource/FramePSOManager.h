/**
 * @file FramePSOManager.h
 * @brief 렌더러 하나가 쓰는 PSO 를 만들고 찾는 자리입니다 — 엔진 패스 PSO, 출력(Present · Canvas · RCAS) 포맷 변형, 머티리얼 · 보기 모드 · 컬 반전 변형, 입자 · 쿠키 아틀라스 · 확장 프로그램 PSO, 머티리얼 폴백 버퍼.
 * @details `FrameRenderer` 가 소유하고 패스 실행 · 드로우가 조회만 합니다. 만드는 일은 모두 기록 전(셋업)이고, 기록 중에는 찾기만 합니다(패스가 병렬로 기록된다).
 *          저장소는 `RenderPSOCache` 이고, 미리보기 씬 렌더러는 주 렌더러의 저장소를 빌려 씁니다(`shareCache`).
 *          언리얼의 `FGraphicsPipelineStateInitializer` 를 패스 · 머티리얼 · 뷰 모드로 조립하는 `FMeshPassProcessor` 와 PSO 캐시 자리를 한데 모은 것이다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHIPipelineStateDesc.h"
#include "Engine/Graphics/View/RenderViewMode.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

namespace sw
{
    struct ExtensionProgramDesc;
    struct RHICapabilities;
    struct ShaderCompileDesc;

    class GPUScene;
    struct RenderFrameSettings;
    class IRHIDevice;
    class Material;
    class MaterialCache;
    struct RHIStructuredBuffer;
    class RenderPipelineAsset;
    class ShaderBindingLayout;

    /**
     * @struct GPUMeshBatchPSO
     * @brief 메시 배치를 그릴 PSO 하나입니다(같은 변형 조건의 배치가 나눠 쓴다).
     */
    struct GPUMeshBatchPSO
    {
        RHIPipelineStateHandle           _pso{ 0 };           ///< 그릴 PSO — 0 이면(만드는 중 · 실패 · 없음) 그 배치는 그리지 않는다. `_state` 가 `Building` 인데 0 이 아니면 직전 변형이다
        const ShaderBindingLayout*       _pLayout{ nullptr }; ///< `_pso` 의 바인딩 레이아웃
        RenderPSOCache::MaterialPSOState _state{ RenderPSOCache::MaterialPSOState::Absent };
    };

    /**
     * @class FramePSOManager
     * @brief 렌더러 하나의 PSO 를 만드는 정책(셋업에서만 · 비동기 빌더 · 변형 키)과 그 저장소를 가집니다.
     * @details 디바이스 · 파이프라인 에셋(패스 서술 · 첨부 포맷) · GPUScene(퍼뮤테이션 · 배치)은 렌더러의 것을 읽기만 한다.
     */
    class SW_API FramePSOManager
    {
    public:
        /** @brief 패스의 렌더 상태를 @p desc 에 정합니다. 파이프라인 패스(@p pPassDesc)는 XML 값 그대로이고, 내부 슬롯(nullptr)은 표의 값입니다. */
        static void applyPassRenderState( RHIPipelineStateDesc& desc, RenderPassKind passType, const RenderGraphPassDesc* pPassDesc );

        /**
         * @brief 렌더러의 디바이스 변수 · 파이프라인 에셋 · GPUScene 을 읽는 관리자를 만듭니다. 셋은 이 관리자보다 오래 산다(렌더러의 멤버).
         * @details 디바이스는 렌더러의 `_pDevice` 변수를 참조한다(`FrameCapture` 와 같다) — 렌더러가 디바이스를 바꿀 때 따로 알릴 일이 없다.
         */
        FramePSOManager( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, const GPUScene& gpuScene, const RenderFrameSettings& frameSettings );
        ~FramePSOManager();
        FramePSOManager( const FramePSOManager& )            = delete;
        FramePSOManager& operator=( const FramePSOManager& ) = delete;

        /**
         * @brief @p owner 의 저장소를 빌려 씁니다(미리보기 씬 렌더러). 해제 · 레이아웃 무효화는 소유자만 합니다.
         * @details 미리보기 씬은 주 렌더러와 같은 디바이스 · 파이프라인 XML 로 그리므로 PSO 가 같다. 나눠 두면 창을 열 때마다 엔진 PSO 를 모두 다시 만들어 첫 프레임이 끊긴다.
         */
        void shareCache( FramePSOManager& owner );
        /** @brief 이 관리자가 저장소를 소유하면 true 입니다(빌려 쓰면 false). */
        bool hasOwnCache() const;
        /** @brief 지금 쓰는 저장소입니다(소유하거나 빌린 것). 캔버스 렌더러가 출력 PSO 를 찾을 때 씁니다. */
        RenderPSOCache&       getCache();
        const RenderPSOCache& getCache() const;

        /**
         * @brief 엔진 슬롯 PSO(패스 종류 표를 enum 순서로), 파이프라인 패스 PSO(패스 이름이 열쇠), 출력 패스의 포맷별 변형, 머티리얼 폴백 버퍼를 셋업에서 만듭니다. 저장소에 이미 있는 것은 그대로 둡니다.
         * @details 출력 패스 PSO 를 하나라도 못 만들면 멈춘다(`reportFatalError`) — 화면에 낼 수 없고 다른 PSO 로 대신하지 않는다.
         *          그래픽스 · 컴퓨트 패스는 desc 를 먼저 모아 셰이더를 워커가 나눠 데우고(`warmShaders`), PSO 는 이 스레드가 차례로 만든다(백엔드 교체 뒤 셋업이 이 길을 다시 탄다).
         *          처음 그리는 프레임에 만드는 PSO(입자 · 쿠키 아틀라스)의 셰이더도 같은 데우기에 넣는다 — 그 프레임에는 드라이버 생성만 남는다.
         */
        void createPassPSOs();
        /** @brief 소유한 저장소의 PSO · 레이아웃과 폴백 버퍼를 놓습니다. 빌린 저장소는 놓지 않습니다(소유자의 몫). 디바이스가 없으면 핸들만 잊습니다. */
        void release();
        /** @brief 셰이더 핫 리로드 — 소유한 저장소에서 그 셰이더를 쓰는 PSO 와 레이아웃만 놓습니다(`RenderPSOCache::releasePSOsUsingShader`). 다시 채우기는 `createPassPSOs` 입니다. */
        void onShaderRecompiled( string_view shaderPath );

        /**
         * @brief 패스 타입에 해당하는 PSO 입니다. 없으면 0 입니다. 프레임 패스(`isFramePassType`)는 이 파이프라인이 선언하지 않았으면 0 입니다.
         * @details 파이프라인이 선언한 그래픽스 뷰 패스 종류면 그 종류의 첫 패스 PSO 다(`getPassPSO`) — 종류로만 묻는 쪽(비 가림 · 입자 원본 · 셋업 로그)의 자리다.
         *          패스를 실행하는 쪽은 `getPassPSO` 로 자기 패스의 것을 받는다(같은 종류가 여럿이면 둘째 패스의 PSO 가 다르다).
         */
        RHIPipelineStateHandle getEnginePSO( RenderPassKind passType ) const;
        /**
         * @brief 파이프라인 그래픽스 패스 @p pass(이 파이프라인 `getGraphPass` 의 원소)의 PSO 입니다. 없으면(만들지 않은 종류 · 실패 · 다른 파이프라인의 서술) 0 입니다.
         * @details 열쇠는 패스 이름이다(`RenderGraphPassDesc::_resolvedName` — 로드 때 intern). 서술 · 머티리얼 필터가 앞 패스와 같으면 그 패스의 PSO 를 함께 쓴다.
         *          컴퓨트 패스는 `findPipelineComputePSO` 다.
         */
        RHIPipelineStateHandle getPassPSO( const RenderGraphPassDesc& pass ) const;
        /**
         * @brief 그림자 패스 밖에서 그림자 PSO 로 그리는 일(비 가림 맵)의 PSO 입니다. 방향광 그림자 패스(깊이 역할 ShadowMap — @p outPass)의 것이고, 그 패스가 없으면 표의 엔진 슬롯이다(@p outPass 는 nullptr).
         * @details 그림자는 여러 번 둘 수 있는 종류라 종류로 찾지 않는다(`isRepeatablePassType`) — 서술(깊이 역할)로 고른다.
         */
        RHIPipelineStateHandle getDirectionalShadowPSO( const RenderGraphPassDesc*& outPass ) const;
        /**
         * @brief 컴퓨트 패스 타입의 엔진 PSO 와 그 PSO 에서 풀어 둔 매개변수 표입니다(`ComputeShaderParameters::apply` 가 받는다). 없으면 빈 값입니다. 프레임 패스 규칙은 `getEnginePSO` 와 같다.
         */
        ComputePipeline getComputePSO( RenderPassKind passType ) const;
        /** @brief 파이프라인 컴퓨트 패스(`RenderPassKind::Compute` · 풀스크린 패스의 컴퓨트 구현) @p passName 의 PSO 와 매개변수 표입니다. 없으면 빈 값입니다. */
        ComputePipeline findPipelineComputePSO( const hashed_string& passName ) const;
        /**
         * @brief 디퍼드 조명 타일 종류 @p tileClass 의 PSO(조명 패스 서술 + 그 종류의 엽만 — deferred_lighting.hlsl VSTile)입니다. 없으면 0 입니다.
         * @details 조회만 한다. 셋업(`createPassPSOs`)이 분류를 쓸 수 있는 백엔드(네이티브 bindless · 간접 인자)와 조명 패스가 있는 파이프라인에서 만든다.
         */
        RHIPipelineStateHandle findDeferredTilePSO( uint32 tileClass ) const;
        /**
         * @brief 주 출력에 그리는 패스(Present, Canvas, RCAS)의 PSO 를 대상 포맷별로 찾습니다.
         * @details 조회만 한다. 패스 실행(태스크 워커) 중에 불리므로 없다고 만들면 안 된다. 셋업에 없는 포맷이면 대신 그리지 않고 멈춘다(`reportFatalError`) —
         *          돌려준 PSO 는 0 이 아니다. 언리얼이 PSO 초기화자의 RenderTargetFormats 를 바인딩된 타깃에서 뽑아 PSO 캐시 키로 삼는 것과 같다.
         */
        RHIPipelineStateHandle findOutputPSO( RenderPassKind passType, RHIFormat targetFormat ) const;
        /** @brief PSO 핸들의 바인딩 레이아웃을 조회합니다. 없으면 nullptr 입니다. */
        const ShaderBindingLayout* findLayout( RHIPipelineStateHandle pso ) const;
        /** @brief PSO 를 만들 때 쓴 디스크립터를 반환합니다. 모르는 PSO 면 false 입니다. */
        [[nodiscard]] bool findDesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const;
        /** @brief PSO 의 깊이 형식만 돌려줍니다(모르는 PSO · 깊이 없는 PSO 는 `Unknown`) — 서술을 복사하지 않는다. */
        RHIFormat findDepthStencilFormat( RHIPipelineStateHandle pso ) const;
        /** @brief 이 레이아웃의 머티리얼 폴백 버퍼입니다(`ensureMaterialFallbackBuffers` 가 셋업에서 만든다). 없으면 nullptr 입니다. */
        const RHIStructuredBuffer* findMaterialFallback( const ShaderBindingLayout* pLayout ) const;

        /** @brief 모듈 확장 패스 프로그램 PSO 하나의 요청입니다(키 = 셰이더 · 고정 상태 · 타깃 포맷 · 깊이 테스트). 프로그램은 부르는 쪽 것을 빌린다. */
        struct ExtensionProgramRequest
        {
            const ExtensionProgramDesc* _pProgram{ nullptr };
            RHIFormat                   _colorFormat{ RHIFormat::Unknown };
            bool                        _bDepthTest{ false };
        };
        /**
         * @brief 아직 PSO 가 없는 확장 프로그램들의 셰이더를 워커가 한 번에 데웁니다(셋업 — PSO 는 뒤의 `getOrCreateExtensionProgramPSO` 가 차례로 만든다).
         * @details `createPassPSOS` 와 같은 "셰이더 병렬 데우기 → PSO 직렬 생성" 이다 — 처음 보는 프로그램이 여럿인 프레임에 렌더 스레드가 하나씩 컴파일하지 않게.
         */
        void warmExtensionPrograms( const vector<ExtensionProgramRequest>& listRequest ) const;
        /**
         * @brief 모듈 확장 패스의 프로그램(`ExtensionProgramDesc`) PSO 를 셋업에서 찾거나 만듭니다(키 = 셰이더 · 고정 상태 · 타깃 포맷). 기록 중에는 부르지 않습니다.
         * @return 0 이면 만들지 못한 것입니다(오류를 남겼다 — 부르는 쪽은 그 패스를 그리지 않는다).
         */
        RHIPipelineStateHandle getOrCreateExtensionProgramPSO( const ExtensionProgramDesc& program, RHIFormat colorFormat, bool bDepthTest );
        /** @brief 2D 빛 쿠키 아틀라스 칸에 옮겨 그리는 PSO(전체 화면 블릿 셰이더, 아틀라스 포맷)를 찾거나 만듭니다. 기록 전(프레임 프리패스)에만 부릅니다. 못 만들면 0 입니다. */
        RHIPipelineStateHandle getOrCreateCookieAtlasPSO( RHIFormat atlasFormat );
        /** @brief UV 공간 층 굽기 PSO(층 굽기 셰이더, 타깃 둘 — @p targetFormat)를 찾거나 만듭니다. 기록 전(프레임 프리패스)에만 부릅니다. 못 만들면 0 입니다. */
        RHIPipelineStateHandle getOrCreateLayerBakePSO( RHIFormat targetFormat );

        /**
         * @brief 이번 프레임의 배치들이 쓸 머티리얼 퍼뮤테이션 PSO 를 기록 시작 전에 모두 만들어 둡니다.
         * @param viewModeMask 이번 프레임에 그리는 뷰들의 보기 모드 비트 집합(`FrameRenderer::collectFrameViewModeMask`)
         */
        void ensureMaterialPSOs( uint32 viewModeMask );
        /**
         * @brief 고정한 머티리얼(`MaterialCache::pinAsset` — 프리로드 세트)의 PSO 를 기록 시작 전에 미리 만듭니다(계획 PreloadAndLOD P6). 주 렌더러만 부릅니다.
         * @details 아직 씬에 없는 머티리얼(다음 레벨 · 컷에 나올 것)만 이 길로 데운다 — 그 머티리얼이 씬에 들어오는 프레임에 DX12 PSO 생성 히치가 없게.
         */
        void warmPinnedMaterialPSOs( MaterialCache& pinnedMaterials, uint32 viewModeMask );
        /**
         * @brief 머티리얼 변형을 빌더 스레드가 만들게 하는지 반환합니다(`-gv_psoAsync` 가 켜져 있고 백엔드가 `_bThreadSafeResourceCreation` 이면 true).
         * @details 결정적 실행(`DeterministicRunPolicy::_bFinishAssetJobsEachFrame`)은 빌더 스레드를 쓰지 않는다. false 이면 렌더 스레드가 같은 줄을 셋업 구간에서 직접 만든다.
         */
        [[nodiscard]] bool usesAsyncMaterialPSOBuilder() const;
        /** @brief 빌더가 끝낸 머티리얼 변형을 저장소에 합치고(스레드를 쓰지 않으면 이 자리에서 만들고), 새로 `Ready` 가 된 것이 있으면 폴백 버퍼를 점검합니다. 셋업 구간에서만 부릅니다. */
        void pollMaterialPSOBuilds();
        /** @brief 입자 그리기가 있는 프레임에 입자 PSO 를 기록 시작 전에 만들어 둡니다(투명 패스 PSO 의 렌더 상태를 물려받고 셰이더만 바꾼다). */
        void ensureParticlePSO();
        /** @brief 입자 PSO 입니다. 투명 패스 PSO 가 없거나 만들지 못했으면 0 입니다. */
        RHIPipelineStateHandle findParticlePSO() const;
        /** @brief 이번 프레임의 (배치 변형 조건, 패스 PSO, 보기 모드) → PSO 표를 짓습니다. `ensureMaterialPSOs` 와 마지막 `pollMaterialPSOBuilds` 뒤, 기록 전에 한 번 부릅니다. */
        void buildBatchPSOTable( uint32 viewModeMask );
        /**
         * @brief 그리기 하나의 표 줄을 찾습니다 — 배치 b 의 PSO 는 `outPSO[ outListVariant[ b ] ]` 다. 이번 프레임 표에 없는 그리기(엔진 패스 PSO 가 아니거나 모으지 않은 보기 모드)면 false 입니다.
         * @details 기록 전에 풀어 둔 값이라 잠금이 없다 — 드로우가 배치마다 저장소를 물으면 병렬 기록의 스레드가 뮤텍스 하나를 배치 × 패스만큼 다툰다(언리얼은 메시 드로우 커맨드가 PSO 번호를 들고 있다).
         */
        [[nodiscard]] bool findBatchPSORow( RHIPipelineStateHandle passPSO, bool bTransparentBatch, RenderViewMode viewMode, const GPUMeshBatchPSO*& outPSO,
                                            span<const uint32>& outListVariant ) const;

    private:
        struct Impl;
        unique_ptr<Impl> _impl; ///< 저장소 · 변형 조건 · 배치 표 · 폴백 버퍼(이 파일만 안다 — 언리얼 `TPimplPtr`)
    };
} // namespace sw
