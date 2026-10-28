#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/X11Headers.h"

    #include <X11/Xlib-xcb.h>
    #include <vulkan/vulkan_wayland.h>
    #include <vulkan/vulkan_xcb.h>
    #include <vulkan/vulkan_xlib.h>
    #include <xcb/xcb.h>

    #include "Core/Common/X11MacroUndef.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    namespace
    {
        /** @brief VkResult 를 프레임 루프가 분기하는 네 분기로 접습니다. */
        VulkanSwapChainStatus toSwapChainStatus( VkResult result )
        {
            if ( result == VK_SUCCESS )
                return VulkanSwapChainStatus::Success;
            if ( result == VK_SUBOPTIMAL_KHR )
                return VulkanSwapChainStatus::Suboptimal;
            if ( result == VK_ERROR_OUT_OF_DATE_KHR )
                return VulkanSwapChainStatus::OutOfDate;
            if ( result == VK_ERROR_SURFACE_LOST_KHR )
                return VulkanSwapChainStatus::SurfaceLost;
            if ( result == VK_ERROR_DEVICE_LOST )
                return VulkanSwapChainStatus::DeviceLost;
            return VulkanSwapChainStatus::Failed;
        }
    } // namespace

    bool VulkanRHISwapChain::createSurface( VkInstance instance, void* pWindowHandle, void* pDisplayHandle, VulkanWsiKind wsiKind )
    {
#if defined( SW_PLATFORM_WINDOWS )
        (void)pDisplayHandle;

        switch ( wsiKind )
        {
            case VulkanWsiKind::Win32:
            {
                VkWin32SurfaceCreateInfoKHR createInfo{};
                createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                createInfo.hwnd      = static_cast<HWND>( pWindowHandle );
                createInfo.hinstance = GetModuleHandleW( nullptr );

                if ( vkCreateWin32SurfaceKHR( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
                {
                    SW_LOG_ERROR( "Failed to create window surface!" );
                    return false;
                }
                return true;
            }
            case VulkanWsiKind::None:
            case VulkanWsiKind::Xlib:
            case VulkanWsiKind::Xcb:
            case VulkanWsiKind::Wayland:
            {
                SW_LOG_ERROR( "Vulkan WSI kind %# cannot create a surface on Windows (only Win32 can).", static_cast<uint32>( wsiKind ) );
                return false;
            }
        }
        SW_LOG_ERROR( "Unknown Vulkan WSI kind %#.", static_cast<uint32>( wsiKind ) );
        return false;
#elif defined( SW_PLATFORM_LINUX )
        // 아래 둘은 X11 길(xlib · xcb)만 읽는다. Wayland 는 핸들의 뜻이 달라 자기 길에서 따로 검사한다.
        Display*   pDisplay          = static_cast<Display*>( pDisplayHandle );
        Window     window            = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindowHandle ) );
        const bool bInvalidX11Handle = pDisplay == nullptr || window == 0;

        switch ( wsiKind )
        {
            case VulkanWsiKind::Wayland:
            {
                // Wayland — 핸들은 wl_surface*, 디스플레이는 wl_display* 다(`WaylandWindow`). 구조체의 두 포인터는 불완전 타입이라 Wayland 헤더가 없어도 된다.
                PFN_vkCreateWaylandSurfaceKHR pCreateFn     = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>( vkGetInstanceProcAddr( instance, "vkCreateWaylandSurfaceKHR" ) );
                const bool                    bCannotCreate = pCreateFn == nullptr || pDisplayHandle == nullptr || pWindowHandle == nullptr;
                if ( bCannotCreate )
                {
                    SW_LOG_ERROR( "Cannot create a Wayland Vulkan surface (loader entry %#, display %#, surface %#)", pCreateFn != nullptr ? 1 : 0,
                                  pDisplayHandle != nullptr ? 1 : 0, pWindowHandle != nullptr ? 1 : 0 );
                    return false;
                }
                VkWaylandSurfaceCreateInfoKHR createInfo{};
                createInfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
                createInfo.display = static_cast<struct wl_display*>( pDisplayHandle );
                createInfo.surface = static_cast<struct wl_surface*>( pWindowHandle );
                if ( pCreateFn( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
                {
                    SW_LOG_ERROR( "Failed to create Wayland Vulkan surface!" );
                    return false;
                }
                return true;
            }
            case VulkanWsiKind::Xlib:
            {
                if ( bInvalidX11Handle )
                {
                    SW_LOG_ERROR( "Invalid X11 display/window for Vulkan surface." );
                    return false;
                }

                PFN_vkCreateXlibSurfaceKHR pCreateFn = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
                    vkGetInstanceProcAddr( instance, "vkCreateXlibSurfaceKHR" ) );
                if ( pCreateFn == nullptr )
                {
                    SW_LOG_ERROR( "vkCreateXlibSurfaceKHR not available from Vulkan loader!" );
                    return false;
                }

                VkXlibSurfaceCreateInfoKHR createInfo{};
                createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
                createInfo.dpy    = pDisplay;
                createInfo.window = window;
                if ( pCreateFn( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
                {
                    SW_LOG_ERROR( "Failed to create Xlib Vulkan surface!" );
                    return false;
                }
                return true;
            }
            case VulkanWsiKind::Xcb:
            {
                if ( bInvalidX11Handle )
                {
                    SW_LOG_ERROR( "Invalid X11 display/window for Vulkan surface." );
                    return false;
                }

                PFN_vkCreateXcbSurfaceKHR pCreateFn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
                    vkGetInstanceProcAddr( instance, "vkCreateXcbSurfaceKHR" ) );
                if ( pCreateFn == nullptr )
                {
                    SW_LOG_ERROR( "vkCreateXcbSurfaceKHR not available from Vulkan loader!" );
                    return false;
                }

                xcb_connection_t* pConnection = XGetXCBConnection( pDisplay );
                if ( pConnection == nullptr )
                {
                    SW_LOG_ERROR( "XGetXCBConnection failed — cannot create Vulkan xcb surface." );
                    return false;
                }

                VkXcbSurfaceCreateInfoKHR createInfo{};
                createInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
                createInfo.connection = pConnection;
                createInfo.window     = static_cast<xcb_window_t>( window );
                if ( pCreateFn( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
                {
                    SW_LOG_ERROR( "Failed to create XCB Vulkan surface!" );
                    return false;
                }
                return true;
            }
            case VulkanWsiKind::None:
            case VulkanWsiKind::Win32:
            {
                SW_LOG_ERROR( "Vulkan WSI kind %# cannot create a surface on Linux (instance creation picks Xlib, Xcb or Wayland).", static_cast<uint32>( wsiKind ) );
                return false;
            }
        }
        SW_LOG_ERROR( "Unknown Vulkan WSI kind %#.", static_cast<uint32>( wsiKind ) );
        return false;
#else
        (void)instance;
        (void)pWindowHandle;
        (void)pDisplayHandle;
        (void)wsiKind;
        return false;
#endif
    }

    void VulkanRHISwapChain::destroySurface( VkInstance instance )
    {
        if ( _surface == nullptr || instance == nullptr )
            return;
        vkDestroySurfaceKHR( instance, _surface, nullptr );
        _surface = nullptr;
    }

    void VulkanRHISwapChain::setRequested( RHIFormat format, uint32 bufferCount, bool bVSync )
    {
        _requestedFormat        = format;
        _actualBackBufferFormat = format;
        _requestedBufferCount   = bufferCount;
        _bRequestedVSync        = bVSync;
    }

    // 길이 유지(긴 함수 정리 기준): 표면 능력 조회 · 형식 고르기 · 스왑체인 · 이미지 뷰 생성의 순서가 곧 의미라 나누지 않는다.
    bool VulkanRHISwapChain::create( VkPhysicalDevice physicalDevice, VkDevice device, uint32 width, uint32 height )
    {
        _bSurfaceEmpty = false;
        // 서피스 질의가 실패하면(서피스 분실 · 메모리 부족) 쓰레기 능력 · 빈 포맷 목록으로 스왑체인을 고르지 않고 실패로 돌려준다(재생성 경로가 다시 부른다).
        VkSurfaceCapabilitiesKHR capabilities{};
        if ( isVkSuccess( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, _surface, &capabilities ), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR" ) == false )
            return false;

        vector<VkSurfaceFormatKHR> listFormat;
        const bool                 bFormatsRead = enumerateVk( listFormat, "vkGetPhysicalDeviceSurfaceFormatsKHR", [physicalDevice, this]( uint32* pCount, VkSurfaceFormatKHR* pData )
                        { return vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, _surface, pCount, pData ); } );
        if ( bFormatsRead == false || listFormat.empty() )
            return false;

        // 백버퍼 포맷은 요청값(constant::kBackBufferFormat)이지 보장이 아니다. 서피스가 B8G8R8A8 만 줄 수 있다.
        // 언리얼 FVulkanSwapChain 과 같은 규칙이다: 요청 → 대체 → 첫 번째 순으로 고르고, 채택한 값을 반환한다
        // (getActualBackBufferFormat → IRHIDevice::getBackBufferFormat). 백버퍼에 그리는 PSO 는 그 값으로
        // 만든다(FrameRenderer::ensurePresentPSO). 렌더타깃 포맷은 PSO 의 일부이지 계약 상수가 아니다.
        const VkFormat requestedFormat = VulkanRHIDeviceInternal::toVulkanTextureFormat( _requestedFormat );
        const VkFormat arrPreferred[]  = { requestedFormat, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };

        VkSurfaceFormatKHR surfaceFormat   = listFormat[0];
        bool               bFoundPreferred = false;
        for ( const VkFormat preferred : arrPreferred )
        {
            for ( const VkSurfaceFormatKHR& availableFormat : listFormat )
            {
                if ( availableFormat.format == preferred && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
                {
                    surfaceFormat   = availableFormat;
                    bFoundPreferred = true;
                    break;
                }
            }
            if ( bFoundPreferred )
                break;
        }
        if ( surfaceFormat.format != requestedFormat )
        {
            SW_LOG_INFO( "The swap chain adopted a different format than requested (requested VkFormat %#, adopted %#) - back buffer PSOs are built with getBackBufferFormat().",
                         static_cast<uint32>( requestedFormat ), static_cast<uint32>( surfaceFormat.format ) );
        }

        // present 모드가 Vulkan 의 VSync 스위치다. DX/GL 처럼 present 호출에 넘길 인자가 없다.
        // FIFO 는 스펙이 항상 지원을 보장하므로 켠 경우엔 그대로 쓰고, 끈 경우에만 서피스가 주는
        // 목록에서 MAILBOX(삼중 버퍼, 티어링 없음) → IMMEDIATE 순으로 고른다. 둘 다 없으면 FIFO 로
        // 남는다. 실패가 아니라 "이 서피스는 끌 수 없다" 이므로 로그만 남긴다.
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if ( _bRequestedVSync == false )
        {
            vector<VkPresentModeKHR> listPresentMode;
            if ( enumerateVk( listPresentMode, "vkGetPhysicalDeviceSurfacePresentModesKHR", [physicalDevice, this]( uint32* pCount, VkPresentModeKHR* pData )
            { return vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, _surface, pCount, pData ); } ) == false )
                return false;

            for ( const VkPresentModeKHR mode : listPresentMode )
            {
                if ( mode == VK_PRESENT_MODE_MAILBOX_KHR )
                {
                    presentMode = mode;
                    break;
                }
                if ( mode == VK_PRESENT_MODE_IMMEDIATE_KHR )
                    presentMode = mode;
            }

            if ( presentMode == VK_PRESENT_MODE_FIFO_KHR )
                SW_LOG_INFO( "VSync off was requested but the surface offers neither MAILBOX nor IMMEDIATE - staying on FIFO." );
        }

        // 서피스가 크기를 고정한 경우( currentExtent != UINT32_MAX ) 반드시 그 값을 써야 한다.
        VkExtent2D extent = capabilities.currentExtent;
        if ( capabilities.currentExtent.width == UINT32_MAX || capabilities.currentExtent.height == UINT32_MAX )
        {
            extent = { width, height };
            if ( extent.width < capabilities.minImageExtent.width )
                extent.width = capabilities.minImageExtent.width;
            if ( extent.width > capabilities.maxImageExtent.width )
                extent.width = capabilities.maxImageExtent.width;
            if ( extent.height < capabilities.minImageExtent.height )
                extent.height = capabilities.minImageExtent.height;
            if ( extent.height > capabilities.maxImageExtent.height )
                extent.height = capabilities.maxImageExtent.height;
        }

        // 창이 최소화되거나 파괴되는 중이면 서피스가 0 크기를 보고한다. 그대로 넘기면
        // vkCreateSwapchainKHR / vkCreateFramebuffer / vkCmdBeginRenderPass 가 줄줄이 0 크기로
        // 불려 검증 오류가 쏟아진다(종료 경로에서 실제로 그랬다). 크기가 생기면 재생성 경로가
        // 다시 부르므로 여기서는 조용히 물러난다.
        if ( extent.width == 0 || extent.height == 0 )
        {
            SW_LOG_TRACE( "createSwapChain: surface size is 0 (%#x%#) - the window is minimizing or closing. Skipping swap chain creation.",
                          extent.width, extent.height );
            _bSurfaceEmpty = true;
            return false;
        }

        // 백버퍼 개수도 백엔드 간 계약이다. 요청값을 존중하되 서피스 능력으로 클램프한다.
        uint32 imageCount = ( _requestedBufferCount > 0 ) ? _requestedBufferCount : ( capabilities.minImageCount + 1 );
        if ( imageCount < capabilities.minImageCount )
            imageCount = capabilities.minImageCount;
        if ( capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount )
            imageCount = capabilities.maxImageCount;

        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ) == 0 )
        {
            if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR ) != 0 )
                compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
            else if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ) != 0 )
                compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
            else if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR ) != 0 )
                compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }

        VkSurfaceTransformFlagBitsKHR preTransform = capabilities.currentTransform;
        if ( ( capabilities.supportedTransforms & preTransform ) == 0 )
            preTransform = ( ( capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ) != 0 ) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = _surface;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat.format;
        createInfo.imageColorSpace  = surfaceFormat.colorSpace;
        createInfo.imageExtent      = extent;
        createInfo.imageArrayLayers = 1;
        // 백버퍼는 그리기(COLOR_ATTACHMENT)만이 아니라 **블릿의 목적지**(포스트 체인의 마지막 블릿 · 오프스크린 → 백버퍼)와
        // **읽기의 출처**(스크린샷 · 리드백)로도 쓰인다. COLOR_ATTACHMENT 만 주면 백버퍼로 블릿하는 프레임마다
        // 검증 레이어가 "TRANSFER_DST 없이 TRANSFER_DST_OPTIMAL 레이아웃" 을 찍는다(RenderPassGPUTest.FusedPostChainMatchesStaged
        // 의 Vulkan 구간). 서피스가 허락하는 것만 더한다.
        VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ( ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) != 0 )
            imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ( ( capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT ) != 0 )
            imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        createInfo.imageUsage       = imageUsage;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform     = preTransform;
        createInfo.compositeAlpha   = compositeAlpha;
        createInfo.presentMode      = presentMode;
        createInfo.clipped          = VK_TRUE;

        if ( vkCreateSwapchainKHR( device, &createInfo, nullptr, &_swapChain ) != VK_SUCCESS )
            return false;

        if ( enumerateVk( _listImage, "vkGetSwapchainImagesKHR", [device, this]( uint32* pCount, VkImage* pData )
        { return vkGetSwapchainImagesKHR( device, _swapChain, pCount, pData ); } ) == false )
            return false;

        _imageFormat            = static_cast<uint32>( surfaceFormat.format );
        _actualBackBufferFormat = ( surfaceFormat.format == requestedFormat )
                                    ? _requestedFormat
                                    : ( ( surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM ) ? RHIFormat::B8G8R8A8_UNORM
                                                                                             : RHIFormat::R8G8B8A8_UNORM );
        _extentWidth            = extent.width;
        _extentHeight           = extent.height;
        _imageIndex             = 0;

        return createImageViews( device );
    }

    bool VulkanRHISwapChain::createImageViews( VkDevice device )
    {
        _listImageView.resize( _listImage.size() );
        for ( size_t imageIndex = 0; imageIndex < _listImage.size(); imageIndex++ )
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image                           = _listImage[imageIndex];
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = static_cast<VkFormat>( _imageFormat );
            createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            if ( vkCreateImageView( device, &createInfo, nullptr, &_listImageView[imageIndex] ) != VK_SUCCESS )
            {
                for ( size_t cleanupIndex = 0; cleanupIndex < imageIndex; ++cleanupIndex )
                {
                    if ( _listImageView[cleanupIndex] != VK_NULL_HANDLE )
                        vkDestroyImageView( device, _listImageView[cleanupIndex], nullptr );
                }
                _listImageView.clear();
                return false;
            }
        }
        return true;
    }

    bool VulkanRHISwapChain::createFramebuffers( VkDevice device, VkRenderPass renderPass )
    {
        _listFramebuffer.resize( _listImageView.size(), VK_NULL_HANDLE );
        for ( size_t imageIndex = 0; imageIndex < _listImageView.size(); imageIndex++ )
        {
            VkImageView             arrAttachment[] = { _listImageView[imageIndex] };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments    = arrAttachment;
            framebufferInfo.width           = _extentWidth;
            framebufferInfo.height          = _extentHeight;
            framebufferInfo.layers          = 1;
            if ( vkCreateFramebuffer( device, &framebufferInfo, nullptr, &_listFramebuffer[imageIndex] ) != VK_SUCCESS )
            {
                for ( size_t cleanupIndex = 0; cleanupIndex < imageIndex; ++cleanupIndex )
                {
                    if ( _listFramebuffer[cleanupIndex] != VK_NULL_HANDLE )
                        vkDestroyFramebuffer( device, _listFramebuffer[cleanupIndex], nullptr );
                }
                _listFramebuffer.clear();
                return false;
            }
        }
        return true;
    }

    bool VulkanRHISwapChain::createSemaphores( VkDevice device, uint32 frameCountInFlight )
    {
        // acquire 세마포어는 프레임 슬롯으로, renderFinished 세마포어는 이미지 인덱스로 센다.
        // 두 개수는 다를 수 있으므로 각자 맞는 크기로 잡는다. 둘 다 이미지 수로 잡으면 드라이버가 인플라이트
        // 프레임 수보다 적은 이미지를 줄 때 범위 밖 접근이 된다.
        _listImageAvailableSemaphore.resize( frameCountInFlight );
        _listRenderFinishedSemaphore.resize( _listImage.size() );

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for ( VkSemaphore& semaphore : _listImageAvailableSemaphore )
        {
            if ( vkCreateSemaphore( device, &semaphoreInfo, nullptr, &semaphore ) != VK_SUCCESS )
                return false;
        }
        for ( VkSemaphore& semaphore : _listRenderFinishedSemaphore )
        {
            if ( vkCreateSemaphore( device, &semaphoreInfo, nullptr, &semaphore ) != VK_SUCCESS )
                return false;
        }
        return true;
    }

    void VulkanRHISwapChain::destroySemaphores( VkDevice device )
    {
        if ( device == nullptr )
            return;

        for ( VkSemaphore semaphore : _listRenderFinishedSemaphore )
        {
            if ( semaphore != VK_NULL_HANDLE )
                vkDestroySemaphore( device, semaphore, nullptr );
        }
        _listRenderFinishedSemaphore.clear();

        for ( VkSemaphore semaphore : _listImageAvailableSemaphore )
        {
            if ( semaphore != VK_NULL_HANDLE )
                vkDestroySemaphore( device, semaphore, nullptr );
        }
        _listImageAvailableSemaphore.clear();
    }

    void VulkanRHISwapChain::destroy( VkDevice device )
    {
        if ( device == nullptr )
            return;

        for ( VkFramebuffer framebuffer : _listFramebuffer )
        {
            if ( framebuffer != VK_NULL_HANDLE )
                vkDestroyFramebuffer( device, framebuffer, nullptr );
        }
        _listFramebuffer.clear();

        for ( VkImageView imageView : _listImageView )
        {
            if ( imageView != VK_NULL_HANDLE )
                vkDestroyImageView( device, imageView, nullptr );
        }
        _listImageView.clear();

        if ( _swapChain != nullptr )
        {
            vkDestroySwapchainKHR( device, _swapChain, nullptr );
            _swapChain = nullptr;
        }
        // 스왑체인이 소유한 이미지이므로 핸들만 버린다.
        _listImage.clear();
        _imageIndex = 0;
    }

    VulkanSwapChainStatus VulkanRHISwapChain::acquireNextImage( VkDevice device, uint32 frameSlot )
    {
        if ( _swapChain == nullptr || frameSlot >= _listImageAvailableSemaphore.size() )
            return VulkanSwapChainStatus::OutOfDate;

        const VkResult result = vkAcquireNextImageKHR( device, _swapChain, UINT64_MAX,
                                                       _listImageAvailableSemaphore[frameSlot], VK_NULL_HANDLE, &_imageIndex );
        if ( toSwapChainStatus( result ) == VulkanSwapChainStatus::Failed )
            SW_LOG_ERROR_ONCE( "vkAcquireNextImageKHR failed! Error code: %#", static_cast<int32>( result ) );
        return toSwapChainStatus( result );
    }

    VulkanSwapChainStatus VulkanRHISwapChain::present( VkQueue queue )
    {
        if ( _swapChain == nullptr || _imageIndex >= _listRenderFinishedSemaphore.size() )
            return VulkanSwapChainStatus::OutOfDate;

        VkSemaphore    arrWaitSemaphore[] = { _listRenderFinishedSemaphore[_imageIndex] };
        VkSwapchainKHR arrSwapChain[]     = { _swapChain };

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores    = arrWaitSemaphore;
        presentInfo.swapchainCount     = 1;
        presentInfo.pSwapchains        = arrSwapChain;
        presentInfo.pImageIndices      = &_imageIndex;

        const VkResult result = vkQueuePresentKHR( queue, &presentInfo );
        if ( toSwapChainStatus( result ) == VulkanSwapChainStatus::Failed )
            SW_LOG_ERROR_ONCE( "vkQueuePresentKHR failed! Error code: %#", static_cast<int32>( result ) );
        return toSwapChainStatus( result );
    }

    VkImage VulkanRHISwapChain::getCurrentImage() const
    {
        if ( _imageIndex >= _listImage.size() )
            return VK_NULL_HANDLE;
        return _listImage[_imageIndex];
    }

    VkFramebuffer VulkanRHISwapChain::getCurrentFramebuffer() const
    {
        if ( _imageIndex >= _listFramebuffer.size() )
            return VK_NULL_HANDLE;
        return _listFramebuffer[_imageIndex];
    }

    VkSemaphore VulkanRHISwapChain::getImageAvailableSemaphore( uint32 frameSlot ) const
    {
        if ( frameSlot >= _listImageAvailableSemaphore.size() )
            return VK_NULL_HANDLE;
        return _listImageAvailableSemaphore[frameSlot];
    }

    VkSemaphore VulkanRHISwapChain::getRenderFinishedSemaphore() const
    {
        if ( _imageIndex >= _listRenderFinishedSemaphore.size() )
            return VK_NULL_HANDLE;
        return _listRenderFinishedSemaphore[_imageIndex];
    }
} // namespace sw
