/**
 * @file RenderPassKindInfo.h
 * @brief 패스 종류(`RenderPassKind`) 하나에 대한 사실을 한 항목에 모은 테이블입니다.
 * @details 기본 셰이더, PSO 상태, 패스 define, 그리는 대상(씬 메시, 풀스크린, 컴퓨트), 입력 계약이 여기 있습니다.
 *          엔진 PSO 등록(`FrameRenderer::ensurePassResources`), 셰이더 쿠킹 요청(`ShaderCookDriver`), 패스 실행
 *          (`FramePassExecutor::executePass`), 파이프라인 검증(`RenderPipelineAsset::validate`)이 모두 이 테이블을 enum 으로 읽습니다.
 *          패스 종류를 하나 추가하려면 enum 에 한 줄을 더하고 이 테이블에 case 를 하나 추가합니다(전용 실행 코드가 필요한 패스만 executePass 에 case 를 추가합니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/Foundation.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/RenderState/RenderState.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

#include "deferred_tile_class.hlsli"

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

    /**
     * @brief 화면 속도 출력(Velocity 역할 컬러 출력)을 선언한 불투명 지오메트리 패스가 셰이더에 추가하는 define 입니다. 포워드는 픽셀 출력 서명에 `SV_TARGET1` 속도를 더합니다(binding_output.hlsli).
     * @details 출력 선언(데이터)이 정하므로 속도를 읽는 패스(TAA · 모션 블러)가 없는 파이프라인은 첨부도 쓰기도 없다(유니티 URP 가 모션 벡터를 요청한 기능이 있을 때만 그리는 것과 같다).
     *          G버퍼는 명세(`GBufferLayout`)가 속도를 늘 넷째 MRT 로 가지므로 출력 서명이 이 define 과 무관하다. 쿠커와 런타임은 같은 함수(`selectRenderPassShader`)로 붙인다.
     */
    inline constexpr const utf8* kPassVelocityDefine = "SW_PASS_VELOCITY=1";

    /** @brief 왜곡 누적 패스가 머티리얼 셰이더에 추가하는 define 입니다. 픽셀 출력을 왜곡 오프셋 하나로 바꾸고 왜곡 훅(`computeMaterialDistortion`)을 부른다(material_shader_main.hlsli). */
    inline constexpr const utf8* kPassDistortionDefine = "SW_PASS_DISTORTION=1";

    /** @brief 컴퓨트 구현(`RenderPassExecutionMode::Compute`)이 패스 셰이더에 추가하는 define 입니다. 같은 파일의 `CSMain` 이 이 define 아래에 있다. */
    inline constexpr const utf8* kPassComputeImplementationDefine = "SW_PASS_COMPUTE=1";

    /** @brief FSR 1 의 RCAS 단계가 fsr1.hlsl 에 추가하는 define 입니다. 없으면 같은 파일이 EASU 단계입니다. */
    inline constexpr const utf8* kPassFSRRcasDefine = "SW_PASS_FSR_RCAS=1";
    /// @brief 컬링 셰이더(gpu_cull.hlsl)를 간접 인자 개수 되돌리기 커널로 만든다(`RenderPassKind::IndirectArgsReset`).
    inline constexpr const utf8* kPassIndirectArgsResetDefine = "SW_CULL_RESET_ARGS=1";
    /// @brief 투명 정렬 셰이더(instance_sort.hlsl)를 다단 정렬 커널로 만든다(`RenderPassKind::InstanceSortMultiPass`). 없으면 같은 파일이 워크그룹 하나짜리 정렬이다.
    inline constexpr const utf8* kPassInstanceSortMultiPassDefine = "SW_SORT_MULTI_PASS=1";
    /// @brief 자동 노출 셰이더(auto_exposure.hlsl)를 히스토그램 컴퓨트 커널로 만든다(`RenderPassKind::AutoExposureHistogram`). 없으면 같은 파일이 1x1 풀스크린이다.
    inline constexpr const utf8* kPassAutoExposureHistogramDefine = "SW_PASS_AUTO_EXPOSURE_HISTOGRAM=1";
    /// @brief 화면 출력 인코딩의 퍼뮤테이션입니다(display_encode.hlsli — 1 HDR10 PQ, 2 scRGB 선형. 0 SDR 은 인코딩 패스가 없다).
    inline constexpr const utf8* kPassDisplayOutputHDR10Define = "SW_DISPLAY_OUTPUT=1";
    inline constexpr const utf8* kPassDisplayOutputScRGBDefine = "SW_DISPLAY_OUTPUT=2";
    /// @brief display_encode.hlsl 을 UI 합성 패스로 만든다(`RenderPassKind::DisplayCompositeUI`). 없으면 같은 파일이 인코딩이다.
    inline constexpr const utf8* kPassDisplayUICompositeDefine = "SW_DISPLAY_UI_COMPOSITE=1";

    /**
     * @brief HDR 화면 출력의 패스 종류(인코딩 둘 · UI 합성)면 true 입니다. 모니터가 HDR 을 받을 때(`RHICapabilities::_displayOutputSupport` 에 SDR 밖 비트가 있을 때)만 PSO 를 만든다
     *        (`FramePSOManager::createPassPSOs`) — HDR 이 없는 기계 · SDR 만 내는 백엔드는 PSO 도 패스도 없다.
     */
    constexpr bool isDisplayOutputPassType( RenderPassKind type )
    {
        return type == RenderPassKind::DisplayEncodeHDR10 || type == RenderPassKind::DisplayEncodeScRGB || type == RenderPassKind::DisplayCompositeUI;
    }

    /** @brief 그림자 패스가 셰이더에 추가하는 define 입니다. 깊이 패스를 머티리얼 셰이더로 그릴 때 그 셰이더가 빛 행렬로 투영하는 퍼뮤테이션을 고릅니다. */
    inline constexpr const utf8* kPassShadowDefine = "SW_PASS_SHADOW=1";

    /**
     * @brief 등장 중인 인스턴스(`GPUInstance::_appearance` < 1)의 퍼뮤테이션 define 입니다. 셰이더가 등장 비율만큼의 픽셀만 남긴다(binding.hlsli 8-1).
     * @details 머티리얼 에셋이 아니라 빌더가 붙인다(`GPUMaterialRegistry::computePermutationHashFor`). 등장 중인 인스턴스만 이 변형이라 다 보이는 물체는
     *          픽셀 버리기 없이 그린다. 깊이만 쓰는 패스도 이 변형에는 픽셀 스테이지를 붙인다(`RenderPassKindInfo::needsAppearanceDitherPixelStage`).
     */
    inline constexpr const utf8* kAppearanceDitherDefine = "SW_APPEARANCE_DITHER";

    // 머티리얼이 내는 기능 define(`kMaterialForwardOnlyDefine` · `kMaterialDistortionDefine` · `kMaterialOutlineDefine` · `kMaterialVertexDeformDefine` · `kMaterialDisplacementDefine` ·
    // `kMaterialDomainPostProcessDefine`)은 MaterialTypes.h 에 있다 — 머티리얼 읽기가 조합을 검사한다(`MaterialPermutation::validateFeatures`).

    /** @brief 그림자를 드리우지 않는 머티리얼의 define 입니다. 그림자 패스 셰이더가 정점을 클립 공간 밖으로 모아 그리지 않습니다. */
    inline constexpr const utf8* kMaterialShadowCastOffDefine = "MATERIAL_SHADOW_CAST_OFF";

    /**
     * @brief 후처리 체인(post_chain.hlsl)이 톤맵까지 하게 하는 퍼뮤테이션 이름입니다(값을 붙인 `SW_POST_TONEMAP=1` 도 켠 것이다). 파이프라인 XML 의 Present 패스가 적습니다.
     * @details 자동 노출은 톤맵하는 패스 앞에서 측정한다(`passAppliesTonemap`). 이 철자와 post_chain.hlsl 의 `#if defined( SW_POST_TONEMAP )` 이 어긋나면 노출을 재지 않는다.
     */
    inline constexpr const utf8* kPostTonemapDefine = "SW_POST_TONEMAP";

    /** @brief 셰이더 메시지를 켠 변형의 define 입니다(`gpu_message.hlsli`). `-gv_gpuMessages` 가 켜진 개발 빌드에서 `kGPUMessageTarget` 패스에만 붙는다. */
    inline constexpr const utf8* kGPUMessagesDefine = "SW_GPU_MESSAGES_ENABLED";

    /** @brief `RenderPassKind` 열거자 수입니다. 마지막 열거자가 바뀌면 여기를 고칩니다(테스트가 리플렉션의 열거자 수와 대조합니다). */
    inline constexpr uint32 kRenderPassKindCount = static_cast<uint32>( RenderPassKind::DisplayCompositeUI ) + 1;

    /** @brief 디퍼드 조명 타일 분류의 종류 수 · 타일 한 변(px)입니다 — 셰이더와 같은 표(deferred_tile_class.hlsli)의 값입니다. */
    inline constexpr uint32 kDeferredTileClassCount = SW_DEFERRED_TILE_CLASS_COUNT;
    inline constexpr uint32 kDeferredTileSize       = SW_DEFERRED_TILE_SIZE;
    /** @brief 디퍼드 조명 타일 변형의 정점 진입점입니다(deferred_lighting.hlsl `VSTile` — 타일 목록의 사각형). 픽셀 진입점은 조명 패스의 것 그대로입니다. */
    inline constexpr const utf8* kDeferredTileVertexEntryPoint = "VSTile";

    /**
     * @struct RenderPassTraitFlag
     * @brief `RenderPassKindInfo::_flags` 의 비트입니다.
     */
    struct RenderPassTraitFlag
    {
        static constexpr uint32 kDepthTest  = BitUtil::bit( 0 ); ///< PSO 기본값으로 깊이 테스트를 쓴다(XML 로 끌 수는 있어도 켤 수는 없다).
        static constexpr uint32 kDepthWrite = BitUtil::bit( 1 ); ///< PSO 기본값으로 깊이 쓰기를 한다.
        // 비트 2 · 13 은 비워 둔다 — 블렌드는 깃발이 아니라 `RenderPassKindInfo::_blend` 칸 하나다(프리멀티플라이인데 블렌드가 꺼진 줄을 적을 수 없다).
        static constexpr uint32 kDepthOnly             = BitUtil::bit( 3 );  ///< 깊이만 쓴다. 출력 선언이 없을 때 컬러 RT 수가 0이 되는 근거다.
        static constexpr uint32 kDrawsSceneMeshes      = BitUtil::bit( 4 );  ///< 씬 메시(GPUScene 배치)를 그린다. 머티리얼 변형 PSO 를 만들 대상이다.
        static constexpr uint32 kDrawsTransparentBatch = BitUtil::bit( 5 );  ///< 씬 메시 중 반투명 배치 목록을 그린다(아니면 불투명 목록을 그린다).
        static constexpr uint32 kUsesMaterialShader    = BitUtil::bit( 6 );  ///< 머티리얼이 선언한 `.hlsl` 로 교체한다(아니면 패스 셰이더에 define 만 적용한다).
        static constexpr uint32 kAppliesViewMode       = BitUtil::bit( 7 );  ///< 뷰 모드(Unlit, Wireframe)를 받는다.
        static constexpr uint32 kGenericFullscreen     = BitUtil::bit( 8 );  ///< 선언한 입력을 역할로 바인딩하고 첫 출력에 풀스크린 삼각형 하나를 그린다.
        static constexpr uint32 kCompute               = BitUtil::bit( 9 );  ///< 컴퓨트 PSO 다(`CSMain`).
        static constexpr uint32 kRequiresGPUCulling    = BitUtil::bit( 10 ); ///< 컴퓨트 PSO 를 `_bGPUCulling` 일 때만 만든다(아니면 `_bCompute`).
        static constexpr uint32 kHasInputContract      = BitUtil::bit( 11 ); ///< `_inputContract` 가 이 패스의 입력을 검사한다.
        static constexpr uint32 kCullFront             = BitUtil::bit( 12 ); ///< PSO 기본값으로 앞면 컬링을 쓴다(인버티드 헐 외곽선). XML 은 None 으로만 바꿀 수 있다.
        /**
         * @brief 후처리를 끈 뷰(CCTV, 표면 값 보기)와 품질(`_minPostQuality`)로 꺼지는 효과 패스다(SSAO, 블룸, 외곽선, 피사계 심도, 모션 블러)와 TAA 다. TAA 말고는 `kGenericFullscreen` 에만 붙는다.
         * @details 꺼진 패스는 기록하지 않는다 — 어떻게 빠지는지는 `kBypassesToSource`(출력이 원본을 가리킨다) 또는 `kOmittedWhenOff`(출력이 없다) 가 정하고, 둘 중 하나가 꼭 붙는다.
         *          조명과 톤맵은 효과가 아니라 이미지를 만드는 단계라 이 비트가 없다.
         */
        static constexpr uint32 kSkipsWithoutPostProcess = BitUtil::bit( 14 );
        /**
         * @brief 파이프라인이 선언한 컬러 출력에 그린다 — 선언이 없으면 로드가 실패한다(`RenderPipelineAsset::validate` 3-1).
         * @details 실행은 이름(SceneColor · TAAColor 등)으로 타깃을 짐작하지 않는다. 짐작하던 길은 없는 첨부의 핸들 0(그때는 백버퍼)을 열었다.
         *          `kGenericFullscreen` 패스도 같은 규칙이다(표의 `makeInfo` 가 함께 붙인다).
         */
        static constexpr uint32 kDrawsDeclaredColor = BitUtil::bit( 15 );
        /**
         * @brief 패스 셰이더가 파이프라인 XML 의 패스 퍼뮤테이션(데이터)을 요구한다 — 패스 서술 없이 표만으로 PSO 를 만들지 않는다.
         * @details 엔진 PSO 등록(`FrameRenderer` 의 패스 자원 준비)과 쿠커의 표 단계(`ShaderCookRequest` 2단계)가 이 비트를 보고 건너뛴다.
         *          그 패스가 없는 파이프라인에서는 PSO 가 없고 실행도 없다. 강조 외곽선의 두께(`SW_HIGHLIGHT_WIDTH`)처럼 기본값을 코드에 두지 않는 값이 그 예다.
         */
        static constexpr uint32 kRequiresPassDesc = BitUtil::bit( 16 );
        /**
         * @brief 꺼진 뷰에서는 패스를 기록하지 않고 출력 이름이 원본(SourceColor 입력)을 가리킨다 — 출력 타깃도 처음 켜질 때까지 만들지 않는다(블룸 · 외곽선 · 피사계 심도 · 모션 블러 · TAA. TAA 출력은 셋업이 기록 형식으로 읽어 늘 만든다).
         * @details 원본을 복사하거나 셰이더가 원본을 고르던 길은 꺼져 있어도 전 화면을 한 번 읽고 쓰고, 출력 타깃이 늘 메모리를 차지했다.
         *          뷰마다 프레임에 한 번 정한다(`FrameViewResources::applyViewPassSelection`).
         *          출력이 원본과 형식 · 크기가 같아야 하고 그 출력을 쓰는 패스가 하나뿐이어야 한다 — 로드가 검사한다(`RenderPipelineAsset::validate` 3-6).
         */
        static constexpr uint32 kBypassesToSource = BitUtil::bit( 17 );
        /**
         * @brief 꺼진 뷰에서는 패스를 기록하지 않고 출력이 없다 — 원본 색이 없는 효과(SSAO)다. 읽는 쪽은 그 역할을 바인딩하지 않아 셰이더 기본값(AO = 1, 가림 없음)을 쓴다.
         * @details 지우기만 하던 길은 꺼져 있어도 타깃을 한 번 쓰고 늘 메모리를 차지했다. 출력을 쓰는 패스가 하나이고, 읽는 패스가 모두 그 역할을 선택 입력으로만 받는지
         *          로드가 검사한다(`RenderPipelineAsset::validate` 3-7).
         */
        static constexpr uint32 kOmittedWhenOff = BitUtil::bit( 18 );
        /**
         * @brief 주 출력(Swapchain · 오프스크린 출력 · 캡처 텍스처)에 그리는 출력 패스다(Present · Canvas · FSR RCAS).
         * @details 렌더 타깃 포맷이 대상마다 달라 PSO 는 셋업이 대상 포맷마다 만들고(`FramePSOManager::collectOutputPSOJobs`) 엔진 PSO 는 없다.
         *          파이프라인에서 Swapchain 을 출력으로 적을 수 있는 패스는 이 비트가 있는 패스뿐이다(`RenderPipelineAsset::validate`).
         */
        static constexpr uint32 kOutputPass = BitUtil::bit( 19 );
        /**
         * @brief 셰이더가 메시지(`SW_GPU_ASSERT` · `SW_GPU_PRINT` · `SW_GPU_LOG`)를 가진 패스다 — 컴퓨트(gpu_cull · mesh_morph · canvas_marker)와 그래픽스(Present 의 fullscreen_blit). `-gv_gpuMessages` 가 켜지면 PSO 가 `kGPUMessagesDefine` 변형으로 만들어지고 렌더러가 그 패스 앞에서 메시지 버퍼를 u4 에 건다(컴퓨트 `bindCompute`, 그래픽스 `bindGraphics`).
         * @details define 은 `selectRenderPassShader` 가 붙인다 — 쿠커와 런타임이 같은 함수를 부르므로 어긋나지 않고, Shipping 은 `GPUMessageChannel::isRequested` 가 거짓이라 변형이 생기지 않는다.
         */
        static constexpr uint32 kGPUMessageTarget = BitUtil::bit( 20 );
        /**
         * @brief 진단 패스다 — 요청한 실행에서만 PSO 를 만든다(`isDiagnosticPassRequested`). 요청하지 않은 실행(Shipping 은 늘)에서는 PSO 도 디스패치도 없다.
         */
        static constexpr uint32 kDiagnostic = BitUtil::bit( 21 );
        /**
         * @brief 컴퓨트가 엔진 텍스처를 읽어야 해서 네이티브 bindless 샘플링(DX12 · Vulkan)이 있는 백엔드에서만 PSO 를 만든다. 에뮬 백엔드(DX11 · GL)의 컴퓨트는 샘플 텍스처를 읽지 못한다(binding.hlsli).
         */
        static constexpr uint32 kRequiresNativeBindless = BitUtil::bit( 22 );
        /** @brief 파이프라인 XML 의 패스가 컴퓨트 셰이더를 고른다(`RenderPassKind::Compute`). 셰이더가 패스마다 달라 PSO 를 패스 이름으로 찾는다(`FramePSOManager::findPipelineComputePSO`). */
        static constexpr uint32 kPipelineCompute = BitUtil::bit( 23 );
        /**
         * @brief 머티리얼이 렌더 타깃과 상관없는 렌더 상태(깊이 · 스텐실 · 채우기)를 덮을 수 있는 색 패스다. 없는 씬 메시 패스(그림자 · 깊이 프리패스 · 외곽선)는 컬 · 앞면 방향만 받는다.
         * @details 그림자 · 프리패스에 머티리얼의 깊이 비교 · 스텐실이 들어가면 깊이의 모양이 색 패스와 어긋난다. 언리얼도 머티리얼의 Two Sided 만 깊이 패스에 넘긴다(기억에 의존한 비교).
         */
        static constexpr uint32 kMaterialFixedState = BitUtil::bit( 24 );
        /** @brief 머티리얼이 블렌드까지 덮을 수 있는 색 패스다(한 첨부에 그리는 포워드 · 반투명). G버퍼는 첨부 셋의 의미가 정해져 있어 받지 않는다. */
        static constexpr uint32 kMaterialBlend = BitUtil::bit( 25 );
        /**
         * @brief 깊이를 쓰는 불투명 지오메트리 패스라 화면 속도(Velocity 역할 출력)를 쓸 수 있다(G버퍼 · 포워드 불투명 · 포워드 전용 불투명). 이 비트가 없는 패스가 속도 출력을 선언하면 읽기 오류다.
         * @details 속도는 깊이와 같은 픽셀의 것이어야 TAA · 모션 블러가 깊이와 짝지어 읽는다 — 깊이를 쓰지 않는 반투명 · 외곽선은 쓰지 않는다(언리얼 기본값도 반투명은 속도를 쓰지 않는다).
         *          G버퍼는 명세가 속도를 넷째 MRT 로 늘 가지고, 포워드는 출력 선언이 있을 때만 `kPassVelocityDefine` 으로 둘째 타깃에 쓴다.
         */
        static constexpr uint32 kWritesVelocity = BitUtil::bit( 26 );
        /**
         * @brief 컬러 출력 없이 픽셀 스테이지가 깊이(SV_Depth)를 쓰는 내부 패스다(그림자 캐시 복원). 컬러가 없는 패스는 픽셀 스테이지를 떼는데(`FramePSOManager::makePassPSODesc`)
         *        이 패스만 남긴다. 쿠커는 내부 슬롯의 VS · PS 를 늘 굽는다.
         */
        static constexpr uint32 kWritesPixelDepth = BitUtil::bit( 27 );
        /**
         * @brief 일반 풀스크린 패스지만 컴퓨트 구현(같은 셰이더 파일의 `CSMain`, define `SW_PASS_COMPUTE`)도 있어 XML 의 `_implementation="Compute"` 로 고를 수 있다(블룸 다운샘플 · 피사계 심도).
         * @details 컴퓨트 구현은 입력을 bindless 번호로 직접 읽어 네이티브 bindless(DX12 · Vulkan)에서만 돈다 — 에뮬 백엔드(DX11 · GL · WebGPU)는 픽셀 구현이 돈다(능력이 정한 길).
         *          어느 쪽이 나은지는 측정으로 정한다(언리얼 블룸 · DOF 의 r.*.Compute 스위치 자리). 비트 27 — W10 이 더한 kWritesVelocity 와 겹치지 않게 26 을 비워 둔다.
         */
        static constexpr uint32 kComputeImplementation = BitUtil::bit( 28 );
        /**
         * @brief 화면 회전 출력 패스다(`RenderPassKind::ScreenRotation`) — `kOutputPass` 와 함께 붙고, 대상은 스왑체인 이미지 하나라 PSO 를 **백버퍼 포맷으로만** 만들며
         *        서피스를 돌릴 수 있는 장치(`RHICapabilities::_bSurfacePreRotation`)에서만 만든다(`FramePSOManager::collectOutputPSOJobs`).
         */
        static constexpr uint32 kSurfaceRotation = BitUtil::bit( 29 );
        /**
         * @brief 읽는 쪽을 위해서만 도는 생산 패스다(장면 색 사본) — 출력을 혼자 쓰고, 출력을 읽는 패스가 이번 뷰에 모두 쉬면 뷰 패스 고르기가 출력을 빼고 실행은 기록하지 않는다.
         * @details 일반 풀스크린 패스(장면 깊이 사본)는 이 표시 없이도 같은 규칙을 탄다. 읽는 쪽이 누구인지는 파이프라인 선언과 그 패스의 이번 뷰 판정이 정한다 — 생산 패스가 읽는 쪽을 알지 않는다(언리얼 RDG 의 소비자 없는 패스 컬링).
         */
        static constexpr uint32 kCullWhenUnconsumed = BitUtil::bit( 30 );
        /**
         * @brief 풀스크린 삼각형 대신 그리기마다 상자 볼륨(정점 36 개, 정점 버퍼 없음)을 그린다(DBuffer 데칼). 파이프라인 XML 의 `_cullMode` 는 Front 여야 한다 —
         *        뒷면만 그려야 픽셀마다 한 번 섞이고 카메라가 상자 안에 있어도 덮는다(언리얼 데칼의 카메라 안쪽 판정을 뒷면 하나로 줄인 모양).
         */
        static constexpr uint32 kDrawsBoxVolume = BitUtil::bit( 31 );
    };
} // namespace sw

namespace sw
{
    /**
     * @enum RenderPassBlend
     * @brief 패스 종류의 PSO 기본 블렌드입니다 — 끔 · 곧은 알파 · 미리 곱한 알파 중 하나다. 파이프라인 패스는 XML 의 `_blendPreset` 이 우선한다.
     * @details 예전에는 깃발 둘(`kBlend` · `kPremultipliedAlpha`)이라 "미리 곱한 알파인데 블렌드 꺼짐" 을 적을 수 있었고, 그렇게 적은 HDR UI 합성이 장면을 덮어썼다.
     */
    enum class RenderPassBlend : uint8
    {
        None,          ///< 블렌드 없음(덮어쓴다)
        Straight,      ///< 곧은 알파(src × a + dst × (1 - a))
        Premultiplied, ///< 미리 곱한 알파(src + dst × (1 - a))
    };

    /**
     * @struct RenderPassKindInfo
     * @brief 패스 종류 하나의 사실입니다. `getRenderPassKindInfo` 가 enum 값으로 찾습니다.
     */
    struct RenderPassKindInfo
    {
        /** @brief 파이프라인 XML 이 `_shaderPath` 를 적지 않았을 때 쓰는 셰이더입니다(EngineDefaultAssets 의 항목). nullptr 이면 엔진 PSO 가 없습니다. */
        DataPath EngineDefaultAssets::* _pDefaultShader{ nullptr };
        /** @brief 패스가 셰이더에 추가하는 define 입니다. 파이프라인 XML 의 `_listPermutation` 뒤에 붙습니다. nullptr 이면 없습니다. */
        const utf8* _pPassDefine{ nullptr };
        /**
         * @brief 이 패스가 그리는 배치의 머티리얼 퍼뮤테이션에 있어야 하는 define 입니다. nullptr 이면 모든 배치를 그립니다.
         * @details 없는 배치(머티리얼 없는 배치 포함)는 드로우, 머티리얼 PSO 변형, 셰이더 쿠킹에서 함께 빠집니다(`shouldDrawMaterial`).
         */
        const utf8* _pRequiredMaterialDefine{ nullptr };
        /** @brief 이 패스가 빼는 배치의 머티리얼 define 입니다(G버퍼의 `MATERIAL_FORWARD_ONLY`). nullptr 이면 빼지 않습니다. 빠지는 범위는 `_pRequiredMaterialDefine` 과 같습니다. */
        const utf8* _pExcludedMaterialDefine{ nullptr };
        /** @brief 고정 컬러 포맷(`_colorTargetCount` 개)입니다. nullptr 이면 패스가 선언한 출력의 포맷을 씁니다. */
        const RHIFormat* _pColorFormat{ nullptr };
        /** @brief PSO 기본 블렌드입니다(패스 서술이 있으면 그 값이 우선한다). */
        RenderPassBlend _blend{ RenderPassBlend::None };
        /** @brief 이 패스 타입이 읽는 입력의 역할입니다. `kHasInputContract` 일 때만 의미가 있습니다. */
        RenderPassInputSignature _inputContract{};
        /**
         * @brief 그래프가 아니라 이 패스의 전용 실행이 스스로 거는 엔진 칸의 역할입니다(Invalid 면 없음). 셰이더가 읽어도 계약 밖이 아니다(`FramePSOManager` 의 계약 검사).
         * @details TAA 는 지난 출력(기록)을 `GBufferAlbedo` 칸에 건다(`FramePassExecutor::executeTAAPass` — 기록 전용 칸을 두면 에뮬 백엔드의 엔진 칸이 하나 는다). 숨은 재사용을
         *          데이터로 적어 검사가 그것만 받게 한다.
         */
        RenderPassInputRole _executionBoundRole{ RenderPassInputRole::Invalid };
        /** @brief 테이블의 항목 순서를 검사하는 용도입니다. 항목 번호가 이 값입니다. */
        RenderPassKind _type{ RenderPassKind::Invalid };
        /**
         * @brief 엔진 내부 슬롯이 대신 그리는 파이프라인 패스 타입입니다 — PSO 의 컬러 출력 포맷을 그 패스의 선언에서 읽습니다. Invalid 면 자기 선언을 씁니다.
         * @details 내부 슬롯(`ForwardOpaqueNoDepthWrite`)은 XML 에 나오지 않아 자기 선언이 없다. 선언 없이 만들면 포맷을 알 수 없어 장면 색이 HDR 일 때
         *          PSO(RGBA8)와 렌더 타깃이 어긋났다(D3D12 ExecuteIndirect 검증 · Vulkan 렌더 패스 비호환). 셰이더 · 깊이 · 블렌드는 그대로 자기 표에서 정한다(쿠커와 같은 답).
         */
        RenderPassKind _targetSourceType{ RenderPassKind::Invalid };
        /** @brief `RenderPassTraitFlag` 의 합입니다. */
        uint32 _flags{ 0 };
        /** @brief 출력 선언이 없을 때의 컬러 RT 수입니다. */
        uint32 _colorTargetCount{ 1 };

        /** @brief 이 플래그가 켜져 있는지 확인합니다. */
        constexpr bool hasFlag( uint32 flag ) const { return ( _flags & flag ) != 0; }

        // 아래 판정은 드로우 루프 · 머티리얼 PSO 변형(`FramePSOManager`) · 셰이더 쿠커(`ShaderCookRequestCollector`)가 같은 답을 보려고 이 표 하나에 둔다.
        // @p pListMaterialDefine 은 배치 머티리얼의 define 목록이고, nullptr 이면 머티리얼이 없는 배치다.

        /** @brief 뎁스만 쓰는 패스 종류인지 반환합니다. 출력 선언이 없을 때 컬러 RT 수를 정하는 기본값(0)의 근거입니다. */
        constexpr bool isDepthOnly() const { return hasFlag( RenderPassTraitFlag::kDepthOnly ); }

        /**
         * @brief 이 패스가 씬 메시(GPUScene 배치)를 그리는지 반환합니다.
         * @details 머티리얼 퍼뮤테이션 PSO 를 미리 만들어 둘 대상이 이 패스들이다. 풀스크린 패스는 배치를 쓰지 않는다.
         */
        constexpr bool shouldDrawSceneMeshes() const { return hasFlag( RenderPassTraitFlag::kDrawsSceneMeshes ); }

        /**
         * @brief 이 패스가 **머티리얼의 셰이더**로 그리는지(아니면 패스 자신의 셰이더인지) 반환합니다.
         * @details 언리얼에서는 패스가 셰이더 **타입**(TShadowDepthVS 같은)을 정하고 머티리얼이 그 타입의
         *          퍼뮤테이션을 준다. 그림자와 뎁스 패스는 지오메트리만 그리므로 자기 셰이더가 기준이고, 머티리얼은
         *          define 만 적용한다. 여기서 true 인 패스만 머티리얼이 선언한 .hlsl 로 교체한다.
         */
        constexpr bool usesMaterialShader() const { return hasFlag( RenderPassTraitFlag::kUsesMaterialShader ); }

        /**
         * @brief 이 패스에 뷰 모드(Unlit, Wireframe, Normals, Depth, Overdraw)를 적용하는지 반환합니다.
         * @details 화면 색을 만드는 지오메트리 패스만 해당한다. 그림자와 뎁스 프리패스는 **제외한다**.
         *          와이어프레임으로 그림자를 구우면 그림자가 선 몇 개로 남고, 뎁스 프리패스를
         *          와이어프레임으로 채우면 이후 패스의 뎁스 테스트가 삼각형 내부를 모두 버려 화면이 빈다.
         *          둘 다 "보기 방식" 이 아니라 다음 패스의 입력이므로 항상 Solid, Lit 로 둔다.
         * @note 메시 외곽선 패스는 머티리얼 셰이더를 쓰지만 뷰 모드를 받지 않습니다. 외곽선은 보기 방식이 아니라 화면의 일부이기 때문입니다.
         */
        constexpr bool shouldApplyViewMode() const { return hasFlag( RenderPassTraitFlag::kAppliesViewMode ); }

        /**
         * @brief 패스 서술 없이 이 표만으로 만든 엔진 PSO 에 픽셀 스테이지가 있는지 반환합니다.
         * @details 파이프라인에 그 종류의 패스가 없으면 `FramePSOManager::makePassPSODesc` 은 표의 컬러 타깃 수로 PSO 를 만들고, 0이면 PS 를
         *          붙이지 않는다. 쿠커가 같은 판정으로 쿠킹한다. 서술이 있는 패스는 `FrameRendererUtil::hasPixelStage( pass, listAttachment )` 를 쓴다.
         */
        constexpr bool hasPixelStage() const { return _colorTargetCount > 0; }

        /** @brief 이 패스 종류가 머티리얼에게 내주는 렌더 상태의 범위입니다(표: `docs/agents/Rendering.md`). 그리는 쪽과 테스트가 같은 답을 본다. */
        RenderStateOverride::Scope getMaterialStateScope() const
        {
            if ( hasFlag( RenderPassTraitFlag::kMaterialBlend ) )
                return RenderStateOverride::Scope::FixedAndBlend;
            return hasFlag( RenderPassTraitFlag::kMaterialFixedState ) ? RenderStateOverride::Scope::FixedState : RenderStateOverride::Scope::CullOnly;
        }

        /** @brief 이 패스가 머티리얼 define 으로 배치를 거르는지(있어야 하거나 없어야 하는 define 이 있는지) 반환합니다. 거르지 않으면 판정을 건너뛴다. */
        bool shouldFilterBatchesByMaterial() const { return _pRequiredMaterialDefine != nullptr || _pExcludedMaterialDefine != nullptr; }

        /**
         * @brief 이 패스가 이 머티리얼 define 목록의 배치를 그리는지 반환합니다. `_pRequiredMaterialDefine` 이 있으면 그것이 있어야 하고,
         *        `_pExcludedMaterialDefine` 이 있으면 그것이 없어야 합니다. 둘 다 없으면 항상 true 입니다.
         * @details 드로우 루프, 머티리얼 PSO 변형, 셰이더 쿠커가 같은 판정을 쓴다. 셋이 갈리면 쿠킹하지 않은 변형을 런타임이 찾거나,
         *          외곽선을 모르는 셰이더가 외곽선 패스의 앞면 컬링으로 그려져 뒷면이 화면을 덮는다.
         */
        bool shouldDrawMaterial( const vector<string>* pListMaterialDefine ) const
        {
            if ( _pExcludedMaterialDefine != nullptr && hasMaterialDefine( pListMaterialDefine, _pExcludedMaterialDefine ) )
                return false;
            if ( _pRequiredMaterialDefine == nullptr )
                return true;
            return hasMaterialDefine( pListMaterialDefine, _pRequiredMaterialDefine );
        }

        /**
         * @brief 깊이만 쓰는 패스(그림자 · 깊이 프리패스)가 이 머티리얼의 배치를 패스 셰이더 대신 **머티리얼의 셰이더**로 그리는지 반환합니다.
         * @details 깊이 패스는 지오메트리만 그리므로 보통 자기 셰이더(shadow_depth.hlsl)가 기준이다. 덮는 모양이나 정점 자리를 머티리얼 코드가 정하면
         *          그 셰이더는 같은 모양을 못 만든다 — 알파 컷오프(머리카락 카드가 사각형 그림자를 드리운다)와 정점 변형(흔드는 식생의 그림자가 서 있다).
         *          언리얼이 Masked · World Position Offset 머티리얼만 자기 깊이 · 그림자 셰이더를 컴파일하고 나머지는 기본 머티리얼의 것을 나눠 쓰는 것과 같다.
         *          정점 변형은 그림자 패스만 갈아탄다 — 깊이 프리패스는 그 머티리얼을 빼므로(shadow_depth.hlsl 의 `MATERIAL_VERTEX_DEFORM`) 어긋날 깊이가 없다.
         *          그림자를 끈 머티리얼은 패스 셰이더가 정점을 모아 그리지 않으므로 갈아타지 않는다.
         *          갈아타는 머티리얼 셰이더는 `SW_PASS_SHADOW`(알파 컷오프면 `SW_PASS_DEPTH_PREPASS` 도) 퍼뮤테이션을 가져야 한다.
         *          런타임 PSO 변형(`FramePSOManager::createMaterialPSOVariant`)과 셰이더 쿠커가 같은 함수를 부른다.
         */
        bool usesMaterialShaderForDepth( const vector<string>* pListMaterialDefine ) const
        {
            if ( isDepthOnly() == false )
                return false;
            const bool bShadowPass = _type == RenderPassKind::Shadow;
            if ( bShadowPass && hasMaterialDefine( pListMaterialDefine, kMaterialShadowCastOffDefine ) )
                return false;
            // 변위는 두 깊이 패스 모두 갈아탄다 — 윤곽이 바뀌므로 깊이 프리패스도 같은 자리를 써야 본 패스의 깊이 비교를 통과한다.
            if ( hasMaterialDefine( pListMaterialDefine, kMaterialAlphaCutoffDefine ) || hasMaterialDefine( pListMaterialDefine, kMaterialDisplacementDefine ) )
                return true;
            return bShadowPass && hasMaterialDefine( pListMaterialDefine, kMaterialVertexDeformDefine );
        }

        /**
         * @brief 이 패스가 이 머티리얼의 배치를 헐 · 도메인 스테이지(머티리얼 셰이더의 `HSMain` · `DSMain`)를 붙여 그리는지 반환합니다.
         * @details 머티리얼 셰이더로 그리는 패스(본 · G버퍼 · 외곽선)와 머티리얼 셰이더로 갈아탄 깊이 패스가 변위 머티리얼(`MATERIAL_DISPLACEMENT`)을 그릴 때다.
         *          패스 셰이더로 그리는 배치는 머티리얼의 헐 · 도메인을 모른다. 런타임 PSO 변형(`FramePSOManager::createMaterialPSOVariant`)과 셰이더 쿠커가 같은 함수를 부른다.
         */
        bool isMaterialTessellated( const vector<string>* pListMaterialDefine ) const
        {
            if ( hasMaterialDefine( pListMaterialDefine, kMaterialDisplacementDefine ) == false )
                return false;
            return usesMaterialShader() || usesMaterialShaderForDepth( pListMaterialDefine );
        }

        /**
         * @brief 깊이 패스가 머티리얼 셰이더로 그릴 때 픽셀 스테이지가 있는지 반환합니다 — 알파 컷오프만 있다(버리기가 픽셀에서 일어난다).
         * @details 정점 변형만이면 정점 스테이지뿐이다. 컬러 출력이 없으므로 픽셀 셰이더는 아무것도 쓰지 않고 `clip` 만 한다.
         */
        bool hasMaterialDepthPixelStage( const vector<string>* pListMaterialDefine ) const
        {
            return usesMaterialShaderForDepth( pListMaterialDefine ) && hasMaterialDefine( pListMaterialDefine, kMaterialAlphaCutoffDefine );
        }

        /**
         * @brief 깊이만 쓰는 패스가 패스 셰이더(shadow_depth.hlsl)로 등장 디더 변형을 그릴 때 픽셀 스테이지를 붙이는지 반환합니다.
         * @details 패스 셰이더의 깊이 패스는 컬러 출력이 없어 픽셀 스테이지가 없다(`hasPixelStage`). 등장 디더는 픽셀을 버리므로 그 변형에만 붙인다 —
         *          붙이지 않으면 프리패스가 물체의 깊이를 다 써 기본 패스가 버린 픽셀 자리에 뒤의 물체가 그려지지 않는다(구멍).
         *          머티리얼 셰이더로 그리는 깊이 배치(알파 컷오프)는 그 셰이더의 픽셀 스테이지가 이미 있고, 정점 변형만인 배치는 디더 없이 깊이를 쓴다.
         *          런타임 PSO 변형(`FramePSOManager::createMaterialPSOVariant`)과 셰이더 쿠커가 같은 함수를 부른다.
         */
        bool needsAppearanceDitherPixelStage( const vector<string>* pListMaterialDefine ) const
        {
            return isDepthOnly() && usesMaterialShaderForDepth( pListMaterialDefine ) == false && hasMaterialDefine( pListMaterialDefine, kAppearanceDitherDefine );
        }

        /** @brief 머티리얼 define 목록에 이 define 이 있는지 반환합니다. nullptr(머티리얼 없는 배치)이면 false 입니다. */
        static bool hasMaterialDefine( const vector<string>* pListMaterialDefine, const utf8* pDefine )
        {
            if ( pListMaterialDefine == nullptr )
                return false;
            for ( const string& define : *pListMaterialDefine )
            {
                if ( define == pDefine )
                    return true;
            }
            return false;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 패스 종류의 사실을 반환합니다. 테이블에 없는 값이면 Invalid 항목을 반환합니다. */
    SW_API const RenderPassKindInfo& getRenderPassKindInfo( RenderPassKind type );

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
     *          화면 속도를 쓰는 패스(`passWritesVelocity`)는 `kPassVelocityDefine` 을 붙입니다.
     * @param pPassDesc 파이프라인의 패스 서술입니다. nullptr 이면 테이블의 기본값만 씁니다.
     * @param pTargetDesc 컬러 출력을 정하는 서술입니다(`passWritesVelocity`). 파이프라인 패스는 @p pPassDesc 와 같고, 내부 슬롯은 대신 그리는 파이프라인 패스의 서술입니다.
     */
    SW_API RenderPassShaderSelection selectRenderPassShader( RenderPassKind type, const RenderGraphPassDesc* pPassDesc, const RenderGraphPassDesc* pTargetDesc,
                                                             const EngineDefaultAssets& engineDefaultAssets );

    /**
     * @brief 패스가 화면 속도를 쓰는지 반환합니다 — 패스 종류가 `kWritesVelocity` 이고 @p pTargetDesc 의 컬러 출력에 Velocity 역할이 있습니다.
     * @param pTargetDesc 컬러 타깃을 정하는 서술입니다. 파이프라인 패스는 자기 서술, 내부 슬롯(`ForwardOpaqueNoDepthWrite`)은 대신 그리는 패스(`_targetSourceType`)의 서술입니다. nullptr 이면 false 입니다.
     */
    SW_API bool passWritesVelocity( RenderPassKind type, const RenderGraphPassDesc* pTargetDesc );

    /**
     * @brief 패스 @p passDesc 의 컴퓨트 구현(`_implementation="Compute"`)이 컴파일할 셰이더 경로와 define 입니다 — 픽셀 구현과 같은 파일 · define 에 `kPassComputeImplementationDefine` 을 더한다.
     * @details 런타임 PSO(`FramePSOManager`)와 쿠커가 함께 부른다. 진입점은 컴퓨트 스테이지 기본값(`CSMain`)이다.
     */
    SW_API RenderPassShaderSelection selectRenderPassComputeShader( const RenderGraphPassDesc& passDesc, const EngineDefaultAssets& engineDefaultAssets );

    /**
     * @brief 조명 패스 @p lightingDesc 의 타일 종류 @p tileClass 변형이 컴파일할 셰이더 경로와 define 입니다 — 조명 패스의 것에 `SW_DEFERRED_TILE_CLASS=<종류>` 를 더한다.
     * @details 런타임 PSO(`FramePSOManager` 의 타일 PSO)와 쿠커가 함께 부른다. 정점 진입점은 `kDeferredTileVertexEntryPoint` 다.
     */
    SW_API RenderPassShaderSelection selectDeferredTileLightingShader( const RenderGraphPassDesc& lightingDesc, uint32 tileClass, const EngineDefaultAssets& engineDefaultAssets );

    /** @brief `kDiagnostic` 패스가 이 실행에서 요청됐는지 반환합니다(NanCheck 는 `-gv_nanCheck`). 진단 패스가 아니면 false 입니다. */
    SW_API bool isDiagnosticPassRequested( RenderPassKind type );

    /** @brief 패스가 HDR 색을 톤맵하는지 반환합니다 — Tonemap 이거나 퍼뮤테이션 `kPostTonemapDefine` 을 켠 Present(후처리 체인)입니다. */
    SW_API bool passAppliesTonemap( const RenderGraphPassDesc& passDesc );
} // namespace sw
