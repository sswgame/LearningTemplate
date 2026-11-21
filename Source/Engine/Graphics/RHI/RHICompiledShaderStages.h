/**
 * @file RHICompiledShaderStages.h
 * @brief 백엔드 PSO 생성이 받는 셰이더 바이트코드 서술입니다 — 컴파일 · 리플렉션은 앞단(`IRHIResourceFactory::createPipelineState`)이 한 번 끝냈다.
 * @details 예전에는 백엔드 다섯이 PSO 를 만들며 각자 셰이더 캐시를 불러 컴파일하고 정점 리플렉션을 했다 — 같은 다섯 단계를 저마다의 순서로 돌아 실패 처리 차례가 갈렸다
 *          (Vulkan 은 컴파일 뒤, WebGPU 는 컴파일 전에 선택 스테이지를 거절했다). 이제 백엔드는 바이트코드 · 진입점 · 정점 속성 마스크만 받는다.
 *          셰이더 형식(`ShaderCompileDesc`)을 끌지 않는다 — RHI 백엔드가 Shader/Compile 을 몰라도 된다. 언리얼 `FGraphicsPipelineStateInitializer` 가 셰이더 객체(바이트코드)를 받는 자리,
 *          Godot `shader_create_from_spirv` 가 스테이지 묶음 바이트코드를 받는 자리와 같다(기억에 의존한 비교).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 컴파일된 스테이지 하나입니다(바이트코드와 그 진입점). 바이트코드가 비면 그 스테이지가 없다. */
    struct RHICompiledShaderStage
    {
        vector<uint8> _bytecode;   ///< 백엔드 형식의 바이트코드(DXBC · DXIL · SPIR-V · WebGPU 꾸러미)
        string        _entryPoint; ///< 컴파일에 쓴 진입점(GL `glSpecializeShader` · Vulkan `pName` · WebGPU `entryPoint` 가 같은 이름을 쓴다)

        [[nodiscard]] bool isPresent() const { return _bytecode.empty() == false; }
    };

    /**
     * @struct RHICompiledGraphicsStages
     * @brief 그래픽스 PSO 하나의 스테이지 바이트코드와, 앞단이 서술체에서 판정한 사실들입니다(`RHIShaderRequest::resolveGraphics` 와 같은 규칙).
     */
    struct SW_API RHICompiledGraphicsStages
    {
        RHICompiledShaderStage _vertex;
        RHICompiledShaderStage _pixel;                  ///< `_bHasPixelShader` 가 0 이면 비었다
        RHICompiledShaderStage _geometry;               ///< `_bHasGeometryShader` 가 0 이면 비었다
        RHICompiledShaderStage _hull;                   ///< `_bHasTessellation` 이 0 이면 비었다
        RHICompiledShaderStage _domain;                 ///< `_bHasTessellation` 이 0 이면 비었다
        RHICompiledShaderStage _compute;                ///< 서술체가 컴퓨트 경로를 들었을 때만(DX11 이 그래픽스 레코드에 함께 단다)
        uint32                 _numRenderTargets;       ///< 파이프라인이 선언할 컬러 RT 수(뎁스 전용이면 0, 그 밖엔 1 이상)
        uint32                 _consumedAttributeMask;  ///< 정점 셰이더가 실제로 읽는 정점 속성 비트(`RHIShaderRequest::computeConsumedVertexAttributeMask`)
        uint32                 _vertexSlotMask;         ///< 그 속성이 읽는 정점 버퍼 슬롯 비트(`RHIShaderRequest::toVertexSlotMask`)
        uint8                  _bHasVertexShader   : 1; ///< 서술체에 정점 경로가 있었다
        uint8                  _bHasPixelShader    : 1;
        uint8                  _bDepthOnly         : 1;
        uint8                  _bHasGeometryShader : 1;
        uint8                  _bHasTessellation   : 1;
        uint8                  _bHasComputeShader  : 1;
        [[maybe_unused]] uint8 _reserved           : 2;

        RHICompiledGraphicsStages() noexcept;
    };
} // namespace sw
