/**
 * @file BehaviorTreeNodeFields.h
 * @brief 행동 트리 데이터 파일(`*.bt.xml`)의 노드 칸입니다 — 노드 하나의 모든 칸(`BehaviorTreeNodeData`)과, 노드 종류마다 파일에 적을 수 있는 칸의 스키마(`Behavior*Fields`).
 * @details 언리얼 BT 의 노드 클래스가 저마다 UPROPERTY 를 가지는 것과 같은 모양이다. 종류마다 받는 속성 · 필수 · 범위 · 기본값은 스키마 구조체의 `PROPERTY` 가 정본이고,
 *          읽기 · 쓰기는 카탈로그와 같은 `GameDataXML::readFields` · `writeFields` 다(속성 이름은 칸 이름에서 앞의 `_` 와 불리언 `b` 를 뗀 것).
 *          런타임 로더(`BehaviorTreeAsset`)와 에디터 모델(`BehaviorTreeEditModel`)이 같은 읽기 · 쓰기(`BehaviorTreeXML`)를 쓴다 — 예전에는 둘이 같은 형식을 따로 손으로 읽었다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"

namespace sw
{
    /** @brief 노드 하나의 모든 칸입니다. 종류에 맞는 칸만 뜻이 있다(나머지는 기본값이고 파일에 쓰지 않는다). 종류마다의 스키마와 이름이 같은 칸끼리 옮긴다(`GameDataXML::copyFields`). */
    REFLECT()
    struct SW_GF_API BehaviorTreeNodeData
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Display name (Selector, Sequence, Action, Condition); empty shows the node type" )
        string _name{};
        PROPERTY( Tooltip = "Task function name of an Action or Condition (BehaviorTaskRegistry)" )
        string _task{};
        PROPERTY( Tooltip = "Blackboard key of a BlackboardCondition or MoveTo" )
        string _key{};
        PROPERTY( Tooltip = "Comparison of a BlackboardCondition" )
        BlackboardCompare _compare{ BlackboardCompare::IsSet };
        PROPERTY( Tooltip = "What a BlackboardCondition stops when its result changes" )
        BehaviorAbortMode _abort{ BehaviorAbortMode::None };
        PROPERTY( Tooltip = "Comparison value of a BlackboardCondition" )
        float32 _value{ 0.0f };
        PROPERTY( Tooltip = "Seconds of a Cooldown, TimeLimit or Wait", Units = s )
        float32 _seconds{ 0.0f };
        PROPERTY( Tooltip = "Distance the MoveTo goal must move before a new path is asked", Units = m )
        float32 _repathDistance{ 0.5f };
        PROPERTY( Tooltip = "Runs of a Repeat; 0 repeats until the child fails" )
        int32 _count{ 0 };
        PROPERTY( Tooltip = "A reactive Selector re-checks higher branches every tick" )
        bool _bReactive{ false };
        PROPERTY( Tooltip = "Node type (the element name in the file)" )
        BehaviorNodeType _type{ BehaviorNodeType::Sequence };
    };

    /** @brief `Selector` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorSelectorFields
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY()
        bool _bReactive{ false };
    };

    /** @brief `Sequence` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorSequenceFields
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
    };

    /** @brief `Repeat` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorRepeatFields
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0 )
        int32 _count{ 0 };
    };

    /** @brief `seconds` 하나로 정하는 종류(`Cooldown` · `TimeLimit` · `Wait`)입니다. */
    REFLECT()
    struct SW_GF_API BehaviorTimedFields
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Units = s )
        float32 _seconds{ 0.0f };
    };

    /** @brief `BlackboardCondition` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorBlackboardConditionFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _key{};
        PROPERTY()
        BlackboardCompare _compare{ BlackboardCompare::IsSet };
        PROPERTY()
        float32 _value{ 0.0f };
        PROPERTY()
        BehaviorAbortMode _abort{ BehaviorAbortMode::None };
    };

    /** @brief 작업 함수를 부르는 종류(`Action` · `Condition`)입니다. */
    REFLECT()
    struct SW_GF_API BehaviorTaskFields
    {
        REFLECT_BODY();

        PROPERTY()
        string _name{};
        PROPERTY( Required )
        string _task{};
    };

    /** @brief `MoveTo` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorMoveToFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _key{};
        PROPERTY( Min = 0.0, Units = m )
        float32 _repathDistance{ 0.5f };
    };
} // namespace sw
