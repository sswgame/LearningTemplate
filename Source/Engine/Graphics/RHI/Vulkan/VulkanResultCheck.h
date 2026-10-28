/**
 * @file VulkanResultCheck.h
 * @brief Vulkan 호출이 돌려준 VkResult 를 받는 두 길입니다(엔진 Vulkan 백엔드, 에디터 ImGui Vulkan 백엔드).
 * @details `VkResult` 는 SDK 의 enum 이고 `vk*` 함수 선언도 고칠 수 없어 `[[nodiscard]]` 를 붙일 수 없습니다. 그래서 결과는 이 두 함수로만
 *          받습니다 — 이어 갈 수 있으면 `isVkSuccess`(오류 로그 + false, `[[nodiscard]]`), 이어 갈 수 없으면 `requireVk`(`SW_FATAL`).
 *          형식으로 못 막는 남은 틈(결과를 아예 받지 않은 호출)은 `[[nodiscard]]` 밖이라 호출하는 쪽이 지킵니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include <vulkan/vulkan.h>

namespace sw
{
    /**
     * @brief VkResult 가 VK_SUCCESS 인지 봅니다. 아니면 무엇이 실패했는지 오류로 남기고 false 입니다.
     * @param pWhat 실패한 호출 이름(예: `"vkBindImageMemory"`)입니다.
     */
    [[nodiscard]] inline bool isVkSuccess( VkResult result, const utf8* pWhat )
    {
        if ( result == VK_SUCCESS )
            return true;
        SW_LOG_ERROR( "%# failed (VkResult %#)", pWhat, static_cast<int32>( result ) );
        return false;
    }

    /** @brief VkResult 가 VK_SUCCESS 가 아니면 이유와 함께 멈춥니다. 실패하면 이어 갈 수 없는 호출(명령 버퍼 시작 · 끝 · 리셋)에 씁니다. */
    inline void requireVk( VkResult result, const utf8* pWhat )
    {
        if ( result != VK_SUCCESS )
            SW_FATAL( "%# failed (VkResult %#)", pWhat, static_cast<int32>( result ) );
    }

    /**
     * @brief Vulkan 의 "개수를 묻고 → 채우는" 두 번 호출(`vkEnumerate*` · `vkGet*KHR`)을 결과까지 확인하며 합니다.
     * @param query `( uint32* pCount, T* pData ) -> VkResult` 인 호출입니다(두 번 부른다).
     * @return 두 호출이 모두 VK_SUCCESS 면 true 이고 @p outList 는 채운 수만큼입니다. 아니면 오류를 남기고 false 이며 목록은 비웁니다
     *         (그 사이 개수가 늘어난 VK_INCOMPLETE 도 실패다 — 잘린 목록으로 고르지 않는다).
     */
    template <typename T, typename Query>
    [[nodiscard]] bool enumerateVk( vector<T>& outList, const utf8* pWhat, Query&& query )
    {
        uint32 count{ 0 };
        outList.clear();
        if ( isVkSuccess( query( &count, static_cast<T*>( nullptr ) ), pWhat ) == false )
            return false;
        outList.resize( count );
        if ( count == 0 )
            return true;
        if ( isVkSuccess( query( &count, outList.data() ), pWhat ) == false )
        {
            outList.clear();
            return false;
        }
        outList.resize( count );
        return true;
    }
} // namespace sw
