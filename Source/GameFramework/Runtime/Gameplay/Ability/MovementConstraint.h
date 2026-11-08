/**
 * @file MovementConstraint.h
 * @brief 이펙트가 이동을 제약하는 계약입니다 — 둔화 · 속박 · 기절을 새 버프 체계 없이 어빌리티 이펙트(속성 · 태그)로 표현하고 이동기가 한 곳에서 읽는다.
 * @details 속도: 속성 `MoveSpeedScale`(이펙트의 Multiply 모디파이어가 곱한다, 속성이 없는 개체는 배율 1 — 이펙트가 걸릴 속성이 없다는 뜻이다).
 *          이동 차단: 태그 `State.MoveBlocked`(자식 `State.MoveBlocked.Rooted` · `.Stunned` 도 같다 — 계층 일치). 점프 차단: `State.JumpBlocked`.
 *          여러 이펙트가 겹쳐도 태그는 개수(`TagCountTable`)로, 속성은 모디파이어 집계로 합성되므로 하나가 풀려도 다른 것의 제약은 남는다 — 효과의 소유 수명이 곧 제약의 수명이다.
 *          언리얼 GAS 의 `MovementSpeed` 속성 + 차단 태그(CharacterMovement 의 `GetMaxSpeed` 가 읽음)와 같은 나눔이다. 공용 이동기(`CharacterPawnMovementComponent` · 2D)가
 *          이것을 읽고, 이동을 직접 짜는 키트(AbilityArena 처럼 `MoveSpeed` 를 읽는 것)는 같은 함수를 부르면 같은 뜻이 된다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObject;

    /** @struct MovementConstraint @brief 개체 하나의 지금 이동 제약입니다(읽기 — 계산은 `read`). */
    struct SW_GF_API MovementConstraint
    {
        float32 _speedScale{ 1.0f };       ///< 이동 속도 배율(0 이상)
        uint8   _bMoveBlocked{ SW_FALSE }; ///< 스스로 움직이지 못한다(속박 · 기절). 밀림 · 중력은 그대로다
        uint8   _bJumpBlocked{ SW_FALSE }; ///< 점프하지 못한다(이동 차단이면 점프도 막는다)

        /** @brief 이동 차단 태그 `State.MoveBlocked` 입니다. */
        static TagID getMoveBlockedTag();
        /** @brief 점프 차단 태그 `State.JumpBlocked` 입니다. */
        static TagID getJumpBlockedTag();
        /** @brief 속도 배율 속성 이름 `MoveSpeedScale` 입니다. */
        static const hashed_string& getSpeedScaleAttribute();
        /** @brief @p owner 의 AbilityComponent 에서 지금 제약을 읽습니다. 컴포넌트가 없으면 제약이 없습니다(걸릴 이펙트가 없다). */
        [[nodiscard]] static MovementConstraint read( const GameObject& owner );
    };
} // namespace sw
