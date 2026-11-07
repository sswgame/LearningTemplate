/**
 * @file AIControllerActions.h
 * @brief AI 조종자의 공통 동작 어휘(`ai.*`)와 조건(`aiHasDestination`)의 칸입니다. 행동 트리 데이터(`*.bt.xml`)의 잎이 이 이름으로 목표를 정합니다.
 * @details 언리얼 BT 의 기본 작업(Move To · Rotate To Face · Set Blackboard Value)과 같은 자리다. 위치 · 대상은 블랙보드 키로 받는다 — 감각(`AIControllerComponent::updateBlackboard`)이 적은 값이다.
 *          모두 그 자리에서 끝나는 동작이다(`Succeeded` · `Failed`). 매 판단 루트부터 다시 고르는 트리(반응형 판단)가 상태 기계의 `switch` 와 같은 결과를 낸다.
 *          조종자 칸(`AIControllerSlot`)이 없는 실행기(지휘관 AI · 스케줄 · 대화)에서는 실패한다.
 *          | 이름 | 칸 | 하는 일 |
 *          | --- | --- | --- |
 *          | `ai.moveTo` | `key`, `repathDistance` | 블랙보드 위치로 간다. 걸린 목적지에서 `repathDistance` 미터 안이면 다시 걸지 않는다 |
 *          | `ai.moveAway` | `from`, `away`, `distance` | `from` 에서 `away` 의 반대쪽(수평)으로 `distance` 미터 떨어진 곳으로 간다 |
 *          | `ai.arrive` | `key`, `self`, `tolerance` | `self` 가 `key` 에서 수평 `tolerance` 안이면 멈추고 성공, 아니면 그리로 가며 실패 |
 *          | `ai.stop` | — | 멈춘다 |
 *          | `ai.focus` | `key`, `height` | 블랙보드 위치(위로 `height`)를 바라본다 |
 *          | `ai.clearFocus` | — | 초점을 지운다(움직이는 쪽을 본다) |
 *          | `ai.press` | `button` | 폰 버튼을 한 번 누른다 |
 *          | `ai.hold` | `button`, `held` | 폰 버튼을 누르고 있거나 뗀다 |
 *          | `ai.state` | `state` | 판단 상태 이름(감시 값 `AI.State`)을 정한다 |
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief `ai.moveTo` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIMoveToParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the destination" )
        hashed_string _key{};
        PROPERTY( Min = 0.0, Units = m, Tooltip = "The destination is not asked again while the goal stays this close to it; 0 asks whenever it moves" )
        float32 _repathDistance{ 0.0f };
    };

    /** @brief `ai.moveAway` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIMoveAwayParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the position to step away from (usually the controller's own)" )
        hashed_string _from{};
        PROPERTY( Required, Tooltip = "Blackboard key of the position to back away from" )
        hashed_string _away{};
        PROPERTY( Required, Min = 0.0, Units = m, Tooltip = "How far the destination lies from 'from'" )
        float32 _distance{ 0.0f };
    };

    /** @brief `ai.arrive` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIArriveParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the spot to stand on" )
        hashed_string _key{};
        PROPERTY( Required, Tooltip = "Blackboard key of the walker's position" )
        hashed_string _self{};
        PROPERTY( Required, Min = 0.0, Units = m, Tooltip = "Counts as standing on the spot inside this horizontal distance" )
        float32 _tolerance{ 0.0f };
    };

    /** @brief `ai.focus` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIFocusParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the position to look at" )
        hashed_string _key{};
        PROPERTY( Units = m, Tooltip = "Looks this far above the position" )
        float32 _height{ 0.0f };
    };

    /** @brief `ai.press` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIPressParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Pawn button name" )
        hashed_string _button{};
    };

    /** @brief `ai.hold` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIHoldParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Pawn button name" )
        hashed_string _button{};
        PROPERTY( Tooltip = "Hold (true) or release (false)" )
        bool _bHeld{ true };
    };

    /** @brief `ai.state` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AIStateParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Decision state name (watch value AI.State)" )
        hashed_string _state{};
    };
} // namespace sw
