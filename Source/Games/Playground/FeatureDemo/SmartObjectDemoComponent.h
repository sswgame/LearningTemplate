/**
 * @file SmartObjectDemoComponent.h
 * @brief 스마트 오브젝트의 시연입니다 — NPC 의 하루 일정(`ScheduleSystem`)이 씬의 벤치(`SmartObjectComponent`) 슬롯을 시간 창으로 예약하고, 활동을 시작할 때 점유하고 끝낼 때 비웁니다.
 * @details 플레이어는 같은 슬롯을 같은 함수로 차지한다(개발 명령 `smartobject.claim`). NPC 의 몸은 일정 NPC id 와 이름이 같은 씬 오브젝트이고, 시각은 개발 명령 `schedule.advance` 로만 흐른다(결정적).
 *          시나리오: `game/playground/automation/smartobject.scenario.xml`(무대 maps/smartobject).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleCatalog.h"
#include "GameFramework/Runtime/Character/AI/Schedule/ScheduleSystem.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Interaction/SmartObjectScheduleLocator.h"

namespace sw
{
    /**
     * @class SmartObjectDemoComponent
     * @brief 일정 시스템과 스마트 오브젝트 예약 어댑터(`SmartObjectScheduleLocator`)를 들고, 일정 이벤트를 슬롯 점유 · 비우기로 잇습니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Smart Object Demo", Tooltip = "Daily schedule whose UseObject blocks reserve and claim smart object slots of the scene" )
    class SmartObjectDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        SmartObjectDemoComponent();
        virtual ~SmartObjectDemoComponent() override;

        /** @brief 첫날 @p minute 분까지 일정을 흘리고, 그 사이의 활동 시작 · 끝을 점유 · 비우기로 잇고, 몸을 그 시각의 자리에 둡니다. 뒤로는 가지 않습니다. */
        void advanceToMinute( int32 minute );

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 쌓인 일정 이벤트를 점유 · 비우기로 잇습니다. */
        void processEvents();
        /** @brief NPC @p npcIndex 의 지금 칸이 예약한 슬롯을 점유하고 결과를 셉니다. 예약이 없는 활동이면 아무 일도 하지 않습니다. */
        void claimSlot( int32 npcIndex );
        /** @brief NPC @p npcIndex 의 지금 칸이 든 예약 번호입니다. 슬롯을 쓰지 않는 활동이면 0 입니다. */
        [[nodiscard]] uint32 findReservation( int32 npcIndex ) const;
        /** @brief NPC @p npcIndex 가 점유한 슬롯을 비웁니다. */
        void releaseSlot( int32 npcIndex );
        /** @brief 일정 NPC 의 몸(같은 이름의 씬 오브젝트)입니다. */
        GameObject* findBody( int32 npcIndex );

    private:
        PROPERTY( Category = "Data", DisplayName = "Schedules", Tooltip = "Schedule catalog (*.schedules.xml)" )
        AssetRef<ScheduleCatalog> _scheduleDataPath;
        PROPERTY( Category = "Data", DisplayName = "Start Minute", Min = 0, Max = 1439, Tooltip = "Minute of day 0 the schedule starts at" )
        int32 _startMinute;

        PROPERTY( RuntimeState, Min = 0 )
        int32 _minute; ///< 흘린 시각(첫날의 분)
        PROPERTY( RuntimeState, Min = 0 )
        int32 _claimCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _refusedCount; ///< 예약했지만 다른 쪽(플레이어)이 차지해 앉지 못한 수
        PROPERTY( RuntimeState, Min = 0 )
        int32 _releaseCount;

        SharedAssetRef<ScheduleCatalog> _pCatalog; ///< 일정이 빌려 쓴다
        SmartObjectScheduleLocator      _locator;  ///< 일정이 빌려 쓴다(`_schedule` 앞에 선언 — 뒤에 사라진다)
        ScheduleSystem                  _schedule;
        vector<uint32>                  _listClaim; ///< NPC 마다 점유 중인 예약 번호(0 은 없음)
    };
} // namespace sw
