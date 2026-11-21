/**
 * @file GatheringNodeComponent.h
 * @brief 채집하면 아이템을 주고, 다 쓰면 다시 자라는 채집 지점입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

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

        void onTick( TickContext& context ) override;
        void stepOnce();
        /** @brief @p who 가 한 번 채집합니다. 상호작용이 부르는 것과 같은 동작입니다. 다 써서 없으면 false 입니다. */
        bool gather( const GameObject& who );

        /** @brief 저작 값 `_uses` 만큼 다 썼으면 true 입니다. 남은 횟수를 따로 저장하지 않고 쓴 횟수(런타임)와 비교한다. 그래서 저작 값이 바뀌어도 첫 플레이가 그 값을 따른다. */
        bool  isDepleted() const { return _usesTaken >= _uses; }
        int32 getUsesLeft() const { return _uses - _usesTaken; }

    private:
        PROPERTY( Required, Category = "Gathering", DisplayName = "Item" )
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
