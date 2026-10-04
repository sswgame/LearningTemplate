/**
 * @file GimmickDamageUtil.h
 * @brief 기믹이 오브젝트에 피해를 주는 한 길 — "game" 채널 이벤트(`GimmickDamageEvent`)와, 대상이 기믹 센서를 가졌으면 그 Damage 입력(폭발 사슬)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /** @struct GimmickDamageUtil */
    struct SW_GF_API GimmickDamageUtil
    {
        /** @brief @p target 에 @p amount 피해를 줍니다. 아무 스레드에서 불러도 됩니다(이벤트는 버스 스레드가 아니면 큐로, 센서는 원자 값). */
        static void applyDamage( GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount );
    };
} // namespace sw
