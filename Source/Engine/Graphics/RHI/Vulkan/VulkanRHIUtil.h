/**
 * @file VulkanRHIUtil.h
 * @brief Vulkan 백엔드의 여러 TU 가 함께 쓰는 내부 도우미와 플랫폼 헤더 모음입니다.
 * @details `VulkanRHIDevice.cpp` 하나가 2,700 줄이라 초기화, 디스크립터, 렌더패스로 나눴는데,
 *          그 조각들과 `VulkanRHIResourceFactory.cpp` 가 같은 도우미(`toVulkanTextureFormat`)와 같은 플랫폼 헤더 묶음을 씁니다.
 *          익명 네임스페이스에 두면 TU 마다 사본이 생겨 포맷을 더할 때 한쪽만 고치게 됩니다.
 * @note 백엔드 내부 전용입니다. RHI 경계 밖으로 나가면 안 됩니다.
 */
#pragma once
#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/Backend/RHIFrameFenceRing.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <vulkan/vulkan_win32.h>
#endif

namespace sw
{
    /** @brief Vulkan 백엔드 조각들이 함께 쓰는 순수 변환과 조회 도우미입니다. */
    struct VulkanRHIUtil
    {
        /** @brief RHIFormat 하나의 Vulkan 값입니다. `Unknown` 은 첨부 없음이라 Vulkan 에 대응 값이 없다. */
        struct TextureFormatRow
        {
            RHIFormat _format;
            VkFormat  _vkFormat;
        };

        static constexpr TextureFormatRow kArrTextureFormatRow[] = {
            {    RHIFormat::R8G8B8A8_UNORM,            VK_FORMAT_R8G8B8A8_UNORM},
            {    RHIFormat::B8G8R8A8_UNORM,            VK_FORMAT_B8G8R8A8_UNORM},
            {RHIFormat::R16G16B16A16_FLOAT,       VK_FORMAT_R16G16B16A16_SFLOAT},
            { RHIFormat::D24_UNORM_S8_UINT,         VK_FORMAT_D24_UNORM_S8_UINT},
            {   RHIFormat::R32G32B32_FLOAT,          VK_FORMAT_R32G32B32_SFLOAT},
            {      RHIFormat::R32G32_FLOAT,             VK_FORMAT_R32G32_SFLOAT},
            {         RHIFormat::R32_FLOAT,                VK_FORMAT_R32_SFLOAT},
            {           RHIFormat::Unknown,                 VK_FORMAT_UNDEFINED},
            {         RHIFormat::BC1_UNORM,      VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
            {         RHIFormat::BC2_UNORM,           VK_FORMAT_BC2_UNORM_BLOCK},
            {         RHIFormat::BC3_UNORM,           VK_FORMAT_BC3_UNORM_BLOCK},
            {         RHIFormat::BC4_UNORM,           VK_FORMAT_BC4_UNORM_BLOCK},
            {         RHIFormat::BC5_UNORM,           VK_FORMAT_BC5_UNORM_BLOCK},
            {         RHIFormat::BC7_UNORM,           VK_FORMAT_BC7_UNORM_BLOCK},
            {         RHIFormat::BC6H_UF16,         VK_FORMAT_BC6H_UFLOAT_BLOCK},
            {          RHIFormat::R8_UNORM,                  VK_FORMAT_R8_UNORM},
            {      RHIFormat::R16G16_SNORM,              VK_FORMAT_R16G16_SNORM},
            {    RHIFormat::ASTC_4x4_UNORM,      VK_FORMAT_ASTC_4x4_UNORM_BLOCK},
            {  RHIFormat::ETC2_RGBA8_UNORM, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK},
            {     RHIFormat::EAC_R11_UNORM,       VK_FORMAT_EAC_R11_UNORM_BLOCK},
            {    RHIFormat::EAC_RG11_UNORM,    VK_FORMAT_EAC_R11G11_UNORM_BLOCK},
            {RHIFormat::R32G32B32A32_FLOAT,       VK_FORMAT_R32G32B32A32_SFLOAT},
            {      RHIFormat::R16G16_FLOAT,             VK_FORMAT_R16G16_SFLOAT},
        };

        static inline VkFormat toVulkanTextureFormat( RHIFormat format )
        {
            const size_t index = static_cast<size_t>( format );
            return index < kRHIFormatCount ? kArrTextureFormatRow[index]._vkFormat : VK_FORMAT_UNDEFINED;
        }

        /** @brief 블렌드 계수를 Vulkan 값으로 바꿉니다. */
        static inline VkBlendFactor toVulkanBlendFactor( RHIBlendFactor factor )
        {
            switch ( factor )
            {
                case RHIBlendFactor::Zero:
                    return VK_BLEND_FACTOR_ZERO;
                case RHIBlendFactor::One:
                    return VK_BLEND_FACTOR_ONE;
                case RHIBlendFactor::SrcColor:
                    return VK_BLEND_FACTOR_SRC_COLOR;
                case RHIBlendFactor::InvSrcColor:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case RHIBlendFactor::SrcAlpha:
                    return VK_BLEND_FACTOR_SRC_ALPHA;
                case RHIBlendFactor::InvSrcAlpha:
                    return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case RHIBlendFactor::DstColor:
                    return VK_BLEND_FACTOR_DST_COLOR;
                case RHIBlendFactor::InvDstColor:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case RHIBlendFactor::DstAlpha:
                    return VK_BLEND_FACTOR_DST_ALPHA;
                case RHIBlendFactor::InvDstAlpha:
                    return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case RHIBlendFactor::SrcAlphaSaturate:
                    return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            }
            return VK_BLEND_FACTOR_ONE;
        }

        /** @brief 블렌드 연산을 Vulkan 값으로 바꿉니다. */
        static inline VkBlendOp toVulkanBlendOp( RHIBlendOp op )
        {
            switch ( op )
            {
                case RHIBlendOp::Add:
                    return VK_BLEND_OP_ADD;
                case RHIBlendOp::Subtract:
                    return VK_BLEND_OP_SUBTRACT;
                case RHIBlendOp::ReverseSubtract:
                    return VK_BLEND_OP_REVERSE_SUBTRACT;
                case RHIBlendOp::Min:
                    return VK_BLEND_OP_MIN;
                case RHIBlendOp::Max:
                    return VK_BLEND_OP_MAX;
            }
            return VK_BLEND_OP_ADD;
        }

        /** @brief 비교 함수를 Vulkan 값으로 바꿉니다. */
        static inline VkCompareOp toVulkanCompareOp( RHICompareFunc func )
        {
            switch ( func )
            {
                case RHICompareFunc::Never:
                    return VK_COMPARE_OP_NEVER;
                case RHICompareFunc::Less:
                    return VK_COMPARE_OP_LESS;
                case RHICompareFunc::Equal:
                    return VK_COMPARE_OP_EQUAL;
                case RHICompareFunc::LessEqual:
                    return VK_COMPARE_OP_LESS_OR_EQUAL;
                case RHICompareFunc::Greater:
                    return VK_COMPARE_OP_GREATER;
                case RHICompareFunc::NotEqual:
                    return VK_COMPARE_OP_NOT_EQUAL;
                case RHICompareFunc::GreaterEqual:
                    return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case RHICompareFunc::Always:
                    return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        }

        /** @brief 스텐실 연산을 Vulkan 값으로 바꿉니다. */
        static inline VkStencilOp toVulkanStencilOp( RHIStencilOp op )
        {
            switch ( op )
            {
                case RHIStencilOp::Keep:
                    return VK_STENCIL_OP_KEEP;
                case RHIStencilOp::Zero:
                    return VK_STENCIL_OP_ZERO;
                case RHIStencilOp::Replace:
                    return VK_STENCIL_OP_REPLACE;
                case RHIStencilOp::IncrementClamp:
                    return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                case RHIStencilOp::DecrementClamp:
                    return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                case RHIStencilOp::Invert:
                    return VK_STENCIL_OP_INVERT;
                case RHIStencilOp::IncrementWrap:
                    return VK_STENCIL_OP_INCREMENT_AND_WRAP;
                case RHIStencilOp::DecrementWrap:
                    return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            }
            return VK_STENCIL_OP_KEEP;
        }

        /** @brief 렌더 패스 로드 동작을 Vulkan 값으로 바꿉니다. */
        static inline VkAttachmentLoadOp toVulkanLoadOp( RHIRenderPassLoadOp loadOp )
        {
            switch ( loadOp )
            {
                case RHIRenderPassLoadOp::Clear:
                    return VK_ATTACHMENT_LOAD_OP_CLEAR;
                case RHIRenderPassLoadOp::Load:
                    return VK_ATTACHMENT_LOAD_OP_LOAD;
                case RHIRenderPassLoadOp::DontCare:
                    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }

        /** @brief 렌더 패스 저장 동작을 Vulkan 값으로 바꿉니다. */
        static inline VkAttachmentStoreOp toVulkanStoreOp( RHIRenderPassStoreOp storeOp )
        {
            switch ( storeOp )
            {
                case RHIRenderPassStoreOp::Store:
                    return VK_ATTACHMENT_STORE_OP_STORE;
                case RHIRenderPassStoreOp::DontCare:
                    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_STORE_OP_STORE;
        }

        static bool hasExtension( const vector<VkExtensionProperties>& listAvailableExt, const utf8* pName )
        {
            return VectorUtil::containsIf( listAvailableExt, [&]( const VkExtensionProperties& ext )
            { return StringUtil::equals( ext.extensionName, pName ); } );
        }
    };
    static_assert( isRHIFormatTableInOrder( VulkanRHIUtil::kArrTextureFormatRow ), "kArrTextureFormatRow needs one row per RHIFormat, in enumerator order" );
    /** @brief 확장 함수 포인터를 조회해 디버그 메신저를 만듭니다(없으면 EXTENSION_NOT_PRESENT). */
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
