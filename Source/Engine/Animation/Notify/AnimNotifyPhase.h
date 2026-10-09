/**
 * @file AnimNotifyPhase.h
 * @brief 울린 애니메이션 알림의 종류(`AnimNotifyPhase`) 하나만 둡니다.
 * @details `Component.h`(PCH 안)의 `AnimNotifyInfo` 가 값으로 듭니다. 재생 핵심(`AnimPlayback.h`)을 통째로 include 하면 그 헤더를 고칠 때마다
 *          PCH 를 쓰는 TU 전부가 다시 컴파일되므로 열거형만 따로 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum AnimNotifyPhase
     * @brief 울린 알림의 종류입니다. 길이 없는 알림은 `Instant` 하나, 구간 알림은 `Begin` 과 `End` 가 따로 울립니다(언리얼 AnimNotify · AnimNotifyState).
     * @details 구간 사이의 매 프레임(`Tick`)은 트랙이 아니라 구간을 열어 둔 쪽(알림 디스패치)이 셉니다 — 트랙은 지나간 시각만 압니다.
     */
    enum class AnimNotifyPhase : uint8
    {
        Instant = 0,
        Begin,
        End,
    };
} // namespace sw
