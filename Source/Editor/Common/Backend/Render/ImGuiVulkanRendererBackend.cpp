#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include "Core/Foundation/PlatformOSHeaders.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISamplerPreset.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanResultCheck.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/X11Headers.h"

    #include "Engine/Window/IWindow.h"

    #include <X11/Xlib-xcb.h>
    #include <vulkan/vulkan_xcb.h>
    #include <vulkan/vulkan_xlib.h>
    #include <xcb/xcb.h>

    #include "Core/Foundation/X11MacroUndef.h"
#endif

namespace sw::editor
{
    namespace
    {
        struct ImGuiVulkanRendererBackendInternal
        {
            using ViewportGuard = ImGuiVulkanRendererBackend::ViewportGuard;

            /** @brief 설치한 백엔드의 원래 콜백입니다(`installVulkanViewportGuards` 가 컨텍스트의 사용자 칸에 건다). 걸려 있지 않으면 nullptr 입니다. */
            static const ViewportGuard* findGuard() { return static_cast<const ViewportGuard*>( ImGui::GetIO().UserData ); }

            static void GuardedVkCreateWindow( ImGuiViewport* pViewport )
            {
                const ViewportGuard* pGuard      = findGuard();
                const auto           pfnOriginal = pGuard != nullptr ? pGuard->_pOriginalCreateWindow : nullptr;
                if ( pViewport == nullptr || pfnOriginal == nullptr )
                    return;

                if ( pViewport->Size.x < 1.0f )
                    pViewport->Size.x = 1.0f;
                if ( pViewport->Size.y < 1.0f )
                    pViewport->Size.y = 1.0f;

                pfnOriginal( pViewport );
            }

            static void GuardedVkSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
            {
                const ViewportGuard* pGuard      = findGuard();
                const auto           pfnOriginal = pGuard != nullptr ? pGuard->_pOriginalSetWindowSize : nullptr;
                if ( pViewport == nullptr || pfnOriginal == nullptr )
                    return;

                if ( size.x < 1.0f || size.y < 1.0f )
                    return;

                pfnOriginal( pViewport, size );
            }

            static void installVulkanViewportGuards( ViewportGuard& guard )
            {
                ImGuiIO& io = ImGui::GetIO();
                SW_ASSERT( io.UserData == nullptr || io.UserData == &guard ); // 컨텍스트의 사용자 칸은 이 가드 하나가 쓴다
                io.UserData = &guard;

                ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
                if ( platformIO.Renderer_CreateWindow != nullptr && platformIO.Renderer_CreateWindow != &GuardedVkCreateWindow )
                {
                    guard._pOriginalCreateWindow     = platformIO.Renderer_CreateWindow;
                    platformIO.Renderer_CreateWindow = &GuardedVkCreateWindow;
                }
                if ( platformIO.Renderer_SetWindowSize != nullptr && platformIO.Renderer_SetWindowSize != &GuardedVkSetWindowSize )
                {
                    guard._pOriginalSetWindowSize     = platformIO.Renderer_SetWindowSize;
                    platformIO.Renderer_SetWindowSize = &GuardedVkSetWindowSize;
                }
            }

            static void clearVulkanViewportGuards( ViewportGuard& guard )
            {
                guard = ViewportGuard{};
                if ( ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().UserData == &guard )
                    ImGui::GetIO().UserData = nullptr;
            }

            /** @brief ImGui 텍스처 · 보조 뷰포트가 디스크립터 세트를 받는 풀을 만듭니다. 종류마다 1000 개입니다. */
            [[nodiscard]] static bool createDescriptorPool( VkDevice device, VkDescriptorPool* pOutPool )
            {
                VkDescriptorPoolSize pool_sizes[] = {
                    {               VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                    {         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                    {         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                    {  VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                    {  VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                    {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                    {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                    {      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
                };
                VkDescriptorPoolCreateInfo pool_info = {};
                pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
                pool_info.maxSets                    = 1000 * IM_ARRAYSIZE( pool_sizes );
                pool_info.poolSizeCount              = static_cast<uint32>( IM_ARRAYSIZE( pool_sizes ) );
                pool_info.pPoolSizes                 = pool_sizes;
                return vkCreateDescriptorPool( device, &pool_info, nullptr, pOutPool ) == VK_SUCCESS;
            }

            /** @brief 보조 뷰포트 창의 Vulkan 표면을 만듭니다(`ImGuiPlatformIO::Platform_CreateVkSurface`). 반환은 VkResult 입니다. */
            static int32 createViewportSurface( ImGuiViewport* pVp, ImU64 vulkanInstance, const void* pVkAllocators, ImU64* pOutVkSurface )
            {
#if defined( SW_PLATFORM_WINDOWS )
                // 뷰포트 핸들을 HWND 로 읽는 것은 Win32 플랫폼 백엔드일 때뿐이다 — 아니면 오류를 남기고 표면을 만들지 않는다.
                HWND hWnd = static_cast<HWND>( IImGuiPlatformBackend::tryGetViewportWin32Handle( pVp ) );
                if ( hWnd == nullptr )
                {
                    SW_LOG_ERROR( "The ImGui Vulkan viewport surface needs a Win32 viewport window with a handle" );
                    return static_cast<int32>( VK_ERROR_INITIALIZATION_FAILED );
                }
                VkWin32SurfaceCreateInfoKHR create_info = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                create_info.hwnd                        = hWnd;
                create_info.hinstance                   = GetModuleHandleW( nullptr );
                VkResult result                         = vkCreateWin32SurfaceKHR( reinterpret_cast<VkInstance>( vulkanInstance ), &create_info, static_cast<const VkAllocationCallbacks*>( pVkAllocators ), reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) );
                return result;
#elif defined( SW_PLATFORM_LINUX )
                const VkInstance             instance    = reinterpret_cast<VkInstance>( vulkanInstance );
                const VkAllocationCallbacks* pAllocators = static_cast<const VkAllocationCallbacks*>( pVkAllocators );
                // 뷰포트 핸들을 Display* 로 읽는 것은 X11 플랫폼 백엔드일 때뿐이다(Wayland 의 PlatformHandle 은 wl_display* 라 읽으면 죽는다).
                X11SurfaceHandles x11Surface;
                if ( IImGuiPlatformBackend::tryGetViewportX11Surface( pVp, x11Surface ) == false )
                {
                    SW_LOG_ERROR( "The ImGui Vulkan viewport surface needs an X11 viewport window with a display connection (a Wayland viewport is not supported)" );
                    return static_cast<int32>( VK_ERROR_INITIALIZATION_FAILED );
                }
                Display*           pDpy   = static_cast<Display*>( x11Surface._pDisplay );
                const xcb_window_t window = static_cast<xcb_window_t>( x11Surface._window );

                PFN_vkCreateXlibSurfaceKHR pCreateXlib = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
                    vkGetInstanceProcAddr( instance, "vkCreateXlibSurfaceKHR" ) );
                if ( pCreateXlib != nullptr )
                {
                    VkXlibSurfaceCreateInfoKHR create_info = {};
                    create_info.sType                      = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
                    create_info.dpy                        = pDpy;
                    create_info.window                     = static_cast<Window>( window );
                    return static_cast<int32>( pCreateXlib( instance, &create_info, pAllocators, reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) ) );
                }

                PFN_vkCreateXcbSurfaceKHR pCreateXcb = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
                    vkGetInstanceProcAddr( instance, "vkCreateXcbSurfaceKHR" ) );
                if ( pCreateXcb == nullptr )
                    return static_cast<int32>( VK_ERROR_EXTENSION_NOT_PRESENT );

                xcb_connection_t* pConnection = XGetXCBConnection( pDpy );
                if ( pConnection == nullptr )
                    return static_cast<int32>( VK_ERROR_INITIALIZATION_FAILED );

                VkXcbSurfaceCreateInfoKHR create_info = {};
                create_info.sType                     = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
                create_info.connection                = pConnection;
                create_info.window                    = window;
                return static_cast<int32>( pCreateXcb( instance, &create_info, pAllocators, reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) ) );
#else
    #error "ImGuiVulkanRendererBackend: no Vulkan surface path for this platform"
#endif
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiVulkan" );

    bool ImGuiVulkanRendererBackend::initialize( class IRHIDevice* pRHIDevice )
    {
        if ( pRHIDevice == nullptr )
            return false;

        // 이 백엔드는 Vulkan 전용이다. 핸들은 `IRHIDevice::queryNativeHandles` 로 받는다 — 모듈 경계를 넘는 것은 버전 번호가 든
        // POD(RHINativeHandles) 하나이고 VulkanRHIDevice 의 레이아웃 · vtable 이 아니다. 버전이 다르거나 Vulkan 디바이스가 아니면 Engine 이 거절한다.
        RHINativeHandles vkNative{};
        if ( pRHIDevice->queryNativeHandles( RHIBackend::Vulkan, vkNative ) == false )
            return false;

        _pRHIDevice.set( pRHIDevice );
        _pDevice.set( static_cast<VkDevice>( vkNative._pDevice ) );
        _pQueueMutex.set( static_cast<mutex*>( vkNative._pQueueMutex ) );

        // 장치 · 풀 · 샘플러는 여기서 한 번 정하고 shutdown 까지 바뀌지 않는다(텍스처 등록 · 미뤄 둔 해제가 잠금 없이 읽는다). 실패해도 shutdown 이 정리한다.
        VkDescriptorPool pImguiDescriptorPool = VK_NULL_HANDLE;
        if ( ImGuiVulkanRendererBackendInternal::createDescriptorPool( _pDevice.get(), &pImguiDescriptorPool ) == false )
        {
            SW_LOG_ERROR( "Failed to create Vulkan descriptor pool for ImGui" );
            return false;
        }
        _pImguiDescriptorPool.set( pImguiDescriptorPool );

        // ImGui 폰트 · 아이콘용 샘플러다. 엔진 기본 샘플러와 **같은 레시피**(`VulkanRHISamplerPreset`)를 쓰지만 같은 객체는
        // 아니다. 저쪽이 씬 텍스처를 위해 비등방 필터로 바뀌어도 UI 는 따라가면 안 된다.
        VkSamplerCreateInfo samplerInfo = VulkanRHISamplerPreset::linearClamp();
        VkSampler           pSampler    = VK_NULL_HANDLE;
        if ( vkCreateSampler( _pDevice.get(), &samplerInfo, nullptr, &pSampler ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create Vulkan sampler for ImGui textures" );
            return false;
        }
        _pSampler.set( pSampler );

        // ImageCount 는 이 백엔드에서 뷰포트별 정점 · 인덱스 버퍼 링의 슬롯 수일 뿐이다(스왑체인 이미지 수와 상관없다). 슬롯이 GPU 에서 돌아오는 시간으로 정한다 —
        // 캡처 프레임은 메인 뷰포트를 두 번 그린다(`kImGuiDrawBufferRingSize`). ImGui 는 MinImageCount 이상을 요구한다.
        const uint32 minImages = vkNative._minImageCount >= 2 ? vkNative._minImageCount : 2;
        const uint32 ringSize  = kImGuiDrawBufferRingSize > minImages ? kImGuiDrawBufferRingSize : minImages;

        ImGui_ImplVulkan_InitInfo init_info    = {};
        init_info.Instance                     = static_cast<VkInstance>( vkNative._pInstance );
        init_info.PhysicalDevice               = static_cast<VkPhysicalDevice>( vkNative._pPhysicalDevice );
        init_info.Device                       = _pDevice.get();
        init_info.QueueFamily                  = vkNative._queueFamily;
        init_info.Queue                        = static_cast<VkQueue>( vkNative._pGraphicsQueue );
        init_info.PipelineCache                = VK_NULL_HANDLE;
        init_info.DescriptorPool               = _pImguiDescriptorPool.get();
        init_info.PipelineInfoMain.RenderPass  = static_cast<VkRenderPass>( vkNative._pRenderPass );
        init_info.PipelineInfoMain.Subpass     = 0;
        init_info.MinImageCount                = minImages;
        init_info.ImageCount                   = ringSize;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator                    = nullptr;
        init_info.CheckVkResultFn              = nullptr;

        ImGui::GetPlatformIO().Platform_CreateVkSurface = &ImGuiVulkanRendererBackendInternal::createViewportSurface;

        if ( ImGui_ImplVulkan_Init( &init_info ) == false )
            return false;

        ImGuiVulkanRendererBackendInternal::installVulkanViewportGuards( _viewportGuard );
        return true;
    }

    void ImGuiVulkanRendererBackend::shutdown()
    {
        // 렌더 스레드 · GPU 를 기다려 미뤄 둔 디스크립터 해제를 모두 끝낸 뒤에 풀을 부순다.
        flushDrawReleases( _pRHIDevice.get() );

        for ( pair<void* const, RHITextureHandle>& pair : _mapTextureID )
        {
            if ( pair.first != nullptr )
                ImGui_ImplVulkan_RemoveTexture( static_cast<VkDescriptorSet>( pair.first ) );
        }
        _mapTextureID.clear();

        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
        {
            // 보조 뷰포트 창을 부수며 vkDeviceWaitIdle 을 부른다 — 큐 잠금 안에서.
            const ScopedOptionalLock<mutex> queueLock{ _pQueueMutex.get() };
            ImGui_ImplVulkan_Shutdown();
        }
        ImGuiVulkanRendererBackendInternal::clearVulkanViewportGuards( _viewportGuard );

        if ( _pSampler.get() != nullptr && _pDevice.get() != nullptr )
        {
            vkDestroySampler( _pDevice.get(), _pSampler.get(), nullptr );
            _pSampler.reset();
        }

        if ( _pImguiDescriptorPool.get() != nullptr && _pDevice.get() != nullptr )
        {
            vkDestroyDescriptorPool( _pDevice.get(), _pImguiDescriptorPool.get(), nullptr );
            _pImguiDescriptorPool.reset();
        }

        _pDevice.reset();
        _pRHIDevice.reset();
        _pQueueMutex.reset();
    }

    void ImGuiVulkanRendererBackend::newFrame()
    {
        if ( ImGui::GetIO().BackendRendererUserData != nullptr )
            ImGui_ImplVulkan_NewFrame();
    }

    void ImGuiVulkanRendererBackend::processTextureUpdates()
    {
        // 업로드는 큐에 제출하고 기다린다(큐 잠금). 글꼴 아틀라스 갱신도 같은 풀에서 세트를 잡고 놓는다(풀 잠금). 순서는 큐 → 풀이다 —
        // 렌더 스레드가 큐 잠금 안에서 미뤄 둔 해제(풀 잠금)를 부를 수 있다.
        const ScopedOptionalLock<mutex> queueLock{ _pQueueMutex.get() };
        ScopedLock<mutex>               lock{ _descriptorPoolMutex };
        updatePendingTextures( &ImGui_ImplVulkan_UpdateTexture );
    }

    void ImGuiVulkanRendererBackend::render( class IRHIDevice* pRHIDevice, ImDrawData* pDrawData )
    {
        const bool bNothingToRender = pDrawData == nullptr || pRHIDevice == nullptr || _pRHIDevice.get() == nullptr;
        if ( bNothingToRender )
            return;
        // 프레임 스트림의 열린 세그먼트에 기록한다. 렌더 스레드(present 훅)만 쓰는 버퍼라 콜백 밖으로 들고 나가지 않는다.
        // 다른 백엔드이면 withNativeContext 가 로그를 남기고 부르지 않는다.
        (void)pRHIDevice->withNativeContext( RHIBackend::Vulkan, SW_DELEGATE_LAMBDA( RHINativeContextVisitor, [pDrawData]( void* pContext )
        {
            VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>( pContext );
            if ( cmdBuffer != nullptr )
                ImGui_ImplVulkan_RenderDrawData( pDrawData, cmdBuffer );
        } ) );
    }

    void* ImGuiVulkanRendererBackend::registerTexture( RHITextureHandle texture )
    {
        const bool bCannotRegisterTexture = texture == 0 || _pRHIDevice.get() == nullptr || _pSampler.get() == nullptr;
        if ( bCannotRegisterTexture )
            return nullptr;

        // Vulkan 의 "네이티브 텍스처 포인터" 가 곧 VkImageView 다 — 전용 조회 함수 없이 범용 함수를 쓴다.
        void* pImageViewPtr = _pRHIDevice->tryGetNativeTexturePointer( RHIBackend::Vulkan, texture );
        if ( pImageViewPtr == nullptr )
        {
            SW_LOG_ERROR( "Failed to resolve VkImageView for RHI handle %#", texture );
            return nullptr;
        }

        VkDescriptorSet set{ VK_NULL_HANDLE };
        {
            ScopedLock<mutex> lock{ _descriptorPoolMutex };
            set = ImGui_ImplVulkan_AddTexture( _pSampler.get(), static_cast<VkImageView>( pImageViewPtr ), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
        }
        if ( set == VK_NULL_HANDLE )
            return nullptr;

        auto textureID           = set;
        _mapTextureID[textureID] = texture;
        return textureID;
    }

    void ImGuiVulkanRendererBackend::unregisterTexture( void* pTextureID )
    {
        if ( pTextureID == nullptr )
            return;

        auto it = _mapTextureID.find( pTextureID );
        if ( it == _mapTextureID.end() )
            return;

        _mapTextureID.erase( it );

        // 이미 낸 draw 스냅샷이 이 세트를 아직 그릴 수 있다. 세트를 그린 마지막 프레임의 GPU 완료 뒤에 놓는다. 그 콜백은 렌더 스레드에서
        // 불리므로 UI 스레드의 할당과 같은 잠금 안에서 놓는다. AddTexture 는 InitInfo.DescriptorPool(이 풀)에서 세트를 잡는다.
        const VkDescriptorSet set = static_cast<VkDescriptorSet>( pTextureID );
        getDrawReleaseQueue().enqueue( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [this, set]()
        {
            ScopedLock<mutex> lock{ _descriptorPoolMutex };
            if ( _pDevice.get() != nullptr && _pImguiDescriptorPool.get() != nullptr )
                requireVk( vkFreeDescriptorSets( _pDevice.get(), _pImguiDescriptorPool.get(), 1, &set ), "vkFreeDescriptorSets (ImGui)" ); // 사양상 늘 VK_SUCCESS — 어기면 드라이버 결함
        } ) );
    }
} // namespace sw::editor
