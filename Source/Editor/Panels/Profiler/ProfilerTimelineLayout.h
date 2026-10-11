/**
 * @file ProfilerTimelineLayout.h
 * @brief 프로파일러 패널 Timeline 탭의 배치 계산입니다 — 사건을 화면 x 범위 · 스레드 줄 · 겹으로 바꿉니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Profiling/ProfilerTimeline.h"

namespace sw::editor
{
    /** @brief 그릴 사각형 하나입니다. x 는 보이는 폭 안으로 잘린 픽셀이고, 줄 · 겹으로 y 를 정합니다. */
    struct ProfilerTimelineRect
    {
        float32 _x0{ 0.0f };
        float32 _x1{ 0.0f };
        uint32  _threadIndex{ 0 };        ///< `ProfilerTimelineThread` 목록의 번호
        uint32  _eventIndex{ 0 };         ///< 그 스레드 `_listEvent` 의 번호
        uint16  _depth{ 0 };              ///< 겹(바깥 구간 0)
        uint8   _bShowsLabel{ SW_FALSE }; ///< 이름을 쓸 만큼 넓다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief Timeline 탭의 배치 계산입니다. 보이는 시간 범위(나노초)를 폭(픽셀)에 펼칩니다. */
    struct ProfilerTimelineLayout
    {
        /** @brief 이 폭(픽셀) 아래의 사각형은 이름을 쓰지 않습니다. */
        static constexpr float32 kMinLabelWidthPixels = 24.0f;
        /** @brief 사각형의 최소 폭(픽셀)입니다. 짧은 구간도 한 픽셀은 보이게 합니다. */
        static constexpr float32 kMinRectWidthPixels = 1.0f;
        /** @brief 확대할 수 있는 가장 좁은 범위(나노초)입니다. */
        static constexpr uint64 kMinSpanNanos = 1000;

        /** @brief 시각 @p nanos 의 x(픽셀)입니다. 범위 밖이면 0 미만 · @p width 초과가 나옵니다. */
        static float32 computeX( uint64 nanos, uint64 visibleBegin, uint64 visibleEnd, float32 width );

        /**
         * @brief 보이는 범위와 겹치는 사건마다 사각형을 만듭니다. 범위 밖 사건은 빼고, 걸친 사건은 폭 안으로 자릅니다.
         * @param outListLaneCount 스레드마다 겹 수(가장 깊은 사건 + 1, 사건이 없으면 1)입니다.
         */
        static void layoutRects( const vector<ProfilerTimelineThread>& listThread, uint64 visibleBegin, uint64 visibleEnd, float32 width,
                                 vector<ProfilerTimelineRect>& outListRect, vector<uint16>& outListLaneCount );

        /** @brief @p pivotX(픽셀) 자리를 그대로 두고 범위를 @p factor 배로 줄이거나 늘립니다(1 보다 작으면 확대). 범위는 @p limitBegin · @p limitEnd 안에 둡니다. */
        static void zoom( uint64& inoutBegin, uint64& inoutEnd, float32 pivotX, float32 width, float32 factor, uint64 limitBegin, uint64 limitEnd );
        /** @brief 범위를 @p deltaPixels 만큼 옮깁니다(양수 = 화면을 오른쪽으로 끌기 = 이른 시각으로). 범위는 한도 안에 둡니다. */
        static void pan( uint64& inoutBegin, uint64& inoutEnd, float32 deltaPixels, float32 width, uint64 limitBegin, uint64 limitEnd );
    };
} // namespace sw::editor
