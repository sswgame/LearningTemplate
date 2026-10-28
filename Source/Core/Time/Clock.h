/**
 * @file Clock.h
 * @brief 시각을 직접 읽지 않고 받아 쓰는 객체가 받는 시계 계약(`IClock`)과 두 구현 — 운영(`SystemMonotonicClock`)과 수동(`ManualClock`)입니다.
 * @details 매 틱 `nowMs` 인자를 받는 서비스는 이 계약이 필요 없다. 이것은 틱 밖에서도 시각을 읽는 객체(임시 저장소의 만료 · 요청 시한 ·
 *          다시 연결 백오프, I/O 스레드)가 받는다. 운영은 `SystemMonotonicClock`, 테스트는 `ManualClock` 을 주고 손으로 민다 — 실제로 잠들지 않는다.
 *          "0 이면 실제 시계" 같은 폴백은 두지 않는다. 시계는 생성자에서 참조로 받으므로 없을 수 없다.
 *          언리얼 `FApp::GetCurrentTime` + 자동화 테스트의 고정 시각, 유니티 `Time` + 테스트 프레임워크의 시간 고정과 같은 갈래다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/TimePoint.h"

namespace sw
{
    /**
     * @class IClock
     * @brief 단조 시각(나노초)을 주는 시계입니다. 기준점은 구현마다 다르므로 두 시각의 차이나 같은 시계로 만든 만료 시각과의 비교로만 씁니다.
     */
    class IClock
    {
    public:
        IClock()          = default;
        virtual ~IClock() = default;

        IClock( const IClock& )            = default;
        IClock& operator=( const IClock& ) = default;

        /** @brief 지금 시각(나노초)입니다. 여러 스레드에서 같이 불러도 됩니다. */
        virtual int64 nowNanoseconds() const noexcept = 0;

        /** @brief 지금 시각(밀리초)입니다. 단조 시각 형식이라 유닉스 시각을 받는 자리(`UnixMilliseconds`)에 넣으면 컴파일이 안 됩니다. */
        MonotonicMilliseconds nowMilliseconds() const noexcept { return MonotonicMilliseconds{ nowNanoseconds() / constant::kNanosecondsPerMillisecond }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 운영 시계입니다. 실제 단조 시계(`MonotonicClock`)를 읽습니다. 상태가 없어 어디서 만들어도 같은 시각을 줍니다. */
    class SystemMonotonicClock final : public IClock
    {
    public:
        int64 nowNanoseconds() const noexcept override { return MonotonicClock::nowNanoseconds(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 손으로 미는 시계입니다(테스트 · 가짜 시각으로 도는 서버). 미는 쪽과 읽는 쪽이 다른 스레드여도 됩니다. */
    class ManualClock final : public IClock
    {
    public:
        /** @param start 처음 시각입니다. 단조 시각 형식이라 유닉스 시각(`UnixMilliseconds`)을 넣으면 컴파일이 안 됩니다. */
        explicit ManualClock( MonotonicMilliseconds start ) noexcept
            : _nowNanoseconds{ start.count() * constant::kNanosecondsPerMillisecond }
        {
        }

        int64 nowNanoseconds() const noexcept override { return _nowNanoseconds.load(); }

        /** @brief 시각을 @p now 로 옮깁니다. 테스트가 가짜 경과 시간을 정할 때 씁니다. */
        void setMilliseconds( MonotonicMilliseconds now ) noexcept { _nowNanoseconds.store( now.count() * constant::kNanosecondsPerMillisecond ); }
        /** @brief 시각을 @p deltaMilliseconds 만큼 앞으로 밉니다. */
        void advanceMilliseconds( int64 deltaMilliseconds ) noexcept { (void)_nowNanoseconds.fetch_add( deltaMilliseconds * constant::kNanosecondsPerMillisecond ); }

    private:
        atomic<int64> _nowNanoseconds;
    };
} // namespace sw
