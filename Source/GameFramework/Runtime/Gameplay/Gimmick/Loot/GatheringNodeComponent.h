/**
 * @file GatheringNodeComponent.h
 * @brief 채집하면 아이템을 주고, 다 쓰면 다시 자라는 채집 지점입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GatheringNodeComponent
     * @brief 채집 지점입니다(약초, 광맥, 나무). 같은 오브젝트의 상호작용(Gather)을 끝낸 쪽이 받을 가방(`IItemReceiver`)에 `_item` × `_count` 를 넣고(`GimmickUtil::giveItem`), `_uses` 번 쓰면
     *        몸과 상호작용을 끈 뒤 `_respawnDelay` 뒤에 복원합니다. 스텝 수로 세므로 결정적입니다.
     * @details 나무는 `_fallDuration` 동안 마지막에 벤 쪽의 반대로 밑동을 축으로 쓰러진 뒤 사라지고, 복원할 때 다시 선다(오브젝트 원점 = 밑동). 쓰러지는 각은 시간의 제곱이다(무너지는 것처럼 빨라진다).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Gathering Node", Tooltip = "Gives items when gathered, depletes and respawns" )
    class SW_GF_API GatheringNodeComponent : public Component
    {
    public:
        REFLECT_BODY();

        GatheringNodeComponent();
        virtual ~GatheringNodeComponent() override = default;

        /** @brief 같은 오브젝트의 상호작용 완료를 듣습니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;
        void stepOnce();
        /** @brief @p who 가 한 번 채집합니다. 상호작용이 부르는 것과 같은 동작입니다. 다 써서 없거나 받을 가방이 없거나 차면 false 입니다(횟수를 쓰지 않는다). 게임 스레드(틱 밖)에서 부릅니다. */
        bool gather( const GameObject& who );

        /** @brief 저작 값 `_uses` 만큼 다 썼으면 true 입니다. 남은 횟수를 따로 저장하지 않고 쓴 횟수(런타임)와 비교한다. 그래서 저작 값이 바뀌어도 첫 플레이가 그 값을 따른다. */
        bool  isDepleted() const { return _usesTaken >= _uses; }
        int32 getUsesLeft() const { return _uses - _usesTaken; }
        /** @brief 쓰러지는 중이면 true 입니다. */
        bool isFalling() const { return _fallStepsLeft > 0; }
        /** @brief 쓰러지는 시간(초, 0 = 바로 사라진다)을 정합니다. 코드로 만든 지점 · 테스트가 씁니다. */
        void setFallDuration( float32 fallDuration ) { _fallDuration = fallDuration; }
        /** @brief 주는 아이템과 횟수를 정합니다. 코드로 만든 지점 · 테스트가 씁니다. */
        void configure( const hashed_string& item, int32 count, int32 uses );

    private:
        /** @brief 상호작용(Gather · Chop)을 끝낸 쪽에 줍니다. */
        void handleUsed( const GameObject& user );
        /** @brief 다 썼다 — 쓰러지기를 시작하거나(`_fallDuration`) 바로 몸을 끕니다. @p who 의 반대쪽으로 쓰러집니다. */
        void handleDepleted( const GameObject& who );
        /** @brief 쓰러진 정도(0 = 서 있음, 1 = 누움)를 트랜스폼에 씁니다. */
        void applyFall( float32 amount );

    private:
        Subscription _usedSubscription; ///< 상호작용 완료 구독(`handleUsed`)
        PROPERTY( Required, Category = "Gathering", DisplayName = "Item" )
        hashed_string _item;
        PROPERTY( Category = "Gathering", DisplayName = "Count", Min = 1 )
        int32 _count;
        PROPERTY( Category = "Gathering", DisplayName = "Uses", Min = 1 )
        int32 _uses;
        PROPERTY( Category = "Gathering", DisplayName = "Respawn Delay", Min = 0.0, Units = s )
        float32 _respawnDelay;
        PROPERTY( Category = "Gathering", DisplayName = "Fall Duration", Min = 0.0, Tooltip = "Topple away from the last gatherer over this time before it disappears (a felled tree); 0 = disappear at once", Units = s )
        float32 _fallDuration;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Uses Taken", Tooltip = "Runtime" )
        int32 _usesTaken;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Respawn Steps Left", Tooltip = "Runtime" )
        int32 _respawnStepsLeft;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Fall Steps Left", Tooltip = "Runtime" )
        int32 _fallStepsLeft;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Fall Yaw", Tooltip = "Direction it falls toward (runtime)", Units = rad )
        float32 _fallYaw;
        PROPERTY( RuntimeState, Category = "Gathering", DisplayName = "Standing Rotation", Tooltip = "Local rotation to stand up with again (runtime)", Units = rad )
        float3 _standingRotation;

        FixedStepTimer _clock;
    };
} // namespace sw
