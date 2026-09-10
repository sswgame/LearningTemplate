/**
 * @file RHIShaderRequest.h
 * @brief 파이프라인 서술체(RHIPipelineStateDesc)를 셰이더 컴파일 요청으로 해석하는 규칙 — 백엔드 넷의 공통부
 * @details 예전엔 DX11·DX12·GL·Vulkan 이 각자 desc 를 읽었다: 진입점 기본값, define 파싱, "RT 0 개 = 픽셀 스테이지
 *          없음" 판정, RT 수 정규화, 캐시-아니면-컴파일. 같은 규칙을 네 곳에 복사해 두니 한 곳만 빠지거나 늦게
 *          따라갔다 — Vulkan 은 define 을 아예 안 읽었고, DX12 만 뎁스 전용 파이프라인에 PS 를 붙였다. 여기서 한 번
 *          해석한 요청을 백엔드가 받기만 하면, 축이 하나 더 들어올 때(마스크드 그림자의 PS 같은) 고칠 자리도 하나다.
 *          백엔드 고유의 것(입력 레이아웃·상태 객체·API 호출)은 그대로 백엔드에 남는다 — 정책은 Engine, 메커니즘은 디바이스.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

namespace sw
{
    /**
     * @struct RHIGraphicsShaderRequest
     * @brief 그래픽스 파이프라인 하나가 컴파일해야 할 스테이지와, 그 판정에 쓴 사실들.
     */
    struct SW_API RHIGraphicsShaderRequest
    {
        ShaderCompileDesc      _vertex;              /**< 버텍스 스테이지 요청 — 진입점 기본값·define 이 채워져 있다 */
        ShaderCompileDesc      _pixel;               /**< 픽셀 스테이지 요청 — `_bHasPixelShader` 가 0 이면 쓰지 않는다 */
        uint32                 _numRenderTargets;    /**< 파이프라인이 선언할 컬러 RT 수 (뎁스 전용이면 0, 그 밖엔 1 이상) */
        uint8                  _bHasPixelShader : 1; /**< 픽셀 스테이지를 붙이는가 */
        uint8                  _bDepthOnly      : 1; /**< 뎁스만 쓰는 파이프라인인가 (RT 0 개 + 뎁스 테스트) */
        [[maybe_unused]] uint8 _reserved        : 6;

        RHIGraphicsShaderRequest() noexcept;
    };

    /** @brief 서술체 → 컴파일 요청 해석과, 캐시-아니면-컴파일. 백엔드는 이 둘만 부른다. */
    struct SW_API RHIShaderRequest
    {
        /**
         * @brief 그래픽스 파이프라인 서술체를 해석합니다.
         * @details 규칙: 진입점이 비면 스테이지 기본값(VSMain/PSMain), `_listShaderDefine` 은 두 스테이지에 같이 붙는다,
         *          RT 0 개 + 뎁스 테스트면 뎁스 전용이라 픽셀 스테이지가 없다(경로가 있어도), RT 수는 뎁스 전용이면 0
         *          아니면 1 이상으로 정규화한다.
         */
        static RHIGraphicsShaderRequest resolveGraphics( const RHIPipelineStateDesc& desc, ShaderTargetFormat targetFormat );

        /** @brief 엔진 서비스가 묶여 있으면 ShaderCache 를, 아니면(테스트·툴) 컴파일러를 직접 씁니다. */
        static ShaderCompileResult compile( const ShaderCompileDesc& desc );
    };
} // namespace sw
