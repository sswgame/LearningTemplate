/**
 * @file VulkanRHISwapChain.h
 * @brief 창 하나에 붙는 Vulkan 서피스 + 스왑체인입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/IRHISwapChain.h"
#include "Engine/Graphics/RHI/RHISurfaceTransform.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    /**
     * @enum VulkanSwapChainStatus
     * @brief acquire / present 결과 중 **프레임 루프가 실제로 분기하는** 네 가지입니다.
     * @details `VkResult` 를 그대로 내보내지 않는 이유는 두 가지입니다. 헤더에 Vulkan 열거형을 노출하지
     *          않으려는 것이 하나고(`VulkanRHIHandle.h` 참고), 40여 개 결과 코드 중 프레임 루프가
     *          정말 구분하는 것은 이 넷뿐이라는 것이 다른 하나입니다.
     */
    enum class VulkanSwapChainStatus : uint8
    {
        Success,     ///< 그대로 쓸 수 있다
        Suboptimal,  ///< 쓸 수는 있지만 창과 어긋나 있음. 다시 만드는 편이 나음
        OutOfDate,   ///< 못 쓴다. 반드시 다시 만들어야 한다
        SurfaceLost, ///< 서피스가 사라졌다(VK_ERROR_SURFACE_LOST_KHR). 서피스부터 다시 만들어야 한다
        DeviceLost,  ///< 장치를 잃었다(VK_ERROR_DEVICE_LOST). 디바이스가 손실로 기록하고 더는 그리지 않는다
        Failed,      ///< 그 밖의 실패 (원인 코드는 호출 지점에서 이미 로그로 남았다)
    };

    /**
     * @enum VulkanWsiKind
     * @brief 인스턴스 확장으로 고른, 서피스를 만드는 길입니다. `NativeSurfaceKind` 와 달리 같은 X11 창에서도 xlib 와 xcb 를 가릅니다.
     */
    enum class VulkanWsiKind : uint8
    {
        None,    ///< 아직 고르지 않았다(인스턴스 전)
        Win32,   ///< VK_KHR_win32_surface — HWND
        Xlib,    ///< VK_KHR_xlib_surface — Display* 와 X11 Window
        Xcb,     ///< VK_KHR_xcb_surface — Display* 에서 얻은 xcb 연결과 X11 Window
        Wayland, ///< VK_KHR_wayland_surface — wl_display* 와 wl_surface*
        Android, ///< VK_KHR_android_surface — ANativeWindow*(앱 수명 안에서 사라졌다 생긴다 — `IRHIDevice::releaseSurface` · `restoreSurface`)
    };

    /**
     * @class VulkanRHISwapChain
     * @brief 창 하나의 서피스, 스왑체인, 백버퍼 이미지입니다. **만들고, 다시 만들고, 다음 이미지를 받고, 표시합니다.**
     * @details Vulkan 은 다른 API 와 달리 백버퍼에 딸린 것이 많습니다. 이미지, 이미지 뷰, 프레임버퍼,
     *          그리고 "이 이미지를 써도 된다"(acquire)와 "이 이미지를 다 그렸다"(renderFinished)를
     *          알리는 세마포어 두 종류입니다. 모두 스왑체인의 수명과 같이 가므로 여기 모아 둡니다.
     *
     *          세마포어 두 종류는 **인덱스가 다릅니다.** acquire 세마포어는 인플라이트 프레임 슬롯
     *          (`frameSlot`)으로, renderFinished 세마포어는 스왑체인 이미지 인덱스로 셉니다. 드라이버가
     *          인플라이트 프레임 수보다 적은 이미지를 줄 수 있어서 두 개수가 같다는 보장이 없습니다.
     *          획득한 이미지를 표시하지 않고 끝난 프레임(무인 실행 · bPresent=false)은 그 이미지를 쥔 채로 다음 프레임에 넘긴다 —
     *          Vulkan 에는 present 말고 이미지를 돌려주는 길이 없어, 매 프레임 새로 획득하면 이미지가 바닥나 acquire 가 영원히 막힌다.
     * @note 프레임 슬롯(펜스, 커맨드 버퍼, 디스크립터 링)은 스왑체인의 것이 아니라 디바이스의 것입니다.
     *       그래서 `frameSlot` 을 인자로 받습니다. 스왑체인은 그 값을 보관하지 않습니다.
     *       메인 창과 다른 창(`IRHIDevice::createSwapChain`)이 같은 클래스 · 같은 규칙을 쓴다.
     */
    class VulkanRHISwapChain final : public IRHISwapChain
    {
    public:
        VulkanRHISwapChain();
        ~VulkanRHISwapChain() override                             = default;
        VulkanRHISwapChain( const VulkanRHISwapChain& )            = delete;
        VulkanRHISwapChain& operator=( const VulkanRHISwapChain& ) = delete;

        /**
         * @brief 창에 붙는 서피스를 만듭니다. **논리 디바이스보다 먼저** 있어야 합니다.
         * @details 물리 디바이스를 고를 때 "이 큐 패밀리가 이 서피스에 present 할 수 있는가"를 묻기
         *          때문입니다. 그래서 서피스만 인스턴스 단계에서 먼저 만들어 둡니다.
         * @param wsiKind 인스턴스를 만들 때 고른 서피스 확장입니다. 이 플랫폼에서 만들 수 없는 종류(`None` 포함)는 대신하지 않고 false 입니다.
         */
        [[nodiscard]] bool createSurface( VkInstance instance, void* pWindowHandle, void* pDisplayHandle, VulkanWsiKind wsiKind );

        /** @brief 서피스를 파괴합니다. 스왑체인을 먼저 파괴한 뒤에 불러야 합니다. */
        void destroySurface( VkInstance instance );

        /**
         * @brief 백버퍼 포맷, 개수, VSync 요청값을 기억합니다. 서피스 능력으로 제한될 수 있습니다.
         * @details VSync 가 여기 있는 이유: Vulkan 은 다른 셋과 달리 present 호출에 동기화 인자가 없고
         *          **스왑체인의 present 모드**가 그 역할을 합니다. 그래서 스왑체인을 다시 만들 때도 같은
         *          요청을 다시 써야 합니다. 백버퍼 포맷과 개수를 여기 두는 것과 같은 이유입니다.
         */
        void setRequested( RHIFormat format, uint32 bufferCount, bool bVSync );
        /** @brief VSync 요청값만 바꿉니다. 다음 `create` 가 present 모드를 다시 고릅니다. */
        void setRequestedVSync( bool bVSync ) { _bRequestedVSync = bVSync; }

        /**
         * @brief 스왑체인과 이미지 뷰를 만듭니다.
         * @details 요청 크기는 서피스가 고정 크기를 보고하면 무시됩니다(스펙 요구).
         * @return 서피스가 0 크기를 보고하면(창 최소화나 종료 중) false. 오류가 아니라 "지금은 못 만든다" 입니다.
         */
        [[nodiscard]] bool create( VkPhysicalDevice physicalDevice, VkDevice device, uint32 width, uint32 height );

        /** @brief 백버퍼 렌더패스에 맞는 프레임버퍼를 이미지마다 만듭니다. */
        [[nodiscard]] bool createFramebuffers( VkDevice device, VkRenderPass renderPass );

        /** @brief acquire · renderFinished 세마포어를 만듭니다. `create` 로 이미지 수가 정해진 뒤에 부릅니다. */
        [[nodiscard]] bool createSemaphores( VkDevice device, uint32 frameCountInFlight );

        /** @brief 세마포어를 파괴합니다. 스왑체인을 다시 만들 때 이미지 수가 바뀔 수 있어 함께 갱신합니다. */
        void destroySemaphores( VkDevice device );

        /** @brief 프레임버퍼, 이미지 뷰, 스왑체인을 파괴합니다(서피스는 남습니다). */
        void destroy( VkDevice device );

        /**
         * @brief 이번 프레임에 그릴 이미지를 받아 옵니다. 성공하면 `getImageIndex()` 가 갱신됩니다.
         * @details 앞 프레임이 표시하지 않고 쥔 이미지가 있으면 다시 획득하지 않고 그것을 돌려준다(기다릴 세마포어도 없다).
         *          새로 획득했으면 그 신호를 이 프레임의 첫 제출이 기다려야 한다 — `takeAcquireWait` 로 꺼낸다.
         * @param frameSlot 이번 프레임의 인플라이트 슬롯. 이 슬롯의 acquire 세마포어가 신호됩니다.
         */
        VulkanSwapChainStatus acquireNextImage( VkDevice device, uint32 frameSlot );
        /** @brief 방금 획득한 이미지의 acquire 세마포어를 꺼냅니다. 한 번 꺼내면 비웁니다. 물려받은 이미지면 VK_NULL_HANDLE 입니다. */
        VkSemaphore takeAcquireWait();
        /**
         * @brief 현재 이미지를 마지막으로 쓴 프레임이 GPU 에서 끝날 때까지 기다리고, 그 자리를 @p frameFence 로 적습니다.
         * @details 마지막으로 쓴 프레임이 지금 프레임 슬롯(@p frameFence)이면 기다리지 않는다 — 부르는 쪽은 그 펜스를 `beginFrame` 에서 이미 기다렸다(리셋 뒤일 수 있다).
         * @return `vkWaitForFences` 결과(VkResult 값)입니다. 기다릴 것이 없으면 VK_SUCCESS 입니다.
         */
        int32 waitForImageReuse( VkDevice device, VkFence frameFence );
        /** @brief 이미지별 펜스 표를 비웁니다. 디바이스가 프레임 펜스를 다시 만들 때 부릅니다(표는 그 펜스의 사본이다). */
        void removeImageFences();

        /** @brief 현재 이미지를 화면에 표시합니다. 이 이미지의 renderFinished 세마포어를 기다립니다. 결과와 상관없이 이미지를 내려놓습니다. */
        VulkanSwapChainStatus present( VkQueue queue );

        /**
         * @brief 서피스의 지금 회전(`currentTransform`)이나 고정 크기(`currentExtent`)가 이 스왑체인을 만들 때와 달라졌는지입니다(다시 만들 까닭이 있는가).
         * @details `VK_SUBOPTIMAL_KHR` 만으로 다시 만들지 않는다 — Android 10+ 는 preTransform 이 currentTransform 과 다르면 표시마다 SUBOPTIMAL 을 준다
         *          (https://developer.android.com/games/optimize/vulkan-prerotation). 엔진은 90 · 180 · 270 은 currentTransform 으로 만들어 미리 돌리지만(`create`), 거울 회전 등
         *          엔진이 돌리지 않는 값은 IDENTITY 로 두고 합성기에 맡겨 SUBOPTIMAL 이 계속 온다 — 그마다 다시 만들면 같은 스왑체인을 프레임마다 다시 만든다.
         *          서피스가 정말 바뀌었을 때만 참이다: 고정 크기는 논리 크기(`getWidth/Height` — 만들 때의 currentExtent)와, 회전은 만들 때의 currentTransform 과 대조한다.
         */
        [[nodiscard]] bool hasSurfaceChanged( VkPhysicalDevice physicalDevice ) const;

        bool isValid() const { return _swapChain != nullptr; }
        /** @brief 마지막 `create` 가 서피스 크기 0(최소화나 종료 중)이라 물러났는지입니다. 오류가 아니라 기다릴 상태입니다. */
        bool isSurfaceEmpty() const { return _bSurfaceEmpty; }

        uint32 getImageIndex() const { return _imageIndex; }
        uint32 getImageCount() const { return static_cast<uint32>( _listImage.size() ); }
        /** @brief 실제 스왑체인 이미지 너비입니다(프레임버퍼 · 렌더 영역 · 블릿). 회전한 서피스면 논리 크기와 맞바뀐다. */
        uint32 getExtentWidth() const { return _extentWidth; }
        uint32 getExtentHeight() const { return _extentHeight; }
        /** @brief 논리 크기(사용자가 보는 방향)입니다 — 창 크기와 대조한다(`IRHIDevice::resizeSwapChain`). 돌리지 않으면 이미지 크기와 같다. */
        uint32 getWidth() const override { return _logicalWidth; }
        uint32 getHeight() const override { return _logicalHeight; }
        /** @brief 엔진이 이미지를 미리 돌려 써야 하는 각도입니다(만들 때 서피스의 `currentTransform` — `create`). */
        RHISurfaceTransform getSurfaceTransform() const { return _surfaceTransform; }
        /** @brief VkFormat 값입니다. 헤더에서 Vulkan 열거형을 쓰지 않으려고 uint32 로 보관합니다. */
        uint32 getImageFormat() const { return _imageFormat; }
        /** @brief 실제로 채택된 백버퍼 포맷입니다. 요청과 다를 수 있습니다. */
        RHIFormat getActualBackBufferFormat() const { return _actualBackBufferFormat; }

        VkSurfaceKHR getSurface() const { return _surface; }

        /** @brief 현재 이미지입니다. 준비 안 됐으면 nullptr 입니다. */
        VkImage getCurrentImage() const;
        /** @brief 현재 이미지의 백버퍼 프레임버퍼입니다. 준비 안 됐으면 nullptr 입니다. */
        VkFramebuffer getCurrentFramebuffer() const;
        /** @brief 이 프레임 슬롯의 acquire 세마포어입니다. 첫 제출이 이것을 기다립니다. */
        VkSemaphore getImageAvailableSemaphore( uint32 frameSlot ) const;
        /** @brief 현재 이미지의 renderFinished 세마포어입니다. 마지막 제출이 이것을 신호합니다. */
        VkSemaphore getRenderFinishedSemaphore() const;

    private:
        /** @brief 이미지마다 뷰를 만듭니다. `create` 안에서만 부릅니다. */
        [[nodiscard]] bool createImageViews( VkDevice device );

    private:
        VkSurfaceKHR   _surface;
        VkSwapchainKHR _swapChain;

        vector<VkImage>       _listImage;
        vector<VkImageView>   _listImageView;
        vector<VkFramebuffer> _listFramebuffer;

        /// @brief 인플라이트 프레임 슬롯으로 셉니다.
        vector<VkSemaphore> _listImageAvailableSemaphore;
        /// @brief 스왑체인 이미지 인덱스로 셉니다.
        vector<VkSemaphore> _listRenderFinishedSemaphore;
        /// @brief 이미지 인덱스별로, 그 이미지를 마지막으로 쓴 디바이스 인플라이트 펜스입니다(소유하지 않는 사본).
        vector<VkFence> _listImageFence;
        /// @brief 방금 획득해 이 프레임의 첫 제출이 기다릴 세마포어입니다(`takeAcquireWait`).
        VkSemaphore _acquireWait;

        uint32 _imageFormat;
        uint32 _extentWidth;
        uint32 _extentHeight;
        uint32 _logicalWidth;  ///< 논리 너비(서피스의 currentExtent — 회전 전 방향)
        uint32 _logicalHeight; ///< 논리 높이
        uint32 _imageIndex;
        /// @brief 만들 때 서피스가 보고한 회전(VkSurfaceTransformFlagBitsKHR 값 — `hasSurfaceChanged` 가 지금 값과 대조한다).
        uint32 _createdTransform;
        /// @brief 스왑체인의 preTransform 을 엔진 값으로 옮긴 것입니다(Identity 면 엔진이 돌리지 않는다).
        RHISurfaceTransform _surfaceTransform;

        /// @brief 요청한 백버퍼 포맷입니다(계약값 `constant::render::kBackBufferFormat`).
        RHIFormat _requestedFormat;
        /// @brief 실제로 채택된 포맷입니다. 요청과 다르면 `IRHIDevice::getBackBufferFormat` 으로 드러납니다.
        RHIFormat _actualBackBufferFormat;
        /// @brief 요청한 백버퍼 개수입니다. 0 이면 서피스 최소값 + 1 을 씁니다.
        uint32 _requestedBufferCount;
        /// @brief 요청한 VSync 입니다. Vulkan 에서는 present 모드가 이 역할을 합니다(FIFO = 켬).
        uint8 _bRequestedVSync : 1;
        /// @brief 마지막 `create` 가 서피스 크기 0 이라 물러났는지입니다. `create` 가 매번 다시 정합니다.
        uint8 _bSurfaceEmpty : 1;
        /// @brief 획득했지만 아직 표시하지 않은 이미지를 쥐고 있는가입니다. `present` · `destroy` 가 내린다.
        uint8 _bImageHeld : 1;
    };
} // namespace sw
