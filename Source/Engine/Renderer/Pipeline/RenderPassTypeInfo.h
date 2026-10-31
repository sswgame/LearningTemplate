/**
 * @file RenderPassTypeInfo.h
 * @brief 패스 종류(`RenderPassType`) 하나에 대한 사실을 한 항목에 모은 테이블입니다.
 * @details 기본 셰이더, PSO 상태, 패스 define, 그리는 대상(씬 메시, 풀스크린, 컴퓨트), 입력 계약이 여기 있습니다.
 *          엔진 PSO 등록(`FrameRenderer::ensurePassResources`), 셰이더 쿠킹 요청(`ShaderCookDriver`), 패스 실행
 *          (`FramePassExecutor::executePass`), 파이프라인 검증(`RenderPipelineAsset::validate`)이 모두 이 테이블을 enum 으로 읽습니다.
 *          패스 종류를 하나 추가하려면 enum 에 한 줄을 더하고 이 테이블에 case 를 하나 추가합니다(전용 실행 코드가 필요한 패스만 executePass 에 case 를 추가합니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

namespace sw
{
    /**
     * @brief G버퍼 패스가 머티리얼 셰이더에 추가하는 define 입니다. 픽셀 출력 서명을 MRT 로 바꿉니다.
     * @details 머티리얼이 셰이더 경로를 정하므로 디퍼드의 G버퍼 패스도 머티리얼의 `.hlsl` 로 그립니다. 그 셰이더가
     *          `SV_TARGET` 하나만 내면 노멀 타깃이 클리어 값 그대로 남고, 디퍼드 조명은 화면 전체를 같은 노멀로 계산합니다.
     *          언리얼이 같은 머티리얼을 패스별 셰이더 **타입**으로 감싸는 역할을 이 엔진에서는 define 이 맡습니다.
     * @note 이 문자열과 `binding.hlsli` 의 `#if defined( SW_PASS_GBUFFER )` 가 어긋나면 컴파일은 되고 화면만 틀린다.
     */
    inline constexpr const utf8* kPassGBufferDefine = "SW_PASS_GBUFFER=1";

    /**
     * @brief 깊이 프리패스가 그림자와 같은 셰이더 파일(shadow_depth.hlsl)을 **카메라** 행렬로 그리게 하는 define 입니다.
     * @details 이 define 이 없으면 프리패스가 광원 행렬(`g_LightViewProj`)로 그려 장면 깊이에 광원 공간의 깊이가 들어갑니다.
     *          언리얼의 EarlyZ 패스, 유니티 URP 의 Depth Priming 에 해당합니다.
     */
    inline constexpr const utf8* kPassDepthPrepassDefine = "SW_PASS_DEPTH_PREPASS=1";

    /**
     * @brief 메시 외곽선 패스가 머티리얼 셰이더에 추가하는 define 입니다. 정점을 노멀 방향으로 밀고 외곽선 색 하나를 내는 퍼뮤테이션을 고릅니다(toon.hlsl).
     */
    inline constexpr const utf8* kPassMeshOutlineDefine = "SW_PASS_MESH_OUTLINE=1";

    /** @brief 감각 모드 강조 패스가 강조 셰이더(highlight_outline.hlsl)에 추가하는 define 입니다. Sense 인스턴스만 남기는 퍼뮤테이션을 고릅니다. */
    inline constexpr const utf8* kPassHighlightSenseDefine = "SW_PASS_HIGHLIGHT_SENSE=1";

    /**
     * @brief 블룸 다운샘플 패스가 블룸 셰이더(post_bloom.hlsl)에 추가하는 define 입니다. 합성 대신 반해상도의 밝은 부분만 내는 퍼뮤테이션을 고릅니다.
     * @details 언리얼의 블룸 셋업과 다운샘플 체인처럼 밝은 부분은 낮은 해상도에서 구하고, Bloom 패스가 원래 해상도에서 업샘플해 더합니다.
     */
    inline constexpr const utf8* kPassBloomDownsampleDefine = "SW_PASS_BLOOM_DOWNSAMPLE=1";

    /** @brief FSR 1 의 RCAS 단계가 fsr1.hlsl 에 추가하는 define 입니다. 없으면 같은 파일이 EASU 단계입니다. */
    inline constexpr const utf8* kPassFSRRcasDefine = "SW_PASS_FSR_RCAS=1";

    /**
     * @brief 메시 외곽선 패스가 그리는 배치의 머티리얼 define 입니다. 퍼뮤테이션에 이것이 없는 배치는 그 패스에서 빠집니다.
     * @details 머티리얼의 정적 스위치(`Outline`)가 켭니다. 외곽선을 아는 셰이더만 이 스위치를 가지므로 다른 셰이더가 외곽선 define 으로
     *          그려질 일이 없습니다(언리얼이 메시 패스 프로세서에서 머티리얼의 성질로 드로우를 거르는 것에 해당합니다).
     */
    inline constexpr const utf8* kMaterialOutlineDefine = "MATERIAL_OUTLINE";

    /**
     * @brief 양면 머티리얼의 define 입니다. 머티리얼 변형 PSO 가 후면 컬링을 끕니다(언리얼 머티리얼의 Two Sided).
     * @details 앞면 컬링을 쓰는 패스(메시 외곽선)는 그대로 둡니다. 껍질은 양면이어도 뒤집어 그려야 외곽선이 됩니다.
     */
    inline constexpr const utf8* kMaterialTwoSidedDefine = "MATERIAL_TWO_SIDED";

    /** @brief 그림자 패스가 셰이더에 추가하는 define 입니다. 깊이 패스를 머티리얼 셰이더로 그릴 때 그 셰이더가 빛 행렬로 투영하는 퍼뮤테이션을 고릅니다. */
    inline constexpr const utf8* kPassShadowDefine = "SW_PASS_SHADOW=1";

    /** @brief 알파 컷오프 머티리얼의 define 입니다. 깊이 패스(그림자, 깊이 프리패스)도 머티리얼 셰이더로 그려 같은 형태로 오립니다(`FrameRendererUtil::drawsDepthWithMaterialShader`). */
    inline constexpr const utf8* kMaterialAlphaCutoffDefine = "MATERIAL_ALPHA_CUTOFF";

    /**
     * @brief 등장 중인 인스턴스(`GPUInstance::_appearance` < 1)의 퍼뮤테이션 define 입니다. 셰이더가 등장 비율만큼의 픽셀만 남긴다(binding.hlsli 8-1).
     * @details 머티리얼 에셋이 아니라 빌더가 붙인다(`GPUMaterialRegistry::permutationHashFor`). 등장 중인 인스턴스만 이 변형이라 다 보이는 물체는
     *          픽셀 버리기 없이 그린다. 깊이만 쓰는 패스도 이 변형에는 픽셀 스테이지를 붙인다(`FrameRendererUtil::addsAppearanceDitherPixelStage`).
     */
    inline constexpr const utf8* kAppearanceDitherDefine = "SW_APPEARANCE_DITHER";

    /** @brief 정점 셰이더가 정점을 옮기는 머티리얼(바람)의 define 입니다. 그림자 패스는 머티리얼 셰이더로 그리고 깊이 프리패스는 그 배치를 뺀다. */
    inline constexpr const utf8* kMaterialVertexDeformDefine = "MATERIAL_VERTEX_DEFORM";

    /**
     * @brief G버퍼로 나타낼 수 없는 셰이딩의 머티리얼 define 입니다. G버퍼 패스가 그 배치를 빼고 `ForwardOnlyOpaque` 패스가 포워드로 그립니다.
     * @details 툰(toon.hlsl)은 빛마다 계단 · 그림자색 · 림 · 맷캡을 섞어 알베도 · 노멀 두 칸에 담을 수 없다. G버퍼에 칸을 더하는 대신 포워드로 그린다 —
     *          유니티 URP 디퍼드가 `UniversalForwardOnly` 머티리얼을 포워드 패스로 그리는 것과 같다(기억에 의존한 비교).
     * @note 이 define 을 빠뜨린 툰 머티리얼은 아직 G버퍼로 가서 램버트로 칠해진다. toon.hlsl 의 G버퍼 퍼뮤테이션을 `#error` 로 막는 일은 셰이더 다시 베이크와 함께 한다.
     */
    inline constexpr const utf8* kMaterialForwardOnlyDefine = "MATERIAL_FORWARD_ONLY";

    /** @brief 그림자를 드리우지 않는 머티리얼의 define 입니다. 그림자 패스 셰이더가 정점을 클립 공간 밖으로 모아 그리지 않습니다. */
    inline constexpr const utf8* kMaterialShadowCastOffDefine = "MATERIAL_SHADOW_CAST_OFF";

    /**
     * @brief 후처리 체인(post_chain.hlsl)이 톤맵까지 하게 하는 퍼뮤테이션 이름입니다(값을 붙인 `SW_POST_TONEMAP=1` 도 켠 것이다). 파이프라인 XML 의 Present 패스가 적습니다.
     * @details 자동 노출은 톤맵하는 패스 앞에서 측정한다(`passAppliesTonemap`). 이 철자와 post_chain.hlsl 의 `#if defined( SW_POST_TONEMAP )` 이 어긋나면 노출을 재지 않는다.
     */
    inline constexpr const utf8* kPostTonemapDefine = "SW_POST_TONEMAP";

    /** @brief 셰이더 assert 를 켠 컴퓨트 변형의 define 입니다(`gpu_assert.hlsli`). `-gv_gpuAssert` 가 켜진 개발 빌드에서 `kGPUAssertTarget` 패스에만 붙는다. */
    inline constexpr const utf8* kGPUAssertDefine = "SW_GPU_ASSERT_ENABLED";

    /** @brief `RenderPassType` 열거자 수입니다. 마지막 열거자가 바뀌면 여기를 고칩니다(테스트가 리플렉션의 열거자 수와 대조합니다). */
    inline constexpr uint32 kRenderPassTypeCount = static_cast<uint32>( RenderPassType::NanCheck ) + 1;

    /**
     * @struct RenderPassTraitFlag
     * @brief `RenderPassTypeInfo::_flags` 의 비트입니다.
     */
    struct RenderPassTraitFlag
    {
        static constexpr uint32 kDepthTest             = SW_BIT( 0 );  ///< PSO 기본값으로 깊이 테스트를 쓴다(XML 로 끌 수는 있어도 켤 수는 없다).
        static constexpr uint32 kDepthWrite            = SW_BIT( 1 );  ///< PSO 기본값으로 깊이 쓰기를 한다.
        static constexpr uint32 kBlend                 = SW_BIT( 2 );  ///< PSO 기본값으로 블렌드를 쓴다(패스 서술이 있으면 그 값이 우선한다).
        static constexpr uint32 kDepthOnly             = SW_BIT( 3 );  ///< 깊이만 쓴다. 출력 선언이 없을 때 컬러 RT 수가 0이 되는 근거다.
        static constexpr uint32 kDrawsSceneMeshes      = SW_BIT( 4 );  ///< 씬 메시(GPUScene 배치)를 그린다. 머티리얼 변형 PSO 를 만들 대상이다.
        static constexpr uint32 kDrawsTransparentBatch = SW_BIT( 5 );  ///< 씬 메시 중 반투명 배치 목록을 그린다(아니면 불투명 목록을 그린다).
        static constexpr uint32 kUsesMaterialShader    = SW_BIT( 6 );  ///< 머티리얼이 선언한 `.hlsl` 로 교체한다(아니면 패스 셰이더에 define 만 적용한다).
        static constexpr uint32 kAppliesViewMode       = SW_BIT( 7 );  ///< 뷰 모드(Unlit, Wireframe)를 받는다.
        static constexpr uint32 kGenericFullscreen     = SW_BIT( 8 );  ///< 선언한 입력을 역할로 바인딩하고 첫 출력에 풀스크린 삼각형 하나를 그린다.
        static constexpr uint32 kCompute               = SW_BIT( 9 );  ///< 컴퓨트 PSO 다(`CSMain`).
        static constexpr uint32 kRequiresGPUCulling    = SW_BIT( 10 ); ///< 컴퓨트 PSO 를 `_bGPUCulling` 일 때만 만든다(아니면 `_bCompute`).
        static constexpr uint32 kHasInputContract      = SW_BIT( 11 ); ///< `_inputContract` 가 이 패스의 입력을 검사한다.
        static constexpr uint32 kCullFront             = SW_BIT( 12 ); ///< PSO 기본값으로 앞면 컬링을 쓴다(인버티드 헐 외곽선). XML 은 None 으로만 바꿀 수 있다.
        static constexpr uint32 kPremultipliedAlpha    = SW_BIT( 13 ); ///< 블렌드가 켜지면 원본 색이 이미 알파를 곱한 값이다(One/InvSrcAlpha, `_bPremultipliedAlpha`).
        /**
         * @brief 후처리를 끈 뷰(CCTV, 표면 값 보기)와 품질(`_minPostQuality`)로 꺼지는 효과 패스다(SSAO, 블룸, 외곽선, 피사계 심도, 모션 블러)와 TAA 다. TAA 말고는 `kGenericFullscreen` 에만 붙는다.
         * @details 꺼진 패스는 기록하지 않는다 — 어떻게 빠지는지는 `kBypassesToSource`(출력이 원본을 가리킨다) 또는 `kOmittedWhenOff`(출력이 없다) 가 정하고, 둘 중 하나가 꼭 붙는다.
         *          조명과 톤맵은 효과가 아니라 이미지를 만드는 단계라 이 비트가 없다.
         */
        static constexpr uint32 kSkipsWithoutPostProcess = SW_BIT( 14 );
        /**
         * @brief 파이프라인이 선언한 컬러 출력에 그린다 — 선언이 없으면 로드가 실패한다(`RenderPipelineAsset::validate` 3-1).
         * @details 실행은 이름(SceneColor · TAAColor 등)으로 타깃을 짐작하지 않는다. 짐작하던 길은 없는 첨부의 핸들 0(백버퍼)을 열었다.
         *          `kGenericFullscreen` 패스도 같은 규칙이다(표의 `makeInfo` 가 함께 붙인다).
         */
        static constexpr uint32 kDrawsDeclaredColor = SW_BIT( 15 );
        /**
         * @brief 패스 셰이더가 파이프라인 XML 의 패스 퍼뮤테이션(데이터)을 요구한다 — 패스 서술 없이 표만으로 PSO 를 만들지 않는다.
         * @details 엔진 PSO 등록(`FrameRenderer` 의 패스 자원 준비)과 쿠커의 표 단계(`ShaderCookRequest` 2단계)가 이 비트를 보고 건너뛴다.
         *          그 패스가 없는 파이프라인에서는 PSO 가 없고 실행도 없다. 강조 외곽선의 두께(`SW_HIGHLIGHT_WIDTH`)처럼 기본값을 코드에 두지 않는 값이 그 예다.
         */
        static constexpr uint32 kRequiresPassDesc = SW_BIT( 16 );
        /**
         * @brief 꺼진 뷰에서는 패스를 기록하지 않고 출력 이름이 원본(SourceColor 입력)을 가리킨다 — 출력 타깃도 처음 켜질 때까지 만들지 않는다(블룸 · 외곽선 · 피사계 심도 · 모션 블러 · TAA. TAA 출력은 셋업이 기록 형식으로 읽어 늘 만든다).
         * @details 원본을 복사하거나 셰이더가 원본을 고르던 길은 꺼져 있어도 전 화면을 한 번 읽고 쓰고, 출력 타깃이 늘 메모리를 차지했다.
         *          뷰마다 프레임에 한 번 정한다(`FrameViewResources::applyViewPassSelection`).
         *          출력이 원본과 형식 · 크기가 같아야 하고 그 출력을 쓰는 패스가 하나뿐이어야 한다 — 로드가 검사한다(`RenderPipelineAsset::validate` 3-6).
         */
        static constexpr uint32 kBypassesToSource = SW_BIT( 17 );
        /**
         * @brief 꺼진 뷰에서는 패스를 기록하지 않고 출력이 없다 — 원본 색이 없는 효과(SSAO)다. 읽는 쪽은 그 역할을 바인딩하지 않아 셰이더 기본값(AO = 1, 가림 없음)을 쓴다.
         * @details 지우기만 하던 길은 꺼져 있어도 타깃을 한 번 쓰고 늘 메모리를 차지했다. 출력을 쓰는 패스가 하나이고, 읽는 패스가 모두 그 역할을 선택 입력으로만 받는지
         *          로드가 검사한다(`RenderPipelineAsset::validate` 3-7).
         */
        static constexpr uint32 kOmittedWhenOff = SW_BIT( 18 );
        /**
         * @brief 주 출력(Swapchain · 오프스크린 출력 · 캡처 텍스처)에 그리는 출력 패스다(Present · Canvas · FSR RCAS).
         * @details 렌더 타깃 포맷이 대상마다 달라 PSO 는 셋업이 대상 포맷마다 만들고(`FramePSOManager::collectOutputPSOJobs`) 엔진 PSO 는 없다.
         *          파이프라인에서 Swapchain 을 출력으로 적을 수 있는 패스는 이 비트가 있는 패스뿐이다(`RenderPipelineAsset::validate`).
         */
        static constexpr uint32 kOutputPass = SW_BIT( 19 );
        /**
         * @brief 셰이더가 `SW_GPU_ASSERT` 를 가진 컴퓨트 패스다(gpu_cull · mesh_morph · canvas_marker). `-gv_gpuAssert` 가 켜지면 PSO 가 `kGPUAssertDefine` 변형으로 만들어지고 렌더러가 그 패스 앞에서 assert 버퍼를 u4 에 건다.
         * @details define 은 `selectRenderPassShader` 가 붙인다 — 쿠커와 런타임이 같은 함수를 부르므로 어긋나지 않고, Shipping 은 `GPUAssertMonitor::isRequested` 가 거짓이라 변형이 생기지 않는다.
         */
        static constexpr uint32 kGPUAssertTarget = SW_BIT( 20 );
        /**
         * @brief 진단 패스다 — 요청한 실행에서만 PSO 를 만든다(`isDiagnosticPassRequested`). 요청하지 않은 실행(Shipping 은 늘)에서는 PSO 도 디스패치도 없다.
         */
        static constexpr uint32 kDiagnostic = SW_BIT( 21 );
        /**
         * @brief 컴퓨트가 엔진 텍스처를 읽어야 해서 네이티브 bindless 샘플링(DX12 · Vulkan)이 있는 백엔드에서만 PSO 를 만든다. 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 읽지 못한다(binding.hlsli).
         */
        static constexpr uint32 kRequiresNativeBindless = SW_BIT( 22 );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderPassTypeInfo
     * @brief 패스 종류 하나의 사실입니다. `getRenderPassTypeInfo` 가 enum 값으로 찾습니다.
     */
    struct RenderPassTypeInfo
    {
        /** @brief 파이프라인 XML 이 `_shaderPath` 를 적지 않았을 때 쓰는 셰이더입니다(EngineDefaultAssets 의 항목). nullptr 이면 엔진 PSO 가 없습니다. */
        DataPath EngineDefaultAssets::* _pDefaultShader{ nullptr };
        /** @brief 패스가 셰이더에 추가하는 define 입니다. 파이프라인 XML 의 `_listPermutation` 뒤에 붙습니다. nullptr 이면 없습니다. */
        const utf8* _pPassDefine{ nullptr };
        /**
         * @brief 이 패스가 그리는 배치의 머티리얼 퍼뮤테이션에 있어야 하는 define 입니다. nullptr 이면 모든 배치를 그립니다.
         * @details 없는 배치(머티리얼 없는 배치 포함)는 드로우, 머티리얼 PSO 변형, 셰이더 쿠킹에서 함께 빠집니다(`FrameRendererUtil::drawsBatchInPass`).
         */
        const utf8* _pRequiredMaterialDefine{ nullptr };
        /** @brief 이 패스가 빼는 배치의 머티리얼 define 입니다(G버퍼의 `MATERIAL_FORWARD_ONLY`). nullptr 이면 빼지 않습니다. 빠지는 범위는 `_pRequiredMaterialDefine` 과 같습니다. */
        const utf8* _pExcludedMaterialDefine{ nullptr };
        /** @brief 고정 컬러 포맷(`_colorTargetCount` 개)입니다. nullptr 이면 패스가 선언한 출력의 포맷을 씁니다. */
        const RHIFormat* _pColorFormat{ nullptr };
        /** @brief 이 패스 타입이 읽는 입력의 역할입니다. `kHasInputContract` 일 때만 의미가 있습니다. */
        RenderPassInputSignature _inputContract{};
        /** @brief 테이블의 항목 순서를 검사하는 용도입니다. 항목 번호가 이 값입니다. */
        RenderPassType _type{ RenderPassType::Invalid };
        /**
         * @brief 엔진 내부 슬롯이 대신 그리는 파이프라인 패스 타입입니다 — PSO 의 컬러 출력 포맷을 그 패스의 선언에서 읽습니다. Invalid 면 자기 선언을 씁니다.
         * @details 내부 슬롯(`ForwardOpaqueNoDepthWrite`)은 XML 에 나오지 않아 자기 선언이 없다. 선언 없이 만들면 포맷을 알 수 없어 장면 색이 HDR 일 때
         *          PSO(RGBA8)와 렌더 타깃이 어긋났다(D3D12 ExecuteIndirect 검증 · Vulkan 렌더 패스 비호환). 셰이더 · 깊이 · 블렌드는 그대로 자기 표에서 정한다(쿠커와 같은 답).
         */
        RenderPassType _targetSourceType{ RenderPassType::Invalid };
        /** @brief `RenderPassTraitFlag` 의 합입니다. */
        uint32 _flags{ 0 };
        /** @brief 출력 선언이 없을 때의 컬러 RT 수입니다. */
        uint32 _colorTargetCount{ 1 };

        /** @brief 이 플래그가 켜져 있는지 확인합니다. */
        constexpr bool hasFlag( uint32 flag ) const { return ( _flags & flag ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 패스 종류의 사실을 반환합니다. 테이블에 없는 값이면 Invalid 항목을 반환합니다. */
    SW_API const RenderPassTypeInfo& getRenderPassTypeInfo( RenderPassType type );

    /**
     * @struct RenderPassShaderSelection
     * @brief 패스 PSO 가 컴파일할 셰이더 경로와 define 목록입니다.
     */
    struct RenderPassShaderSelection
    {
        string         _shaderPath;
        vector<string> _listDefine;
    };

    /**
     * @brief 패스 PSO 의 셰이더 경로와 define 을 정합니다. 런타임 PSO 생성과 셰이더 쿠커가 함께 부릅니다.
     * @details 경로는 패스 서술의 `_shaderPath` 가 우선이고, 비어 있으면 테이블의 기본 셰이더를 씁니다. define 은 패스 서술의
     *          `_listPermutation` 뒤에 테이블의 패스 define 을 (겹치지 않게) 붙인 것입니다. 둘이 어긋나면 런타임이 찾는 해시를
     *          아무도 쿠킹하지 않아 Shipping 에서 그 패스의 드로우가 사라집니다.
     * @param pPassDesc 파이프라인의 패스 서술입니다. nullptr 이면 테이블의 기본값만 씁니다.
     */
    SW_API RenderPassShaderSelection selectRenderPassShader( RenderPassType type, const RenderGraphPassDesc* pPassDesc, const EngineDefaultAssets& engineDefaultAssets );

    /** @brief `kDiagnostic` 패스가 이 실행에서 요청됐는지 반환합니다(NanCheck 는 `-gv_nanCheck`). 진단 패스가 아니면 false 입니다. */
    SW_API bool isDiagnosticPassRequested( RenderPassType type );

    /** @brief 패스가 HDR 색을 톤맵하는지 반환합니다 — Tonemap 이거나 퍼뮤테이션 `kPostTonemapDefine` 을 켠 Present(후처리 체인)입니다. */
    SW_API bool passAppliesTonemap( const RenderGraphPassDesc& passDesc );
} // namespace sw
