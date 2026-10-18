/**
 * @file RacingGimmicks.h
 * @brief 레이싱 기믹입니다. 부스트 패드와 아이템 상자(시드를 고정한 가중치 뽑기, 다시 나타남)를 다룹니다.
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
    /** @class BoostPadComponent 는 밟기 시작한 것(태그로 거름)에 `GimmickBoostEvent` 를 냅니다. 차량 몸이 이 이벤트를 받습니다. */
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
        PROPERTY( RuntimeState, Category = "Boost", DisplayName = "Boost Count", Tooltip = "Runtime" )
        int32 _boostCount;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemBoxComponent
     * @brief 닿은 것(태그로 거름)에 후보 중 가중치로 뽑은 아이템을 주고(`GimmickItemEvent`), 몸을 숨겼다가 `_respawnDelay` 뒤에 복원합니다.
     * @details 뽑기는 시드(`_seed`)와 지금까지 연 횟수로 정한다. 같은 순서로 열면 같은 아이템이 나오므로 리플레이와 롤백에 쓸 수 있다. 순위 가중치 같은 게임 규칙은
     *          이벤트를 받는 쪽(카트 아이템 규칙)이 아이템을 바꿔서 적용한다.
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
        /** @brief @p target 에게 엽니다. 겹침이 부릅니다. 비어 있어서(숨은 동안) 아이템을 줄 수 없으면 아무 일도 없이 무효 이름을 반환합니다. */
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
        PROPERTY( RuntimeState, Category = "Item Box", DisplayName = "Open Count", Tooltip = "Runtime" )
        int32 _openCount;
        PROPERTY( RuntimeState, Category = "Item Box", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;

        FixedStepTimer _clock;
    };
} // namespace sw
