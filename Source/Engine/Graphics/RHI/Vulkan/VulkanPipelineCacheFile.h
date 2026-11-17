/**
 * @file VulkanPipelineCacheFile.h
 * @brief Vulkan 파이프라인 캐시 파일의 이름입니다. Vulkan 헤더 없이 읽혀 테스트가 씁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    /**
     * @struct VulkanPipelineCacheFile
     * @brief 파이프라인 캐시 파일을 장치마다 가릅니다(`VkPhysicalDeviceProperties` 의 vendorID · deviceID · driverVersion).
     * @details 캐시 데이터의 머리는 만든 장치를 적고, 다른 장치 · 드라이버는 그 데이터를 버리고 빈 캐시로 시작한다(Vulkan 명세 `vkCreatePipelineCache`).
     *          파일이 하나뿐이면 하드웨어 → software(lavapipe · dzn) → 하드웨어 실행이 하드웨어 캐시를 매번 덮어 처음부터 다시 만든다(R-13).
     *          언리얼 `FVulkanPipelineCacheFile` 도 장치 · 드라이버로 파일을 가른다.
     */
    struct VulkanPipelineCacheFile
    {
        /** @brief `Saved` 아래의 상대 경로입니다(`ShaderCache/vk_pipeline_cache_<vendor>_<device>_<driver>.bin`, 16 진 8 자리). */
        static string makeRelativePath( uint32 vendorID, uint32 deviceID, uint32 driverVersion )
        {
            StringBuilder<constant::buffer::kMax128> path;
            path.appendFormat( "ShaderCache/vk_pipeline_cache_%08x_%08x_%08x.bin", vendorID, deviceID, driverVersion );
            return string{ path.c_str() };
        }
    };
} // namespace sw
