/**
 * @file DevCounter.h
 * @brief 개발 구성에서만 세는 누적 카운터 칸 `DevCounter` 입니다 — 배포(`SW_SHIPPING`)는 빈 형식이라 칸도 증가 비용도 없고 읽으면 늘 0 입니다.
 * @details 감시 값 · 회귀 테스트만 읽는 횟수(풀 적중 · 실패, 실시간 구간 위반, 진단 적용 수)를 런타임 클래스의 `uint32` 칸 · 접근자로 두면 배포에도 증가와
 *          API 가 남는다. 그 칸을 이 형식으로 두면 구성 스위치는 여기 한 곳이고(쓰는 곳마다 `#if` 를 두지 않는다), 감시 값 처리 함수는 배포에서도 컴파일되어
 *          (`SW_AUTOMATION_WATCH` 의 배포 정의가 함수를 붙잡는다) 썩지 않는다. 언리얼 `INC_DWORD_STAT`(STATS=0 인 Shipping 에서 사라짐)과 같은 자리다.
 *          프레임마다 비우는 값 · 시간은 이것이 아니라 프로파일러 계측(`SW_PROFILE_COUNT` · `SW_PROFILE_SCOPE`)이다 — 이 칸은 계측을 켜지 않은 자동화
 *          실행에서도 프레임을 넘어 쌓이는 횟수 자리다. 증가는 relaxed 원자 하나라 어느 스레드에서나 부른다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class DevCounter
     * @brief 개발 구성 전용 누적 카운터입니다. 파일 머리말 참고.
     */
    class DevCounter final
    {
    public:
        DevCounter() = default;
        /** @brief 센 수를 옮겨 담습니다 — 칸을 든 형식(스트림 창 · 바인딩 묶음)이 복사 · 이동될 수 있어 원자처럼 막지 않는다. */
        DevCounter( const DevCounter& other ) { add( other.get() ); }
        DevCounter& operator=( const DevCounter& other )
        {
            reset();
            add( other.get() );
            return *this;
        }

        /** @brief @p count 를 더합니다. */
        void add( [[maybe_unused]] uint64 count = 1 )
        {
#if !defined( SW_SHIPPING )
            _value.fetch_add( count, std::memory_order_relaxed );
#endif
        }
        /** @brief 지금까지 센 수입니다(배포는 0). */
        uint64 get() const
        {
#if !defined( SW_SHIPPING )
            return _value.load( std::memory_order_relaxed );
#else
            return 0;
#endif
        }
        /** @brief 0 으로 되돌립니다(판 · 세션 단위로 세는 칸). */
        void reset()
        {
#if !defined( SW_SHIPPING )
            _value.store( 0, std::memory_order_relaxed );
#endif
        }

#if !defined( SW_SHIPPING )
    private:
        atomic<uint64> _value{ 0 };
#endif
    };
} // namespace sw
