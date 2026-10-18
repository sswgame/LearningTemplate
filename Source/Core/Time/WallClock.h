/**
 * @file WallClock.h
 * @brief UTC 벽시계 — 유닉스 시각(1970-01-01 00:00 UTC 부터) 밀리초입니다. 서버의 기간 · 만료 · 기록 시각처럼 기준점(epoch)이 있어야 하는 값에 씁니다.
 * @details 시스템 시계는 거꾸로 갈 수 있다(NTP 보정) — 경과 시간 · 시한은 `MonotonicClock` 으로 측정한다. 서비스 코드는 이 값을 직접 읽지 않고
 *          `nowMs` 매개변수로 받는다(테스트가 가짜 시각을 넣는다). `std::chrono::system_clock` 을 읽는 곳은 이 파일 하나다(`CheckClockReads`).
 *
 *          지역 시각 달력(로그 줄의 날짜, 스크린샷 파일 이름)도 여기 `toLocalCalendar` 하나가 바꾼다. C 라이브러리의 `localtime` 은 프로세스에
 *          하나뿐인 정적 버퍼에 써서 여러 스레드가 부르면 데이터 레이스다 — 호출하는 쪽마다 플랫폼 분기를 적으면 한쪽이 그것을 고른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct LocalCalendarTime
     * @brief 이 기계의 시간대로 본 달력 시각입니다(월 · 일은 1 부터).
     */
    struct LocalCalendarTime
    {
        int32 _year{ 0 };
        int32 _month{ 0 };
        int32 _day{ 0 };
        int32 _hour{ 0 };
        int32 _minute{ 0 };
        int32 _second{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct WallClock
     * @brief UTC 벽시계입니다.
     */
    struct SW_API WallClock
    {
        /** @brief 지금 UTC 유닉스 시각(밀리초)입니다. */
        static int64 nowUnixMilliseconds() noexcept;

        /**
         * @brief 유닉스 시각(초)을 이 기계의 지역 시각 달력으로 바꿉니다. 여러 스레드에서 같이 불러도 됩니다(스레드마다 결과 버퍼).
         * @return 바꿨으면 true. 플랫폼이 그 시각을 달력으로 나타내지 못하면 false 이고 @p outCalendar 는 건드리지 않습니다.
         */
        [[nodiscard]] static bool toLocalCalendar( int64 unixSeconds, LocalCalendarTime& outCalendar ) noexcept;
    };
} // namespace sw
