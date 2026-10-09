/**
 * @file ScheduleActivity.h
 * @brief 일정 활동 등록부 — 데이터의 `activity="WorkAt"` 이 고르는 이름 붙은 활동 종류입니다.
 * @details 코드는 활동 종류(어디로 가서 무엇을 하는가의 규칙)만 등록하고, 어느 NPC 가 언제 무엇을 하는지는 데이터가 이름으로 고릅니다.
 *          모르는 이름은 읽을 때 오류입니다. 게임은 종류를 더 등록할 수 있습니다(낚시 = 스마트 오브젝트 `FishingSpot` 을 쓰는 활동).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 활동이 가는 곳을 정하는 방식입니다. */
    enum class ScheduleActivityTarget : uint8
    {
        Place = 0,   ///< 일정 칸의 `place`(장소 id)
        Home,        ///< NPC 의 집 장소
        SmartObject, ///< `objectKind` 의 자리를 활동 자리 제공자에게 예약한다(없으면 `place` · 집)
        Appointment, ///< 약속(`appointment`)의 장소 · 시간
        Wander       ///< `place` 둘레 `radius` 안을 `wanderMinutes` 마다 옮겨 다닌다
    };

    SW_GF_API const utf8* toString( ScheduleActivityTarget target );
} // namespace sw

namespace sw
{
    /** @brief 활동 종류 하나입니다. */
    struct ScheduleActivityDef
    {
        hashed_string          _name{};
        hashed_string          _animation{}; ///< 도착해 활동을 시작할 때 재생할 기본 애니메이션 이름(일정 칸의 `animation` 이 덮는다)
        ScheduleActivityTarget _target{ ScheduleActivityTarget::Place };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScheduleActivityRegistry
     * @brief 활동 이름 → 종류입니다. 만들 때 기본 종류를 등록합니다.
     * @details 기본 — `GoTo`(장소로 가서 선다) · `WorkAt`(장소에서 일한다) · `Attend`(축제 · 행사 자리) · `StayHome` · `Sleep`(집) ·
     *          `UseObject`(스마트 오브젝트 종류) · `Meet`(약속) · `Wander`(장소 둘레를 돌아다닌다).
     */
    class SW_GF_API ScheduleActivityRegistry
    {
    public:
        ScheduleActivityRegistry();

        /** @brief 기본 종류만 든 등록부입니다(게임이 등록부를 주지 않은 카탈로그가 쓴다). */
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
