/**
 * @file ScheduleSaveState.h
 * @brief 일정 시스템의 저장 상태 — 게임의 `SaveGame` 에 `PROPERTY()` 로 넣는 리플렉션 구조체입니다.
 * @details 계획은 저장하지 않습니다. 계획은 (그날 · 계획을 세운 시각 · 그때의 자리 · 깨진 약속 · 예약)의 함수라서 그것만 담아 다시 세우면
 *          같은 계획이 나옵니다 — 저장 파일이 작고, 데이터를 고친 뒤 불러와도 새 일정으로 이어집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 쌓인 끼어들기 하나입니다. 시각은 0 일 0:00 부터 센 분입니다. */
    REFLECT()
    struct SW_GF_API ScheduleInterruptionSaveState
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        int32 _startMinute{ 0 };
        PROPERTY()
        int32 _expireMinute{ -1 }; ///< −1 = 게임이 끝낼 때까지
    };
} // namespace sw

namespace sw
{
    /** @brief NPC 하나 — 오늘 계획을 세운 시각 · 자리, 끼어들기, 태그입니다. */
    REFLECT()
    struct SW_GF_API ScheduleNpcSaveState
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        hashed_string _originArea{};
        PROPERTY()
        hashed_string _heldArea{}; ///< 끼어든 동안 머문 지역
        PROPERTY()
        vector<ScheduleInterruptionSaveState> _listInterruption{};
        PROPERTY()
        vector<string> _listTag{};
        PROPERTY()
        float3 _originPosition{};
        PROPERTY()
        float3 _heldPosition{}; ///< 끼어든 동안 머문 자리(게임이 알린 자리)
        PROPERTY()
        int32 _originMinute{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 기본 활동 자리 제공자(`ScheduleSpotLocator`)의 예약 하나입니다. */
    REFLECT()
    struct SW_GF_API ScheduleReservationSaveState
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _spot{};
        PROPERTY()
        hashed_string _npc{};
        PROPERTY()
        hashed_string _kind{};
        PROPERTY()
        uint32 _id{ 0 };
        PROPERTY()
        int32 _day{ 0 };
        PROPERTY()
        int32 _startMinute{ 0 };
        PROPERTY()
        int32 _endMinute{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 일정 시스템 전체입니다. */
    REFLECT()
    struct SW_GF_API ScheduleSaveState
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _weather{};
        PROPERTY()
        vector<ScheduleNpcSaveState> _listNpc{};
        PROPERTY()
        vector<string> _listBrokenAppointment{}; ///< 오늘 깨진 약속
        PROPERTY()
        vector<ScheduleReservationSaveState> _listReservation{};
        PROPERTY()
        int32 _minute{ 0 }; ///< 0 일 0:00 부터 센 분
        PROPERTY()
        uint32 _seed{ 0 };
    };
} // namespace sw
