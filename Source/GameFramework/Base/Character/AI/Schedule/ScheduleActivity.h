/**
 * @file ScheduleActivity.h
 * @brief 일정 활동 레지스트리입니다. 데이터의 `activity="WorkAt"` 이 이름으로 활동 종류를 고릅니다.
 * @details 코드는 활동 종류(어디로 가서 무엇을 하는가의 규칙)만 등록하고, 어느 NPC 가 언제 무엇을 하는지는 데이터가 이름으로 고릅니다.
 *          모르는 이름은 읽을 때 오류입니다. 게임은 종류를 더 등록할 수 있습니다. 예를 들어 낚시는 스마트 오브젝트 `FishingSpot` 을 쓰는 활동입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 활동이 가는 곳을 정하는 방식입니다. */
    ENUM()
    enum class ScheduleActivityTarget : uint8
    {
        Place = 0,    ///< 일정 블록의 `place`(장소 id)
        Home,         ///< NPC 의 집 장소
        UsableObject, ///< `objectKind` 의 슬롯을 활동 슬롯 제공자에게 예약한다(없으면 `place`, 그것도 없으면 집)
        Appointment,  ///< 약속(`appointment`)의 장소와 시간
        Wander        ///< `place` 둘레 `radius` 안을 `wanderMinutes` 마다 옮겨 다닌다
    };
} // namespace sw

namespace sw
{
    /** @brief 활동 종류 하나입니다. */
    struct ScheduleActivityDef
    {
        hashed_string          _name{};
        hashed_string          _animation{}; ///< 도착해 활동을 시작할 때 재생할 기본 애니메이션 이름(일정 블록의 `animation` 이 덮어쓴다)
        ScheduleActivityTarget _target{ ScheduleActivityTarget::Place };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleActivityRegistry
     * @brief 활동 이름으로 종류를 찾는 레지스트리입니다. 만들 때 기본 종류를 등록합니다.
     * @details 기본 종류는 `GoTo`(장소로 가서 선다), `WorkAt`(장소에서 일한다), `Attend`(축제나 행사), `StayHome` 과 `Sleep`(집),
     *          `UseObject`(스마트 오브젝트 종류), `Meet`(약속), `Wander`(장소 둘레를 돌아다닌다)이다.
     */
    class SW_GF_API ScheduleActivityRegistry
    {
    public:
        ScheduleActivityRegistry();

        /** @brief 기본 종류만 가진 레지스트리입니다. 게임이 레지스트리를 주지 않은 카탈로그가 씁니다. */
        static const ScheduleActivityRegistry& getBuiltin();

        /** @brief 종류를 더합니다. 이름이 비었거나 이미 있으면 false 입니다. */
        [[nodiscard]] bool                 registerActivity( const ScheduleActivityDef& def );
        const ScheduleActivityDef*         findActivity( const hashed_string& name ) const;
        const vector<ScheduleActivityDef>& getActivities() const { return _registry.getItems(); }

    private:
        void registerBuiltinActivities();

        NameRegistry<ScheduleActivityDef> _registry;
    };
} // namespace sw
