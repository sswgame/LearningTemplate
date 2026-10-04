/**
 * @file RenderPassTypeInfo.h
 * @brief 패스 종류(`RenderPassType`) 하나에 대한 사실을 한 줄에 모은 표입니다.
 * @details 기본 셰이더 · PSO 상태 · 패스 define · 그리는 대상(씬 메시 · 풀스크린 · 컴퓨트) · 입력 계약이 여기 있습니다.
 *          엔진 PSO 등록(`FrameRenderer::ensurePassResources`), 셰이더 쿠킹 요청(`ShaderCookDriver`), 패스 실행
 *          (`FrameRenderer::executePass`), 파이프라인 검증(`RenderPipelineAsset::validate`)이 모두 이 표를 enum 으로 읽습니다.
 *          패스 종류를 하나 더하는 일은 enum 한 줄 + 이 표의 case 하나입니다(전용 실행 코드가 필요한 패스만 executePass 에 case 를 더합니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Common/Common.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassAsset.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassInputSignature.h"

namespace sw
{
    /**
     * @brief G버퍼 패스가 머티리얼 셰이더에 얹는 define 입니다. 픽셀 출력 서명을 MRT 로 바꿉니다.
     * @details 머티리얼이 셰이더 경로를 정하므로 디퍼드의 G버퍼 패스도 머티리얼의 `.hlsl` 로 그립니다. 그 셰이더가
     *          `SV_TARGET` 하나만 내면 노멀 타깃이 클리어 값 그대로 남고, 디퍼드 조명은 화면 전체를 같은 노멀로 계산합니다.
     *          언리얼이 같은 머티리얼을 패스별 셰이더 **타입**으로 감싸는 자리를 이 엔진에서는 define 이 맡습니다.
     * @note 이 문자열과 `binding.hlsli` 의 `#if defined( SW_PASS_GBUFFER )` 가 어긋나면 컴파일은 되고 화면만 틀립니다.
     */
    inline constexpr const utf8* kPassGBufferDefine = "SW_PASS_GBUFFER=1";

    /**
     * @brief 깊이 프리패스가 그림자와 같은 셰이더 파일(shadowdepth.hlsl)을 **카메라** 행렬로 그리게 하는 define 입니다.
     * @details 이 define 이 없으면 프리패스가 광원 행렬(`g_LightViewProj`)로 그려 장면 깊이에 광원 공간의 깊이가 들어갑니다.
     *          언리얼의 EarlyZ 패스 · 유니티 URP 의 Depth Priming 자리입니다.
     */
    inline constexpr const utf8* kPassDepthPrepassDefine = "SW_PASS_DEPTH_PREPASS=1";

    /** @brief `RenderPassType` 열거자 수입니다. 마지막 열거자가 바뀌면 여기를 고칩니다(시험이 리플렉션의 열거자 수와 대조합니다). */
    inline constexpr uint32 kRenderPassTypeCount = static_cast<uint32>( RenderPassType::MeshMorph ) + 1;

    /**
     * @struct RenderPassTraitFlag
     * @brief `RenderPassTypeInfo::_flags` 의 비트입니다.
     */
    struct RenderPassTraitFlag
    {
        static constexpr uint32 kDepthTest             = SW_BIT( 0 );  ///< PSO 기본값: 깊이 테스트(XML 로 끌 수는 있어도 켤 수는 없다)
        static constexpr uint32 kDepthWrite            = SW_BIT( 1 );  ///< PSO 기본값: 깊이 쓰기
        static constexpr uint32 kBlend                 = SW_BIT( 2 );  ///< PSO 기본값: 블렌드(패스 서술이 있으면 그 값이 이긴다)
        static constexpr uint32 kDepthOnly             = SW_BIT( 3 );  ///< 깊이만 쓴다. 출력 선언이 없을 때 컬러 RT 수가 0 이 되는 근거다
        static constexpr uint32 kDrawsSceneMeshes      = SW_BIT( 4 );  ///< 씬 메시(GpuScene 배치)를 그린다. 머티리얼 변형 PSO 를 만들 대상이다
        static constexpr uint32 kDrawsTransparentBatch = SW_BIT( 5 );  ///< 씬 메시 중 반투명 배치 목록을 그린다(아니면 불투명 목록)
        static constexpr uint32 kUsesMaterialShader    = SW_BIT( 6 );  ///< 머티리얼이 선언한 `.hlsl` 로 갈아탄다(아니면 패스 셰이더에 define 만)
        static constexpr uint32 kAppliesViewMode       = SW_BIT( 7 );  ///< 뷰 모드(Unlit · Wireframe)를 받는다
        static constexpr uint32 kGenericFullscreen     = SW_BIT( 8 );  ///< 선언한 입력을 역할로 걸고 첫 출력에 풀스크린 삼각형 하나를 그린다
        static constexpr uint32 kCompute               = SW_BIT( 9 );  ///< 컴퓨트 PSO 다(`CSMain`)
        static constexpr uint32 kRequiresGpuCulling    = SW_BIT( 10 ); ///< 컴퓨트 PSO 를 `_bGpuCulling` 일 때만 만든다(아니면 `_bCompute`)
        static constexpr uint32 kHasInputContract      = SW_BIT( 11 ); ///< `_inputContract` 가 이 패스의 입력을 검사한다
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
        /** @brief 파이프라인 XML 이 `_shaderPath` 를 적지 않았을 때 쓰는 셰이더입니다(EngineDefaultAssets 의 칸). nullptr 이면 엔진 PSO 가 없습니다. */
        string EngineDefaultAssets::* _pDefaultShader{ nullptr };
        /** @brief 패스가 셰이더에 얹는 define 입니다. 파이프라인 XML 의 `_listPermutation` 뒤에 붙습니다. nullptr 이면 없습니다. */
        const utf8* _pPassDefine{ nullptr };
        /** @brief 고정 컬러 포맷(`_colorTargetCount` 개)입니다. nullptr 이면 패스가 선언한 출력의 포맷을 씁니다. */
        const RHIFormat* _pColorFormat{ nullptr };
        /** @brief 풀스크린 패스가 타깃 첨부의 클리어 색 선언이 없을 때 쓰는 색입니다. nullptr 이면 렌더러의 클리어 색입니다. */
        const float4* _pDefaultClear{ nullptr };
        /** @brief 이 패스 타입이 읽는 입력의 역할입니다. `kHasInputContract` 일 때만 뜻이 있습니다. */
        RenderPassInputSignature _inputContract{};
        /** @brief 표의 줄 순서 검사용입니다. 줄 번호 = 이 값입니다. */
        RenderPassType _type{ RenderPassType::Invalid };
        /** @brief 이 패스의 엔진 PSO 가 없을 때 대신 쓸 패스 타입입니다. Invalid 면 대신하지 않습니다. */
        RenderPassType _psoFallbackType{ RenderPassType::Invalid };
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
    /** @brief 패스 종류의 사실을 반환합니다. 표에 없는 값이면 Invalid 줄입니다. */
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
     * @details 경로는 패스 서술의 `_shaderPath` 가 먼저이고 비어 있으면 표의 기본 셰이더입니다. define 은 패스 서술의
     *          `_listPermutation` 뒤에 표의 패스 define 을 (겹치지 않게) 붙인 것입니다. 둘이 어긋나면 런타임이 찾는 해시를
     *          아무도 쿠킹하지 않아 Shipping 에서 그 패스의 드로우가 사라집니다.
     * @param pPassDesc 파이프라인의 패스 서술(nullptr 이면 표의 기본값만 씁니다)
     */
    SW_API RenderPassShaderSelection selectRenderPassShader( RenderPassType type, const RenderGraphPassDesc* pPassDesc, const EngineDefaultAssets& engineDefaultAssets );
} // namespace sw
