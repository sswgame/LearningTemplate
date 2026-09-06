/**
 * @file VulkanRHISwapChain.h
 * @brief 창 하나에 붙는 Vulkan 서피스 + 스왑체인
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIHandle.h"

namespace sw
{
    /**
     * @enum VulkanSwapChainStatus
     * @brief acquire / present 결과 중 **프레임 루프가 실제로 분기하는** 네 가지.
     * @details `VkResult` 를 그대로 내보내지 않는 이유는 두 가지다. 헤더에 Vulkan 열거형을 노출하지
     *          않으려는 것이 하나고(`VulkanRHIHandle.h` 참고), 40여 개 결과 코드 중 프레임 루프가
     *          정말 구분하는 건 이 넷뿐이라는 것이 다른 하나다.
     */
    enum class VulkanSwapChainStatus : uint8
    {
        Success,    ///< 그대로 쓸 수 있다
        Suboptimal, ///< 쓸 수는 있지만 창과 어긋나 있다 — 다시 만드는 편이 낫다
        OutOfDate,  ///< 못 쓴다. 반드시 다시 만들어야 한다
        Failed,     ///< 그 밖의 실패 (원인 코드는 호출 지점에서 이미 로그로 남았다)
    };

    /**
     * @class VulkanRHISwapChain
     * @brief 창 하나의 서피스·스왑체인·백버퍼 이미지. **만들고 · 다시 만들고 · 다음 이미지를 받고 · 표시한다.**
     * @details Vulkan 은 다른 API 와 달리 백버퍼에 딸린 것이 많다 — 이미지, 이미지 뷰, 프레임버퍼,
     *          그리고 "이 이미지를 써도 된다"(acquire)와 "이 이미지를 다 그렸다"(renderFinished)를
     *          알리는 세마포어 두 종류다. 전부 스왑체인의 수명과 같이 가므로 여기 모아 둔다.
     *
     *          세마포어 두 종류는 **인덱스가 다르다.** acquire 세마포어는 인플라이트 프레임 슬롯
     *          (`frameSlot`)으로, renderFinished 세마포어는 스왑체인 이미지 인덱스로 센다. 드라이버가
     *          인플라이트 프레임 수보다 적은 이미지를 줄 수 있어서 두 개수가 같다는 보장이 없다.
     * @note 프레임 슬롯(펜스·커맨드버퍼·디스크립터 링)은 스왑체인의 것이 아니라 디바이스의 것이다.
     *       그래서 `frameSlot` 을 인자로 받는다 — 스왑체인이 그 값을 들고 있지 않다.
     */
    class VulkanRHISwapChain
    {
    public:
        VulkanRHISwapChain()                                       = default;
        ~VulkanRHISwapChain()                                      = default;
        VulkanRHISwapChain( const VulkanRHISwapChain& )            = delete;
        VulkanRHISwapChain& operator=( const VulkanRHISwapChain& ) = delete;

        /**
         * @brief 창에 붙는 서피스를 만듭니다. **논리 디바이스보다 먼저** 있어야 합니다.
         * @details 물리 디바이스를 고를 때 "이 큐 패밀리가 이 서피스에 present 할 수 있는가"를 묻기
         *          때문입니다. 그래서 서피스만 인스턴스 단계에서 먼저 만들어 둡니다.
         * @param linuxWsi 0=없음, 1=xlib, 2=xcb (인스턴스 확장 가용성으로 정해진 값)
         */
        bool createSurface( VkInstance instance, void* pWindowHandle, void* pDisplayHandle, uint32 linuxWsi );

        /** @brief 서피스를 파괴합니다. 스왑체인을 먼저 파괴한 뒤에 불러야 합니다. */
        void destroySurface( VkInstance instance );

        /** @brief 백버퍼 포맷·개수 요청값을 기억합니다. 서피스 능력으로 클램프될 수 있습니다. */
        void setRequested( RHIFormat format, uint32 bufferCount );

        /**
         * @brief 스왑체인과 이미지 뷰를 만듭니다.
         * @details 요청 크기는 서피스가 고정 크기를 보고하면 무시됩니다(스펙 요구).
         * @return 서피스가 0 크기를 보고하면(창 최소화/종료 중) false — 오류가 아니라 "지금은 못 만든다".
         */
        bool create( VkPhysicalDevice physicalDevice, VkDevice device, uint32 width, uint32 height );

        /** @brief 백버퍼 렌더패스에 맞는 프레임버퍼를 이미지마다 만듭니다. */
        bool createFramebuffers( VkDevice device, VkRenderPass renderPass );

        /** @brief acquire / renderFinished 세마포어를 만듭니다. `create` 로 이미지 수가 정해진 뒤에. */
        bool createSemaphores( VkDevice device, uint32 frameCountInFlight );

        /** @brief 세마포어를 파괴합니다. 스왑체인을 다시 만들 때 이미지 수가 바뀔 수 있어 함께 갱신합니다. */
        void destroySemaphores( VkDevice device );

        /** @brief 프레임버퍼·이미지 뷰·스왑체인을 파괴합니다(서피스는 남습니다). */
        void destroy( VkDevice device );

        /**
         * @brief 다음에 그릴 이미지를 받아 옵니다. 성공하면 `getImageIndex()` 가 갱신됩니다.
         * @param frameSlot 이번 프레임의 인플라이트 슬롯 — 이 슬롯의 acquire 세마포어가 신호됩니다.
         */
        VulkanSwapChainStatus acquireNextImage( VkDevice device, uint32 frameSlot );

        /** @brief 현재 이미지를 화면에 표시합니다. 이 이미지의 renderFinished 세마포어를 기다립니다. */
        VulkanSwapChainStatus present( VkQueue queue );

        bool isValid() const { return _swapChain != nullptr; }
        bool hasSurface() const { return _surface != nullptr; }

        uint32 getImageIndex() const { return _imageIndex; }
        uint32 getImageCount() const { return static_cast<uint32>( _listImage.size() ); }
        uint32 getExtentWidth() const { return _extentWidth; }
        uint32 getExtentHeight() const { return _extentHeight; }
        /** @brief VkFormat 값. 헤더에서 Vulkan 열거형을 쓰지 않으려고 uint32 로 들고 있습니다. */
        uint32 getImageFormat() const { return _imageFormat; }
        /** @brief 실제로 채택된 백버퍼 포맷. 요청과 다를 수 있습니다. */
        RHIFormat getActualBackBufferFormat() const { return _actualBackBufferFormat; }

        VkSurfaceKHR   getSurface() const { return _surface; }
        VkSwapchainKHR getNative() const { return _swapChain; }

        /** @brief 현재 이미지. 준비 안 됐으면 nullptr. */
        VkImage getCurrentImage() const;
        /** @brief 현재 이미지의 백버퍼 프레임버퍼. 준비 안 됐으면 nullptr. */
        VkFramebuffer getCurrentFramebuffer() const;
        /** @brief 이 프레임 슬롯의 acquire 세마포어. 첫 제출이 이걸 기다립니다. */
        VkSemaphore getImageAvailableSemaphore( uint32 frameSlot ) const;
        /** @brief 현재 이미지의 renderFinished 세마포어. 마지막 제출이 이걸 신호합니다. */
        VkSemaphore getRenderFinishedSemaphore() const;

    private:
        /** @brief 이미지마다 뷰를 만듭니다. `create` 안에서만 부릅니다. */
        bool createImageViews( VkDevice device );

        VkSurfaceKHR   _surface{ nullptr };
        VkSwapchainKHR _swapChain{ nullptr };

        vector<VkImage>       _listImage;
        vector<VkImageView>   _listImageView;
        vector<VkFramebuffer> _listFramebuffer;

        /// @brief 인플라이트 프레임 슬롯으로 센다.
        vector<VkSemaphore> _listImageAvailableSemaphore;
        /// @brief 스왑체인 이미지 인덱스로 센다.
        vector<VkSemaphore> _listRenderFinishedSemaphore;

        uint32 _imageFormat{ 0 };
        uint32 _extentWidth{ 0 };
        uint32 _extentHeight{ 0 };
        uint32 _imageIndex{ 0 };

        /// @brief 요청한 백버퍼 포맷(계약값 `constant::kBackBufferFormat`).
        RHIFormat _requestedFormat{ constant::kBackBufferFormat };
        /// @brief 실제로 채택된 포맷. 요청과 다르면 `IRHIDevice::getBackBufferFormat` 으로 드러난다.
        RHIFormat _actualBackBufferFormat{ constant::kBackBufferFormat };
        /// @brief 요청한 백버퍼 개수. 0이면 서피스 최소값 + 1 을 쓴다.
        uint32 _requestedBufferCount{ 0 };
    };
} // namespace sw
