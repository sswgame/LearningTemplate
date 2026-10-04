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
        /**
         * @brief @p target 에 @p amount 피해를 줍니다. 아무 스레드에서 불러도 됩니다.
         * @details 틱 중이면 틱 뒤 게임 스레드로 미룹니다 — 병렬 틱에서 다른 오브젝트의 센서에 바로 넣으면 그쪽이 이번 틱에 먹을지 다음 틱에 먹을지가
         *          스케줄에 달려 결과가 결정적이지 않다(사슬 폭발 · 롤백). 미루면 늘 다음 틱에 먹는다.
         */
        static void applyDamage( GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount );
    };
} // namespace sw
