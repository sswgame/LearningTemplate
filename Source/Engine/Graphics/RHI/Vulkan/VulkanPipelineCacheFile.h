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

        /**
         * @brief 주기 저장을 할 때인지 봅니다. 지난 저장(@p lastSaveNanoseconds) 뒤로 주기(@p intervalNanoseconds)가 지났고, 캐시 크기가 지난 저장 때와 다르면 true 입니다.
         * @details 크기만 묻는 것은 싸다(`vkGetPipelineCacheData` 에 버퍼 없이). 새 파이프라인이 없으면 크기가 같아 디스크를 건드리지 않는다 — 종료 저장은 바이트를 비교한다.
         */
        static constexpr bool isPeriodicSaveDue( int64 nowNanoseconds, int64 lastSaveNanoseconds, int64 intervalNanoseconds, uint64 cacheBytes, uint64 lastSavedBytes )
        {
            return intervalNanoseconds > 0 && nowNanoseconds - lastSaveNanoseconds >= intervalNanoseconds && cacheBytes != lastSavedBytes;
        }
    };
} // namespace sw
