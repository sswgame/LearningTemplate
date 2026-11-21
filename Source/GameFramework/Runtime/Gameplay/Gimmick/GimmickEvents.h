/**
 * @file GimmickEvents.h
 * @brief 기믹이 "game" 채널(`GameEventUtil::send`)에 내는 이벤트입니다(피해 · 발사 · 부스트 · 아이템 · 연출 신호 · 소리). 피해는 게임의 피해 규칙(어빌리티, 체력)이 받아서 적용합니다.
 */
#pragma once
#include "Core/Event/EventType.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 기믹(위험 지대, 폭발, 포탑)이 오브젝트에 피해를 줬음을 알립니다. */
    struct SW_GF_API GimmickDamageEvent final : EventBase
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{}; ///< 피해를 낸 기믹 오브젝트
        hashed_string    _kind{};   ///< 피해 종류(노드 종류, "Explosion", "Turret")
        float32          _amount{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickDamageEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 발사대나 스프링이 오브젝트를 쏘았음을 알립니다. 캐릭터 컨트롤러와 이동 몸이 이 이벤트를 받아 속도를 바꿉니다. */
    struct SW_GF_API GimmickLaunchEvent final : EventBase
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{};
        float3           _velocity{};
        SW_DECLARE_GAMEPLAY_EVENT( GimmickLaunchEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 부스트 패드를 밟았음을 알립니다. 차량 몸이 이 이벤트를 받아 부스트를 적용합니다. */
    struct SW_GF_API GimmickBoostEvent final : EventBase
    {
        GameObjectHandle _target{};
        float32          _duration{ 0.0f };
        float32          _strength{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickBoostEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 아이템을 얻었음을 알립니다. 아이템 상자와 채집 지점에서 냅니다. 인벤토리 규칙이 이 이벤트를 받습니다. */
    struct SW_GF_API GimmickItemEvent final : EventBase
    {
        GameObjectHandle _target{};
        GameObjectHandle _source{};
        hashed_string    _item{};
        int32            _count{ 0 };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickItemEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 연출 신호입니다. 공포 트리거, 폭발, 파괴 단계에서 냅니다. 사운드, 카메라 흔들림, 시퀀서가 이름으로 이 신호를 받습니다. */
    struct SW_GF_API GimmickCueEvent final : EventBase
    {
        GameObjectHandle _source{};
        GameObjectHandle _target{};
        hashed_string    _cue{};
        float3           _position{};
        float32          _magnitude{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickCueEvent );
    };
} // namespace sw

namespace sw
{
    /** @brief 소리를 냈음을 알립니다. 잠입 게임에서 반경 안의 AI 가 이 소리를 듣습니다(`AIStimulus::_noiseRadius`). */
    struct SW_GF_API GimmickNoiseEvent final : EventBase
    {
        GameObjectHandle _source{};
        float3           _position{};
        float32          _radius{ 0.0f };
        SW_DECLARE_GAMEPLAY_EVENT( GimmickNoiseEvent );
    };
} // namespace sw
