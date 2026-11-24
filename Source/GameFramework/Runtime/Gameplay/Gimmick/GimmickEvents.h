/**
 * @file GimmickEvents.h
 * @brief 기믹이 "game" 채널(`GameEventUtil::send`)에 내는 연출 신호 이벤트입니다.
 * @details 받는 쪽이 대상 하나뿐인 효과(발사 · 부스트 · 아이템 지급 · 피해)는 이벤트를 내지 않고 기믹이 그 컴포넌트를 직접 부른다(피해는 맞음 길 `CharacterRayHit::deliverHit`).
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
    /**
     * @brief 연출 신호입니다. 공포 트리거, 폭발, 파괴 단계, 소리 내기에서 냅니다(`GimmickUtil::sendCue`). 프리팹 · 소리 · 카메라 흔들림은 내는 쪽이 큐 표로 바로 걸고,
     *        이 이벤트는 퀘스트 일지(`QuestLogComponent` — 자기 폰이 부른 신호를 센다)가 받습니다.
     */
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
