/**
 * @file AnimationNotifyPhase.h
 * @brief 울린 애니메이션 알림의 종류(`AnimationNotifyPhase`) 하나만 둡니다.
 * @details `Component.h`(PCH 안)의 `AnimationNotifyInfo` 가 값으로 듭니다. 재생 핵심(`AnimationPlayback.h`)을 통째로 include 하면 그 헤더를 고칠 때마다
 *          PCH 를 쓰는 TU 전부가 다시 컴파일되므로 열거형만 따로 둡니다. 컴포넌트 모델 코어는 Animation 폴더를 include 하지 않으므로
 *          Engine/Foundation 에 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @enum AnimationNotifyPhase
     * @brief 울린 알림의 종류입니다. 길이 없는 알림은 `Instant` 하나이고, 구간 알림은 `Begin` 과 `End` 가 따로 울립니다(언리얼의 AnimationNotify 와 AnimationNotifyState).
     * @details 구간 사이의 매 프레임(`Tick`)은 트랙이 아니라 구간을 열어 둔 쪽(알림 디스패치)이 센다. 트랙은 지나간 시각만 안다.
     */
    enum class AnimationNotifyPhase : uint8
    {
        Instant = 0,
        Begin,
        End,
    };
} // namespace sw
