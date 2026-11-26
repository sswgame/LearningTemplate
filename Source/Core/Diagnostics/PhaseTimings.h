/**
 * @file PhaseTimings.h
 * @brief 기동 · 초기화처럼 한 번 도는 일의 단계별 시간표입니다. 단계는 차례로 찍거나(`mark`) 스코프로 잰다(`ScopedPhaseTiming`). 같은 이름은 합친다.
 * @details 언리얼 `SCOPED_BOOT_TIMING` 구간 표 자리다(기억에 의존한 비교). 표는 재는 쪽이 멤버로 든다 — 프로세스 전역 표를 두지 않는다(숨은 싱글턴 금지).
 *          RHI 장치 초기화(`IRHIDevice::markInitPhase`)와 호스트 기동이 같은 표를 쓴다. 단계 수가 작고 고정이라 고정 배열에 이름 포인터(정적 글)만 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class PhaseTimings
     * @brief 단계 이름 → 걸린 마이크로초입니다. `begin` 이 시작을 찍고, `describe` 가 지금을 끝으로 한 줄을 만듭니다.
     */
    class SW_API PhaseTimings
    {
    public:
        /** @brief 담는 단계 수의 상한입니다. 넘는 단계는 버린다(단언). */
        static constexpr uint32 kMaxPhaseCount = 16;

        /** @brief 지금을 시작으로 하고 찍은 단계를 비웁니다. */
        void begin();
        /** @brief 앞 `mark`(또는 `begin`) 뒤부터 지금까지를 @p pPhaseName 단계로 더합니다. @p pPhaseName 은 프로그램 내내 사는 글이어야 합니다. */
        void mark( const utf8* pPhaseName );
        /** @brief @p pPhaseName 단계에 @p microseconds 를 더합니다(같은 이름이 있으면 합친다 — 겹쳐 잰 스코프 · 되풀이한 단계). */
        void add( const utf8* pPhaseName, int64 microseconds );
        /** @brief "instance 120 ms, device 30 ms, rest 5 ms (total 155 ms)" 꼴로 돌려줍니다. rest 는 마지막 `mark` 뒤부터 지금까지입니다. */
        [[nodiscard]] string describe() const;
        /** @brief @p pPhaseName 단계의 시간(마이크로초)입니다. 없으면 0 입니다. */
        [[nodiscard]] int64 findMicroseconds( const utf8* pPhaseName ) const;

    private:
        const utf8* _arrPhaseName[kMaxPhaseCount]{};
        int64       _arrPhaseMicro[kMaxPhaseCount]{};
        uint32      _phaseCount{ 0 };
        int64       _startMicro{ 0 };
        int64       _lastMarkMicro{ 0 };
    };

    /** @brief 스코프 동안을 @p table 의 한 단계로 잽니다. 소멸할 때 더한다. */
    class SW_API ScopedPhaseTiming final
    {
    public:
        ScopedPhaseTiming( PhaseTimings& table, const utf8* pPhaseName );
        ~ScopedPhaseTiming();
        ScopedPhaseTiming( const ScopedPhaseTiming& )            = delete;
        ScopedPhaseTiming& operator=( const ScopedPhaseTiming& ) = delete;

    private:
        PhaseTimings& _table;
        const utf8*   _pPhaseName;
        int64         _startMicro;
    };
} // namespace sw
