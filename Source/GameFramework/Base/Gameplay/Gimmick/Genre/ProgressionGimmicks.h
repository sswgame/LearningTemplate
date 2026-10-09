/**
 * @file ProgressionGimmicks.h
 * @brief 진행 기믹 — 능력 문(메트로배니아: 능력 태그를 가진 것이 가까이 오면 열림), 채집 지점(RPG: 쓰면 아이템, 다 쓰면 다시 자람)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class AbilityGateComponent
     * @brief 막는 몸(콜라이더 · 그림)을 가진 문 — `_requiredTags`(Ability.DoubleJump · Ability.Bomb)를 모두 가진 것이 `_openRadius` 안에 오면 몸을 끕니다.
     *        `_bStaysOpen` 이면 한 번 열린 뒤 그대로(진행 저장 — PROPERTY `_bOpen`), 아니면 떠나면 닫힙니다. 2D 는 `_bPlanar` 로 Z 를 뺀 거리입니다.
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
        PROPERTY( Category = "Gate", DisplayName = "Open", Tooltip = "Runtime / progress" )
        bool _bOpen;

        FixedStepTimer _clock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class GatheringNodeComponent
     * @brief 채집 지점(약초 · 광맥 · 나무) — 같은 오브젝트의 상호작용(Gather)을 끝낸 이에게 `_item` × `_count` 를 주고(`GimmickItemEvent`), `_uses` 번 쓰면
     *        몸과 상호작용을 끄고 `_respawnDelay` 뒤에 되살립니다. 걸음 수로 세어 결정적입니다.
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
        /** @brief @p who 가 한 번 거둡니다(상호작용이 부르는 것과 같다). 다 써서 없으면 false 입니다. */
        bool gather( GameObject& who );

        bool  isDepleted() const { return _usesLeft <= 0; }
        int32 getUsesLeft() const { return _usesLeft; }

    private:
        PROPERTY( Category = "Gathering", DisplayName = "Item" )
        hashed_string _item;
        PROPERTY( Category = "Gathering", DisplayName = "Count", Min = 1 )
        int32 _count;
        PROPERTY( Category = "Gathering", DisplayName = "Uses", Min = 1 )
        int32 _uses;
        PROPERTY( Category = "Gathering", DisplayName = "Respawn Delay", Min = 0.0, Units = s )
        float32 _respawnDelay;
        PROPERTY( Category = "Gathering", DisplayName = "Uses Left", Tooltip = "Runtime" )
        int32 _usesLeft;
        PROPERTY( Category = "Gathering", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;

        FixedStepTimer _clock;
    };
} // namespace sw
