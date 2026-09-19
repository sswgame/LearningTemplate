/**
 * @file VulkanRHISamplerRecipe.h
 * @brief 이름 붙인 Vulkan 샘플러 조리법 — "어떤 샘플러인가" 를 한 곳에서 정합니다.
 *
 * [왜 조리법인가]
 * 엔진의 기본 샘플러와 에디터 ImGui 샘플러가 **같은 아홉 줄**을 각자 적고 있었다. 그렇다고 둘을
 * 하나의 객체로 합칠 수는 없다 — 엔진 쪽은 **씬 텍스처용**이라 나중에 비등방 필터링이나 다른 주소
 * 모드로 갈 수 있고, ImGui 폰트·아이콘이 그 변화를 따라가면 안 된다. 합치면 **없는 결합**이 생긴다.
 *
 * 그래서 공유하는 것은 객체가 아니라 **이름 붙은 조리법**이다. 둘 다 "선형 보간 + 가장자리 고정"
 * 을 원해서 값이 같았을 뿐이고, 그 사실을 `linearClamp()` 라는 이름으로 적어 둔다. 한쪽이 다른
 * 성질을 원하게 되면 **이 조리법을 부르지 않으면 된다** — 그때 갈라지는 것이 옳다.
 *
 * @note 실제로 달랐던 한 곳: 엔진만 `borderColor` 를 세우고 있었다. 주소 모드가 `CLAMP_TO_EDGE` 라
 *       그 값은 쓰이지 않으므로 동작 차이는 없었지만, 나란히 두면 버그처럼 보이는 자리였다.
 *       조리법에는 **쓰이는 값만** 담는다.
 */
#pragma once
#include "Core/Common/Types.h"

#include <vulkan/vulkan.h>

namespace sw
{
    /**
     * @struct VulkanRHISamplerRecipe
     * @brief 자주 쓰는 샘플러 설정을 이름으로 돌려줍니다.
     */
    struct VulkanRHISamplerRecipe
    {
        /**
         * @brief 선형 보간 + 가장자리 고정 샘플러 설정입니다.
         * @details UI 텍스처·기본 씬 텍스처처럼 **타일링하지 않고 부드럽게** 읽고 싶은 것에 쓴다.
         *          `maxLod` 를 크게 열어 두어 밉이 있는 텍스처도 그대로 쓸 수 있다.
         */
        static VkSamplerCreateInfo linearClamp()
        {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter    = VK_FILTER_LINEAR;
            samplerInfo.minFilter    = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            // 비등방을 켜지 않았으므로 1.0 이 곧 "쓰지 않는다" 는 뜻이다.
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.maxLod        = 1000.0f;
            return samplerInfo;
        }
    };
} // namespace sw
