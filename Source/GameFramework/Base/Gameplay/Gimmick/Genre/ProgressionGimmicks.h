/**
 * @file ProgressionGimmicks.h
 * @brief 진행 기믹입니다. 능력 문(메트로배니아에서 능력 태그를 가진 것이 가까이 오면 열림)과 채집 지점(RPG에서 쓰면 아이템을 주고, 다 쓰면 다시 자람)을 다룹니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AbilityGateComponent
     * @brief 막는 몸(콜라이더와 그래픽)을 가진 문입니다. `_requiredTags`(Ability.DoubleJump, Ability.Bomb)를 모두 가진 것이 `_openRadius` 안에 오면 몸을 끕니다.
     *        `_bStaysOpen` 이 켜져 있으면 한 번 열린 뒤 그대로 유지하고(진행 저장은 PROPERTY `_bOpen`), 아니면 떠날 때 닫힙니다. 2D 는 `_bPlanar` 로 Z 를 뺀 거리를 씁니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Ability Gate", Tooltip = "Opens for objects carrying the required ability tags" )
    class SW_GF_API AbilityGateComponent : public Component
    {
    public:
        REFLECT_BODY();

        AbilityGateComponent();
        virtual ~AbilityGateComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;
        void stepOnce();
        bool isOpen() const { return _bOpen; }

    private:
        PROPERTY( Category = "Gate", DisplayName = "Required Tags", Tooltip = "Ability tags that open the gate" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Gate", DisplayName = "Open Radius", Min = 0.0, Units = m )
        float32 _openRadius;
        PROPERTY( Category = "Gate", DisplayName = "Stays Open" )
        bool _bStaysOpen;
        PROPERTY( Category = "Gate", DisplayName = "Planar", Tooltip = "2D distance (ignore Z)" )
        bool _bPlanar;
        PROPERTY( RuntimeState, Category = "Gate", DisplayName = "Open", Tooltip = "Runtime / progress" )
        bool _bOpen;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GatheringNodeComponent
     * @brief 채집 지점입니다(약초, 광맥, 나무). 같은 오브젝트의 상호작용(Gather)을 끝낸 쪽에 `_item` × `_count` 를 주고(`GimmickItemEvent`), `_uses` 번 쓰면
     *        몸과 상호작용을 끈 뒤 `_respawnDelay` 뒤에 복원합니다. 스텝 수로 세므로 결정적입니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gathering Node", Tooltip = "Gives items when gathered, depletes and respawns" )
    class SW_GF_API GatheringNodeComponent : public Component
    {
    public:
        REFLECT_BODY();

        GatheringNodeComponent();
        virtual ~GatheringNodeComponent() override = default;

        void onTick( float32 deltaTime ) override;
        void stepOnce();
        /** @brief @p who 가 한 번 채집합니다. 상호작용이 부르는 것과 같은 동작입니다. 다 써서 없으면 false 입니다. */
        bool gather( GameObject& who );

        /** @brief 저작 값 `_uses` 만큼 다 썼으면 true 입니다. 남은 횟수를 따로 저장하지 않고 쓴 횟수(런타임)와 비교한다. 그래서 저작 값이 바뀌어도 첫 플레이가 그 값을 따른다. */
        bool  isDepleted() const { return _usesTaken >= _uses; }
        int32 getUsesLeft() const { return _uses - _usesTaken; }

    private:
        PROPERTY( Category = "Gathering", DisplayName = "Item" )
        hashed_string _item;
        PROPERTY( Category = "Gathering", DisplayName = "Count", Min = 1 )
        int32 _count;
        PROPERTY( Category = "Gathering", DisplayName = "Uses", Min = 1 )
        int32 _uses;
        PROPERTY( Category = "Gathering", DisplayName = "Respawn Delay", Min = 0.0, Units = s )
        float32 _respawnDelay;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Uses Taken", Tooltip = "Runtime" )
        int32 _usesTaken;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;

        FixedStepTimer _clock;
    };
} // namespace sw
