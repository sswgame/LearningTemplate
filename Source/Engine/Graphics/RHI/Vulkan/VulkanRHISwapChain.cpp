#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    bool VulkanRHISwapChain::createSurface( VkInstance instance, void* pWindowHandle, void* pDisplayHandle, uint32 linuxWsi )
    {
#if defined( SW_PLATFORM_WINDOWS )
        (void)pDisplayHandle;
        (void)linuxWsi;

        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd      = static_cast<HWND>( pWindowHandle );
        createInfo.hinstance = GetModuleHandle( nullptr );

        if ( vkCreateWin32SurfaceKHR( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create window surface!" );
            return false;
        }
        return true;
#elif defined( SW_PLATFORM_LINUX )
        Display* pDisplay = static_cast<Display*>( pDisplayHandle );
        Window   window   = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindowHandle ) );
        if ( pDisplay == nullptr || window == 0 )
        {
            SW_LOG_ERROR( "Invalid X11 display/window for Vulkan surface." );
            return false;
        }

        if ( linuxWsi == 1 )
        {
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

        if ( linuxWsi == 2 )
        {
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

        SW_LOG_ERROR( "No Linux Vulkan WSI selected during instance creation." );
        return false;
#elif defined( SW_PLATFORM_MACOS )
        (void)pDisplayHandle;
        (void)linuxWsi;

        VkMetalSurfaceCreateInfoEXT createInfo{};
        createInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        createInfo.pLayer = pWindowHandle;

        if ( vkCreateMetalSurfaceEXT( instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create Metal window surface!" );
            return false;
        }
        return true;
#else
        (void)instance;
        (void)pWindowHandle;
        (void)pDisplayHandle;
        (void)linuxWsi;
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

    void VulkanRHISwapChain::setRequested( RHIFormat format, uint32 bufferCount )
    {
        _requestedFormat        = format;
        _actualBackBufferFormat = format;
        _requestedBufferCount   = bufferCount;
    }

    bool VulkanRHISwapChain::create( VkPhysicalDevice physicalDevice, VkDevice device, uint32 width, uint32 height )
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, _surface, &capabilities );

        uint32 formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, _surface, &formatCount, nullptr );
        vector<VkSurfaceFormatKHR> formats( formatCount );
        vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, _surface, &formatCount, formats.data() );

        // 백버퍼 포맷은 백엔드 간 계약이다(constant::kBackBufferFormat). 파이프라인이 그 포맷으로
        // 렌더패스를 만들기 때문에, 여기서 다른 걸 고르면 백버퍼에 직접 그리는 패스가 통째로
        // 렌더패스 비호환이 된다. 요청 포맷 → 대체 → 첫 번째 순으로 고르고, 요청을 못 맞추면
        // getActualBackBufferFormat() 으로 실제 값을 보고한다(조용히 어긋나게 두지 않는다).
        const VkFormat requestedFormat = VulkanRHIDeviceInternal::toVulkanTextureFormat( _requestedFormat );
        const VkFormat arrPreferred[]  = { requestedFormat, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };

        VkSurfaceFormatKHR surfaceFormat   = formats[0];
        bool               bFoundPreferred = false;
        for ( const VkFormat preferred : arrPreferred )
        {
            for ( const VkSurfaceFormatKHR& availableFormat : formats )
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
            SW_LOG_ERROR( "스왑체인이 요청 포맷(%#)을 지원하지 않아 %# 로 대체됐습니다 — 백버퍼를 직접 "
                          "타깃으로 하는 파이프라인이 렌더패스 비호환이 될 수 있습니다.",
                          static_cast<uint32>( requestedFormat ), static_cast<uint32>( surfaceFormat.format ) );
        }

        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

        // 서피스가 크기를 고정한 경우( currentExtent != UINT32_MAX ) 반드시 그 값을 써야 합니다.
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
            SW_LOG_TRACE( "createSwapChain: 서피스 크기가 0 (%#x%#) — 창이 최소화/종료 중입니다. 스왑체인 생성을 건너뜁니다.",
                          extent.width, extent.height );
            return false;
        }

        // 백버퍼 개수도 백엔드 간 계약이다 — 예전엔 이 값을 무시하고 minImageCount + 1 을 썼다.
        // 요청값을 존중하되 서피스 능력으로 클램프한다.
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
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform     = preTransform;
        createInfo.compositeAlpha   = compositeAlpha;
        createInfo.presentMode      = presentMode;
        createInfo.clipped          = VK_TRUE;

        if ( vkCreateSwapchainKHR( device, &createInfo, nullptr, &_swapChain ) != VK_SUCCESS )
            return false;

        vkGetSwapchainImagesKHR( device, _swapChain, &imageCount, nullptr );
        _listImage.resize( imageCount );
        vkGetSwapchainImagesKHR( device, _swapChain, &imageCount, _listImage.data() );

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
        // acquire 세마포어는 프레임 슬롯으로, renderFinished 세마포어는 이미지 인덱스로 센다 —
        // 두 개수는 다를 수 있으므로 각자 맞는 크기로 잡는다. 예전엔 둘 다 이미지 수로 잡아서,
        // 드라이버가 인플라이트 프레임 수보다 적은 이미지를 주면 범위 밖 접근이 될 수 있었다.
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
        // 스왑체인이 소유한 이미지이므로 핸들만 버립니다.
        _listImage.clear();
        _imageIndex = 0;
    }

    namespace
    {
        /** @brief VkResult 를 프레임 루프가 분기하는 네 갈래로 접습니다. */
        VulkanSwapChainStatus toSwapChainStatus( VkResult result )
        {
            if ( result == VK_SUCCESS )
                return VulkanSwapChainStatus::Success;
            if ( result == VK_SUBOPTIMAL_KHR )
                return VulkanSwapChainStatus::Suboptimal;
            if ( result == VK_ERROR_OUT_OF_DATE_KHR )
                return VulkanSwapChainStatus::OutOfDate;
            return VulkanSwapChainStatus::Failed;
        }
    } // namespace

    VulkanSwapChainStatus VulkanRHISwapChain::acquireNextImage( VkDevice device, uint32 frameSlot )
    {
        if ( _swapChain == nullptr || frameSlot >= _listImageAvailableSemaphore.size() )
            return VulkanSwapChainStatus::OutOfDate;

        const VkResult result = vkAcquireNextImageKHR( device, _swapChain, UINT64_MAX,
                                                       _listImageAvailableSemaphore[frameSlot], VK_NULL_HANDLE, &_imageIndex );
        if ( toSwapChainStatus( result ) == VulkanSwapChainStatus::Failed )
            SW_LOG_ERROR( "vkAcquireNextImageKHR failed! Error code: %#", static_cast<int32>( result ) );
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
            SW_LOG_ERROR( "vkQueuePresentKHR failed! Error code: %#", static_cast<int32>( result ) );
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
