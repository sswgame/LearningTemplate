/**
 * @file VulkanRHIHandle.h
 * @brief Vulkan C 핸들 타입의 전방 선언 묶음
 * @details Vulkan 백엔드의 **헤더**는 `<vulkan/vulkan.h>` 를 포함하지 않는다. 모듈을 끄고 정적으로
 *          빌드하면 `RHIBackendRegistry.cpp`(Engine) 가 `VulkanRHIDevice.h` 를 포함하는데, 그때
 *          Vulkan SDK 헤더 전체가 Engine 번역 단위로 딸려 들어오기 때문이다. 핸들은 전부
 *          불투명 포인터라, 이렇게 이름만 선언해 두면 헤더에서 멤버로 들고 있을 수 있다.
 * @note 실제 Vulkan 함수와 구조체가 필요한 .cpp 는 `VulkanRHIDeviceInternal.h` 를 포함한다.
 */
#pragma once

#define SW_VK_DEFINE_HANDLE( object ) typedef struct object##_T* object;
SW_VK_DEFINE_HANDLE( VkInstance )
SW_VK_DEFINE_HANDLE( VkPhysicalDevice )
SW_VK_DEFINE_HANDLE( VkDevice )
SW_VK_DEFINE_HANDLE( VkQueue )
SW_VK_DEFINE_HANDLE( VkRenderPass )
SW_VK_DEFINE_HANDLE( VkSurfaceKHR )
SW_VK_DEFINE_HANDLE( VkSwapchainKHR )
SW_VK_DEFINE_HANDLE( VkImage )
SW_VK_DEFINE_HANDLE( VkImageView )
SW_VK_DEFINE_HANDLE( VkFramebuffer )
SW_VK_DEFINE_HANDLE( VkCommandPool )
SW_VK_DEFINE_HANDLE( VkCommandBuffer )
SW_VK_DEFINE_HANDLE( VkSemaphore )
SW_VK_DEFINE_HANDLE( VkFence )
SW_VK_DEFINE_HANDLE( VkPipelineLayout )
SW_VK_DEFINE_HANDLE( VkDescriptorSetLayout )
SW_VK_DEFINE_HANDLE( VkDescriptorPool )
SW_VK_DEFINE_HANDLE( VkDescriptorSet )
SW_VK_DEFINE_HANDLE( VkBuffer )
SW_VK_DEFINE_HANDLE( VkDeviceMemory )
SW_VK_DEFINE_HANDLE( VkPipeline )
SW_VK_DEFINE_HANDLE( VkPipelineCache )
SW_VK_DEFINE_HANDLE( VkDebugUtilsMessengerEXT )
SW_VK_DEFINE_HANDLE( VkSampler )
