/**
 * @file RHIShaderRequest.h
 * @brief 파이프라인 디스크립터(RHIPipelineStateDesc)를 셰이더 컴파일 요청으로 해석하는 규칙입니다. 네 백엔드의 공통부입니다.
 * @details 진입점 기본값, define 파싱, "RT 0 개 = 픽셀 스테이지 없음" 판정, RT 수 정규화, 캐시 조회 후 컴파일을 여기서 한 번
 *          해석하고 백엔드는 그 요청을 받기만 합니다. 백엔드마다 desc 를 따로 읽으면 한 곳만 규칙을 빠뜨립니다(define 무시,
 *          뎁스 전용 파이프라인에 PS 붙이기). 판정 축이 하나 더 늘어날 때(마스크드 그림자의 PS 같은) 고칠 곳도 하나입니다.
 *          백엔드 고유의 것(입력 레이아웃, 상태 객체, API 호출)은 그대로 백엔드에 남습니다. 정책은 Engine, 메커니즘은 디바이스입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    struct RHIPipelineStateDesc;

    /**
     * @struct RHIGraphicsShaderRequest
     * @brief 그래픽스 파이프라인 하나가 컴파일해야 할 스테이지와, 그 판정에 쓴 사실들입니다.
     */
    struct SW_API RHIGraphicsShaderRequest
    {
        ShaderCompileDesc      _vertex;              /**< 정점 스테이지 요청. 진입점 기본값과 define 이 채워져 있음 */
        ShaderCompileDesc      _pixel;               /**< 픽셀 스테이지 요청. `_bHasPixelShader` 가 0 이면 쓰지 않음 */
        uint32                 _numRenderTargets;    /**< 파이프라인이 선언할 컬러 RT 수 (뎁스 전용이면 0, 그 밖엔 1 이상) */
        uint8                  _bHasPixelShader : 1; /**< 픽셀 스테이지를 붙이는가 */
        uint8                  _bDepthOnly      : 1; /**< 뎁스만 쓰는 파이프라인인가 (RT 0 개 + 뎁스 테스트) */
        [[maybe_unused]] uint8 _reserved        : 6;

        RHIGraphicsShaderRequest() noexcept;
    };
} // namespace sw

namespace sw
{
    /** @brief 디스크립터를 컴파일 요청으로 해석하고, 캐시에 없으면 컴파일합니다. 백엔드는 이 둘만 부릅니다. */
    struct SW_API RHIShaderRequest
    {
        /**
         * @brief 그래픽스 파이프라인 디스크립터를 해석합니다.
         * @details 규칙: 진입점이 비면 스테이지 기본값(VSMain/PSMain)을 쓰고, `_listShaderDefine` 은 두 스테이지에 같이 붙고,
         *          RT 0 개 + 뎁스 테스트면 뎁스 전용이라 픽셀 스테이지가 없고(경로가 있어도), RT 수는 뎁스 전용이면 0
         *          아니면 1 이상으로 정규화합니다.
         */
        static RHIGraphicsShaderRequest resolveGraphics( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat );

        /** @brief 매크로 목록("NAME" 또는 "NAME=VALUE")을 컴파일 요청의 define 에 붙입니다. 컴퓨트 PSO 가 씁니다(그래픽스는 `resolveGraphics` 가 같은 규칙으로 붙인다). */
        static void appendDefines( ShaderCompileDesc& inoutDesc, const vector<string>& listDefine );

        /** @brief 셰이더 캐시에서 바이트코드를 얻습니다(출처는 캐시가 기동 때 고른 공급자 — 쿠킹본 또는 실행 중 컴파일). 컴파일러를 직접 부르지 않는다. */
        static ShaderCompileResult compile( const ShaderCompileDesc& desc );

        /**
         * @brief 그래픽스 요청의 정점 스테이지와 (있으면) 픽셀 스테이지를 컴파일합니다. DX12, GL, Vulkan 이 같이 씁니다.
         * @return 정점 스테이지가 성공했고 픽셀 스테이지는 없거나 성공했으면 true 입니다. 실패한 쪽의 메시지는 그 결과에 남습니다.
         */
        [[nodiscard]] static bool compileGraphics( const RHIGraphicsShaderRequest& request, ShaderCompileResult& outVertex, ShaderCompileResult& outPixel );

        /**
         * @brief 정점 셰이더가 실제로 읽는 정점 속성을 `constant::arrVertexAttribute` 의 인덱스 비트로 반환합니다(리플렉션의 location 으로 대조합니다).
         * @details 셰이더는 모두 `VertexInput` 을 선언하지만 컴파일러가 쓰지 않는 입력을 제거하므로(풀스크린 셰이더의 노멀, UV), PSO 가 테이블 전체를
         *          바인딩하면 Vulkan 검증 레이어가 "Vertex attribute at location N not consumed" 를 낸다. 셰이더가 읽는데 PSO 가 빼면 미정의라
         *          빼는 쪽만 이 마스크로 한다. 리플렉션은 SPIR-V, DXIL, DXBC 모두 된다.
         */
        static uint32 computeConsumedVertexAttributeMask( const vector<uint8>& vertexBytecode, ShaderTargetFormat targetFormat );
    };
} // namespace sw
