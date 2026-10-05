/**
 * @file VulkanRHIDeviceInternal.h
 * @brief Vulkan 백엔드의 여러 TU 가 함께 쓰는 내부 도우미와 플랫폼 헤더 묶음입니다.
 * @details `VulkanRHIDevice.cpp` 하나가 2,700 줄이라 초기화 · 디스크립터 · 렌더패스로 나눴는데,
 *          그 조각들과 `VulkanRHIResourceFactory.cpp` 가 같은 도우미(`toVulkanTextureFormat`)와 같은 플랫폼 헤더 묶음을 씁니다.
 *          익명 네임스페이스에 두면 TU 마다 사본이 생겨 포맷을 더할 때 한쪽만 고치게 됩니다.
 * @note 백엔드 내부 전용입니다. RHI 경계 밖으로 나가면 안 됩니다.
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <vulkan/vulkan_win32.h>
#endif

namespace sw
{
    /** @brief Vulkan 백엔드 조각들이 함께 쓰는 순수 변환 · 조회 도우미입니다. */
    struct VulkanRHIDeviceInternal
    {
        static inline VkFormat toVulkanTextureFormat( RHIFormat format )
        {
            switch ( format )
            {
                case RHIFormat::R8G8B8A8_UNORM:
                    return VK_FORMAT_R8G8B8A8_UNORM;
                case RHIFormat::B8G8R8A8_UNORM:
                    return VK_FORMAT_B8G8R8A8_UNORM;
                case RHIFormat::R16G16B16A16_FLOAT:
                    return VK_FORMAT_R16G16B16A16_SFLOAT;
                case RHIFormat::D24_UNORM_S8_UINT:
                    return VK_FORMAT_D24_UNORM_S8_UINT;
                case RHIFormat::R32G32B32_FLOAT:
                    return VK_FORMAT_R32G32B32_SFLOAT;
                case RHIFormat::R32G32_FLOAT:
                    return VK_FORMAT_R32G32_SFLOAT;
                case RHIFormat::R32_FLOAT:
                    return VK_FORMAT_R32_SFLOAT;
                case RHIFormat::BC1_UNORM:
                    return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                case RHIFormat::BC2_UNORM:
                    return VK_FORMAT_BC2_UNORM_BLOCK;
                case RHIFormat::BC3_UNORM:
                    return VK_FORMAT_BC3_UNORM_BLOCK;
                case RHIFormat::BC4_UNORM:
                    return VK_FORMAT_BC4_UNORM_BLOCK;
                case RHIFormat::BC5_UNORM:
                    return VK_FORMAT_BC5_UNORM_BLOCK;
                case RHIFormat::BC7_UNORM:
                    return VK_FORMAT_BC7_UNORM_BLOCK;
                case RHIFormat::BC6H_UF16:
                    return VK_FORMAT_BC6H_UFLOAT_BLOCK;
                case RHIFormat::Unknown: ///< 첨부 없음. Vulkan 에는 대응 값이 없다.
                    break;
            }
            return VK_FORMAT_UNDEFINED;
        }

        static bool hasExtension( const vector<VkExtensionProperties>& listAvailableExt, const utf8* pName )
        {
            for ( const VkExtensionProperties& ext : listAvailableExt )
            {
                if ( StringUtil::equals( ext.extensionName, pName ) )
                    return true;
            }
            return false;
        }
    };
    /** @brief 확장 함수 포인터를 조회해 디버그 메신저를 만듭니다 (없으면 EXTENSION_NOT_PRESENT). */
    inline VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger )
    {
        PFN_vkCreateDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );
        if ( func != nullptr )
            return func( instance, pCreateInfo, pAllocator, pDebugMessenger );
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    /** @brief 디버그 메신저를 파괴합니다. 초기화와 종료가 서로 다른 TU 에 있어 헤더에 둡니다. */
    inline void DestroyDebugUtilsMessengerEXT(
        VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator )
    {
        PFN_vkDestroyDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );
        if ( func != nullptr )
            func( instance, debugMessenger, pAllocator );
    }
} // namespace sw
