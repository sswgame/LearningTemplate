/**
 * @file TargetFilter.h
 * @brief 오브젝트를 적의 대상으로 고를 수 있는지 한 자리에서 정합니다(AI 시각 · 포탑이 함께 묻는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /**
     * @struct TargetFilter
     * @brief 꺼졌거나 숨었거나(`State.Hidden`) 쓰러진(`State.Dead`) 오브젝트는 대상이 아닙니다. 고르는 쪽마다 따로 정하면 한쪽만 눈을 감습니다.
     * @details 태그로 적는 까닭은 병렬 틱에서 남의 것을 읽어도 되는 사실이기 때문이다(`GameObject::hasTag` — 태그 쓰기는 틱 뒤로 미뤄진다). 숨음은 숨는 곳
     *          (`HidingSpotComponent`)이, 쓰러짐은 체력 원천의 알림(`IHealthSource::notifyHealthChanged`)이 붙이고 뗀다(언리얼 Lyra 의 `Status.Death` 태그 자리).
     *          무적은 넣지 않는다 — 무적인 대상도 겨누고 맞힌다(피해만 없다).
     */
    struct SW_GF_API TargetFilter
    {
        /** @brief 숨은 오브젝트의 태그입니다. 소리는 그대로 들립니다. */
        static TagID getHiddenTag() { return "State.Hidden"_tag; }
        /** @brief 쓰러진 오브젝트의 태그입니다. */
        static TagID getDeadTag() { return "State.Dead"_tag; }
        /** @brief 대상으로 고를 수 있으면 true 입니다. 병렬 틱에서도 부릅니다. */
        [[nodiscard]] static bool isSelectable( const GameObject& object );
    };
} // namespace sw
