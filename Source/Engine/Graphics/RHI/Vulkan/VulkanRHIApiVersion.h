/**
 * @file VulkanRHIApiVersion.h
 * @brief Vulkan 백엔드가 요구하는 API 판입니다. 셰이더 쿠킹 타깃과 물리 디바이스 선택이 이 값 하나를 봅니다.
 * @details Vulkan 헤더 없이 읽히도록 판 번호를 `VK_MAKE_API_VERSION( 0, major, minor, 0 )` 과 같은 비트 배치로 직접 만듭니다
 *          (셰이더 컴파일러와 시험이 이 헤더를 씁니다).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct VulkanRHIApiVersion
     * @brief Vulkan 셰이더 쿠킹 타깃(`-fspv-target-env=vulkan<major>.<minor>`)이자 디바이스가 넘어야 하는 최소 API 판입니다.
     * @details 쿠킹된 SPIR-V 판은 이 타깃이 정합니다(vulkan1.3 → SPIR-V 1.6). 그보다 낮은 디바이스는 그 판의 모듈을 받지 못하므로 셰이더 전부가
     *          무효입니다 — 그래서 물리 디바이스 선택이 이 판에 못 미치는 디바이스를 건너뜁니다. 1.3 디바이스에서는 HLSL `discard` 가 내는
     *          `OpDemoteToHelperInvocation` 의 기능(shaderDemoteToHelperInvocation)도 필수 기능이라 늘 있습니다.
     */
    struct VulkanRHIApiVersion
    {
        static constexpr uint32 kRequiredMajor = 1;
        static constexpr uint32 kRequiredMinor = 3;

        /** @brief `VK_MAKE_API_VERSION( 0, major, minor, 0 )` 과 같은 값입니다. */
        static constexpr uint32 makeApiVersion( uint32 major, uint32 minor ) { return ( major << 22u ) | ( minor << 12u ); }

        /** @brief 요구 판을 API 판 번호로 나타낸 값입니다(`VkApplicationInfo::apiVersion`). `makeApiVersion` 과 같은 배치입니다(클래스 안에서는 그 함수를 상수식으로 부를 수 없다). */
        static constexpr uint32 kRequiredApiVersion = ( kRequiredMajor << 22u ) | ( kRequiredMinor << 12u );

        /** @brief 디바이스가 보고한 API 판(`VkPhysicalDeviceProperties::apiVersion`)이 요구 판 이상인가. 맨 위 변형(variant) 3 비트는 보지 않습니다. */
        static constexpr bool isApiVersionSupported( uint32 apiVersion ) { return ( apiVersion & 0x1FFFFFFFu ) >= kRequiredApiVersion; }

        /**
         * @brief Vulkan 1.minor 타깃으로 쿠킹된 SPIR-V 의 판(SPIR-V 헤더의 `0x00MMmm00`)입니다. 그 Vulkan 판이 받는 가장 높은 SPIR-V 판이기도 합니다.
         * @details Vulkan 1.0 → 1.0, 1.1 → 1.3, 1.2 → 1.5, 1.3 → 1.6.
         */
        static constexpr uint32 computeSpirvVersion( uint32 vulkanMinor )
        {
            uint32 spirvMinor{ 6u };
            if ( vulkanMinor == 0u )
                spirvMinor = 0u;
            else if ( vulkanMinor == 1u )
                spirvMinor = 3u;
            else if ( vulkanMinor == 2u )
                spirvMinor = 5u;
            return ( 1u << 16u ) | ( spirvMinor << 8u );
        }
    };
} // namespace sw
