/**
 * @file VulkanPhysicalDeviceRank.h
 * @brief Vulkan 물리 디바이스의 종류를 고르는 순서로 바꿉니다(디바이스 선택과 테스트가 이 헤더를 씁니다 — Vulkan 헤더 없이 읽힌다).
 * @details 열거 순서는 로더 · 설치된 드라이버 마음이다. 선호 종류만 앞으로 옮기고 나머지를 열거 순서로 두면, 외장 GPU 가 없는 머신에서 CPU 구현(lavapipe)이
 *          내장 GPU 보다 먼저 나와 소프트웨어로 그린다. 그래서 종류 전체에 순서를 준다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct VulkanPhysicalDeviceRank
     * @brief `VkPhysicalDeviceType` 값의 순위입니다. 작을수록 먼저 본다.
     */
    struct VulkanPhysicalDeviceRank
    {
        // VkPhysicalDeviceType 의 값이다(vulkan.h 와 같은지는 VulkanRHIDevice.cpp 가 static_assert 로 본다).
        static constexpr uint32 kOther         = 0;
        static constexpr uint32 kIntegratedGPU = 1;
        static constexpr uint32 kDiscreteGPU   = 2;
        static constexpr uint32 kVirtualGPU    = 3;
        static constexpr uint32 kCPU           = 4;

        /** @brief 종류 @p deviceType 의 순위입니다. 저전력 선호(@p bPreferMinimumPower)면 내장, 아니면 외장이 먼저이고 CPU 구현은 늘 마지막입니다. */
        static constexpr uint32 compute( uint32 deviceType, bool bPreferMinimumPower )
        {
            switch ( deviceType )
            {
                case kDiscreteGPU:
                    return bPreferMinimumPower ? 1u : 0u;
                case kIntegratedGPU:
                    return bPreferMinimumPower ? 0u : 1u;
                case kVirtualGPU:
                    return 2u;
                case kCPU:
                    return 4u;
                default:
                    return 3u; // kOther 와 이 판이 모르는 종류 — GPU 일 수 있으므로 CPU 보다 먼저 본다
            }
        }
    };
} // namespace sw
