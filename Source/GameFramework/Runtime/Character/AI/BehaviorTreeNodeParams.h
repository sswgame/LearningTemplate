/**
 * @file BehaviorTreeNodeParams.h
 * @brief 행동 트리 데이터 파일(`*.bt.xml`)의 노드 칸입니다 — 노드 종류마다 파일에 적을 수 있는 칸의 스키마(`Behavior*Fields`)와, 읽은 노드 하나(`BehaviorTreeNodeData`).
 * @details 언리얼 BT 의 노드 클래스가 저마다 UPROPERTY 를 가지는 것과 같은 모양이다. 종류마다 받는 속성 · 필수 · 범위 · 기본값은 스키마 구조체의 `PROPERTY` 가 정본이고,
 *          읽기 · 쓰기는 카탈로그와 같은 `XMLFields::readProperties` · `writeProperties` 다. 모든 종류의 칸을 모은 고정 구조체를 두지 않는다 — 노드는 자기 종류의 스키마 값 하나와, 작업이면 그 동작의 칸을 든다.
 *          편집기는 이 값을 리플렉션 그리드로 그리므로 칸을 더해도 패널을 고치지 않는다. 런타임 로더(`BehaviorTreeAsset`)와 에디터 모델(`BehaviorTreeEditModel`)이 같은 읽기 · 쓰기(`BehaviorTreeXML`)를 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/InstancedStruct.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"

namespace sw
{
    class BehaviorTreeAsset;

    /** @brief `Selector` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorSelectorParams
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Re-check higher priority children every tick" )
        bool _bReactive{ false };
    };

    /** @brief `Repeat` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorRepeatParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0, Tooltip = "Runs of the child; 0 repeats until it fails" )
        int32 _count{ 0 };
    };

    /** @brief `seconds` 하나로 정하는 종류(`Cooldown` · `TimeLimit` · `Wait`)입니다. */
    REFLECT()
    struct SW_GF_API BehaviorTimedParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Units = s, Tooltip = "Seconds of a Cooldown, TimeLimit or Wait" )
        float32 _seconds{ 1.0f };
    };

    /** @brief `BlackboardCondition`(조건 데코레이터)의 칸입니다. 식은 공통 어휘입니다(`hasBlackboard(Threat)`, `blackboard(Hp) < 5 && flag(alarm)`). */
    REFLECT()
    struct SW_GF_API BehaviorConditionDecoratorParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Condition expression that guards the child" )
        string _test{};
        PROPERTY( Tooltip = "What stops when the result changes" )
        BehaviorAbortMode _abort{ BehaviorAbortMode::None };
    };

    /** @brief `Condition`(조건 잎)의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorConditionParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Condition expression" )
        string _test{};
    };

    /** @brief `Action` 의 칸입니다. 나머지 속성은 그 동작의 칸입니다(`ActionRegistry`). */
    REFLECT()
    struct SW_GF_API BehaviorActionParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Registered action name" )
        string _task{};
    };

    /** @brief `MoveTo` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BehaviorMoveToParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of the destination" )
        string _key{};
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Distance the goal must move before a new path is asked" )
        float32 _repathDistance{ 0.5f };
    };

    /** @brief `SubTree` 의 칸입니다. 둘 중 정확히 하나를 적습니다. */
    REFLECT()
    struct SW_GF_API BehaviorSubTreeParams
    {
        REFLECT_BODY();

        /** @brief .cpp 에 두어 이 헤더가 `BehaviorTreeAsset` 을 앞선 선언으로만 안다(그 헤더가 이 칸들을 쓴다). */
        BehaviorSubTreeParams();

        PROPERTY( Tooltip = "Tree file expanded here when the tree is read" )
        AssetRef<BehaviorTreeAsset> _tree{};
        PROPERTY( Tooltip = "Slot name the user of the tree fills (BehaviorTreeRunner::setSlotTree)" )
        string _slot{};
    };

    /** @brief `blackboard.clear` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BlackboardClearParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key" )
        hashed_string _key{};
    };

    /** @brief `blackboard.set` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API BlackboardSetParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key" )
        hashed_string _key{};
        PROPERTY( Tooltip = "Number to store" )
        float32 _value{ 1.0f };
    };

    /** @brief `blackboard.add` 의 칸입니다. 수치 값에 더합니다(없는 키는 실패 — 처음 값은 감각이나 `blackboard.set` 이 둔다). */
    REFLECT()
    struct SW_GF_API BlackboardAddParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key of a number" )
        hashed_string _key{};
        PROPERTY( Required, Tooltip = "Amount to add (negative subtracts)" )
        float32 _value{ 0.0f };
    };

    /** @brief `blackboard.copy` 의 칸입니다. 값을 종류째(위치 · 수 · 참거짓 · 오브젝트) 옮겨 적습니다. */
    REFLECT()
    struct SW_GF_API BlackboardCopyParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Blackboard key to read" )
        hashed_string _from{};
        PROPERTY( Required, Tooltip = "Blackboard key to write" )
        hashed_string _to{};
    };
} // namespace sw

namespace sw
{
    /** @brief 등록되지 않은 동작의 칸입니다. 편집기가 아직 안 만든 동작의 트리를 열어도 값이 사라지지 않게 글로 듭니다. */
    struct BehaviorRawParam
    {
        string _name{};
        string _value{};
    };

    /** @brief 노드 하나의 칸입니다. `_params` 는 종류의 스키마 값이고, `_actionParams` 는 작업의 동작 칸입니다. */
    struct SW_GF_API BehaviorTreeNodeData
    {
        string                   _inheritID{}; ///< 파일의 `id`. `_base` 트리에서 이 노드를 짚는 열쇠(`InheritKey`)
        string                   _name{};      ///< 표시 이름. 비면 종류 이름이다
        InstancedStruct          _params{};
        InstancedStruct          _actionParams{};
        vector<BehaviorRawParam> _listRawParam{};
        BehaviorNodeKind         _type{ BehaviorNodeKind::Sequence };
    };
} // namespace sw
