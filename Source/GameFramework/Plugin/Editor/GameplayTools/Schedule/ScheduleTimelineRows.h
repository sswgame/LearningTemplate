/**
 * @file ScheduleTimelineRows.h
 * @brief NPC 하루 계획(`ScheduleSystem::getPlan`)을 일정 디버그 창의 표 행으로 바꾸는 화면 없는 변환입니다.
 * @details ImGui 를 모르므로 EngineTest 가 직접 컴파일해 테스트합니다(`TestScheduleTimelineRows.cpp`). 시각은 그날의 분입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct ScheduleSegment;
} // namespace sw

namespace sw::editor
{
    /** @brief 타임라인 표의 한 행입니다. */
    struct ScheduleTimelineRow
    {
        string _time{};              ///< "08:00-12:00"
        string _travel{};            ///< "07:40 -> 07:55"(이동이 없으면 빈 글)
        string _place{};             ///< 지역 이름, 없으면 좌표
        string _activity{};          ///< 활동 이름
        string _source{};            ///< "Routine 'work'"
        int32  _segmentIndex;        ///< 일정 구간 번호(줄을 만들 때 채운다)
        bool   _bActive{ SW_FALSE }; ///< 미리 보는 시각을 덮는 칸 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool   _bPast{ false };      ///< 계획을 세운 시각보다 앞에 끝난 칸 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ScheduleTimelineRows
     * @brief 계획 → 표 행 변환입니다.
     */
    struct ScheduleTimelineRows
    {
        /** @brief 그날의 분을 "HH:MM" 으로 씁니다. 하루를 넘는 값은 하루로 접습니다. */
        static string formatClock( int32 minuteOfDay );
        /** @brief @p minuteOfDay 를 덮는 칸의 번호입니다(시작 포함, 끝 제외). 없으면 비어 있습니다. */
        static optional<int32> findSegmentAt( const vector<ScheduleSegment>& listSegment, int32 minuteOfDay );
        /** @brief @p listSegment 를 행으로 바꿉니다. @p previewMinute 를 덮는 칸이 `_bActive` 입니다. */
        static void populate( const vector<ScheduleSegment>& listSegment, int32 previewMinute, vector<ScheduleTimelineRow>& outListRow );
    };
} // namespace sw::editor
