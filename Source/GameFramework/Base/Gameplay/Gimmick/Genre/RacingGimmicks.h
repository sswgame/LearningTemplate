/**
 * @file RacingGimmicks.h
 * @brief 레이싱 기믹 — 부스트 패드 · 아이템 상자(씨앗 고정 가중치 뽑기 · 다시 나타남)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 아이템 상자의 후보 하나입니다. */
    REFLECT()
    struct SW_GF_API GimmickItemChoice
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _item{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @class BoostPadComponent — 밟기 시작한 것(태그 거르기)에 `GimmickBoostEvent` 를 냅니다(차량 몸이 받는다). */
    REFLECT( Category = "Gimmick", DisplayName = "Boost Pad", Tooltip = "Sends a boost event to whatever starts overlapping" )
    class SW_GF_API BoostPadComponent : public Component
    {
    public:
        REFLECT_BODY();

        BoostPadComponent();
        virtual ~BoostPadComponent() override = default;

        void  onOverlapBegin( const OverlapInfo& overlap ) override;
        int32 getBoostCount() const { return _boostCount; }

    private:
        PROPERTY( Category = "Boost", DisplayName = "Duration", Min = 0.0, Units = s )
        float32 _duration;
        PROPERTY( Category = "Boost", DisplayName = "Strength", Min = 0.0, Tooltip = "Speed multiplier or added speed, as the vehicle reads it" )
        float32 _strength;
        PROPERTY( Category = "Boost", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Boost", DisplayName = "Boost Count", Tooltip = "Runtime" )
        int32 _boostCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemBoxComponent
     * @brief 닿은 것(태그 거르기)에 후보 중 가중치로 뽑은 아이템을 주고(`GimmickItemEvent`) 몸을 숨겼다가 `_respawnDelay` 뒤에 되살립니다.
     * @details 뽑기는 씨앗(`_seed`)과 지금까지 연 횟수로 정합니다 — 같은 순서로 열면 같은 아이템입니다(리플레이 · 롤백). 순위 가중치 같은 게임 규칙은
     *          이벤트를 받는 쪽(카트 아이템 규칙)이 아이템을 바꿔 적용합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Item Box", Tooltip = "Gives a seeded weighted item on touch, hides and respawns" )
    class SW_GF_API ItemBoxComponent : public Component
    {
    public:
        REFLECT_BODY();

        ItemBoxComponent();
        virtual ~ItemBoxComponent() override = default;

        void onOverlapBegin( const OverlapInfo& overlap ) override;
        void onTick( float32 deltaTime ) override;
        void stepOnce();
        /** @brief @p target 에게 엽니다(겹침이 부른다). 비어 있으면(숨은 동안) 아무 일 없이 무효 이름입니다. */
        hashed_string open( GameObject& target );
        void          setChoices( const vector<GimmickItemChoice>& listChoice ) { _listChoice = listChoice; }

        bool  isAvailable() const { return _respawnStepsLeft <= 0; }
        int32 getOpenCount() const { return _openCount; }

    private:
        PROPERTY( Category = "Item Box", DisplayName = "Choices" )
        vector<GimmickItemChoice> _listChoice;
        PROPERTY( Category = "Item Box", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( Category = "Item Box", DisplayName = "Respawn Delay", Min = 0.0, Units = s )
        float32 _respawnDelay;
        PROPERTY( Category = "Item Box", DisplayName = "Seed" )
        uint32 _seed;
        PROPERTY( Category = "Item Box", DisplayName = "Open Count", Tooltip = "Runtime" )
        int32 _openCount;
        PROPERTY( Category = "Item Box", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;

        FixedStepTimer _clock;
    };
} // namespace sw
