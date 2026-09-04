/**
 * @file RHIModuleAbi.h
 * @brief RHI MODULE C ABI 버전 + 스탬프 (Engine과 RHI_*가 일치해야 함)
 *
 * IRHIDevice / IRHICommandList / RHIDeferredCommandList의 public·protected
 * 리플레이 표면이 바이너리 비호환으로 바뀌면 kRHIModuleAbiVersion 및/또는
 * kRHIModuleAbiStamp를 올립니다. Engine과 함께 모든 RHI_* 모듈을 다시 빌드하세요.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;

    inline constexpr uint32 kRHIModuleAbiVersion = 2;
    /** @brief 불투명 표면 지문. CL/디바이스 ABI가 바뀌면 문자열을 바꿉니다.
     *         v3: IRHICommandList/ICommandReplayTarget 에 bindConstantBuffer/bindStructuredBuffer 추가.
     *         v4: drawInstanced (인스턴스드 드로우, GPUScene 인스턴스 버퍼) 추가.
     *         v5: bindComputeConstantBuffer/bindComputeShaderResource 추가 (gpucull 컴퓨트 바인딩 수정). */
    inline constexpr auto kRHIModuleAbiStamp = "rhi-cl-v5-2026-09";

    using PFN_CreateRHIDevice        = IRHIDevice* (*)();
    using PFN_GetRHIModuleAbiVersion = uint32 ( * )();
    using PFN_GetRHIModuleAbiStamp   = const utf8* (*)();
} // namespace sw
