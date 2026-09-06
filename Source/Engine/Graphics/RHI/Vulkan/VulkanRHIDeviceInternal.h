/**
 * @file VulkanRHIDeviceInternal.h
 * @brief Vulkan 백엔드 TU 들이 공유하는 내부 헬퍼와 플랫폼 헤더 묶음
 * @details `VulkanRHIDevice.cpp` 하나가 2,700 줄이라 초기화/디스크립터/렌더패스로 나눴는데,
 *          그 조각들이 같은 헬퍼(`toVulkanTextureFormat`)와 같은 플랫폼 헤더 묶음을 쓴다.
 *          예전엔 익명 네임스페이스에 있어서 TU 를 나누는 순간 보이지 않게 되고,
 *          실제로 `VulkanRHIResource.cpp` 는 같은 변환 함수를 따로 복사해 갖고 있었다 —
 *          포맷을 하나 추가하면 두 곳을 고쳐야 했다는 뜻이다.
 * @note 백엔드 내부 전용이다. RHI 경계 밖으로 나가면 안 된다.
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
    #include <X11/Xlib-xcb.h>
    #include <vulkan/vulkan_xcb.h>
    #include <vulkan/vulkan_xlib.h>
    #include <xcb/xcb.h>
#elif defined( SW_PLATFORM_MACOS )
    #include <vulkan/vulkan_metal.h>
#endif

namespace sw
{
    /** @brief Vulkan 백엔드 조각들이 공유하는 순수 변환/조회 헬퍼. */
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
                case RHIFormat::Unknown: ///< 첨부 없음 — Vulkan 에는 대응 값이 없다.
                default:
                    break;
            }
            return VK_FORMAT_UNDEFINED;
        }

        static bool hasExtensionVal( const vector<VkExtensionProperties>& listAvailableExt, const utf8* pName )
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

    /** @brief 디버그 메신저를 파괴합니다. 초기화와 종료가 서로 다른 TU 에 있어 헤더에 둔다. */
    inline void DestroyDebugUtilsMessengerEXT(
        VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator )
    {
        PFN_vkDestroyDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );
        if ( func != nullptr )
            func( instance, debugMessenger, pAllocator );
    }
} // namespace sw
