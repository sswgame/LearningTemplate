/**
 * @file GimmickEvents.h
 * @brief 기믹이 "game" 채널(`GameEventUtil::send`)에 내는 이벤트입니다. 피해는 게임의 피해 규칙(어빌리티, 체력)이 받아서 적용합니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 기믹(위험 지대, 폭발, 포탑)이 오브젝트에 피해를 줬음을 알립니다. */
    struct SW_GF_API GimmickDamageEvent final : IEvent
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{}; ///< 피해를 낸 기믹 오브젝트
        hashed_string    _kind{};   ///< 피해 종류(노드 종류, "Explosion", "Turret")
        float32          _amount{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickDamageEvent );
    };
} // namespace sw
