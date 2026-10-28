/**
 * @file GimmickDamageUtil.h
 * @brief 기믹이 오브젝트에 피해를 주는 단일 경로입니다. "game" 채널 이벤트(`GimmickDamageEvent`)를 내고, 대상이 기믹 센서를 가지고 있으면 그 센서의 Damage 입력에도 넣습니다. 폭발 연쇄가 이 입력을 씁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /** @struct GimmickDamageUtil */
    struct SW_GF_API GimmickDamageUtil
    {
        /**
         * @brief @p target 에 @p amount 만큼 피해를 줍니다. 아무 스레드에서 불러도 됩니다.
         * @details 틱 중이면 틱 뒤의 게임 스레드로 미룬다. 병렬 틱에서 다른 오브젝트의 센서에 바로 넣으면, 그 센서가 이번 틱에 처리할지 다음 틱에 처리할지가
         *          스케줄에 달려서 결과가 결정적이지 않다. 연쇄 폭발과 롤백에서 문제가 된다. 미루면 항상 다음 틱에 처리한다.
         */
        static void applyDamage( GameObject& target, const GameObject* pSource, const hashed_string& kind, float32 amount );
    };
} // namespace sw
