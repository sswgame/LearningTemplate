/**
 * @file RealtimeSection.h
 * @brief 실시간 구간(오디오 장치 콜백)의 할당 · 해제 · 잠금을 재는 장치입니다 — 검사 구성(`SW_DO_CHECKS`)에서만 세고, 배포에는 멤버도 비용도 없습니다.
 * @details 장치 콜백은 고우선 실시간 스레드라(Android AAudio · 웹 AudioWorklet · XAudio2 처리 스레드) 그 안에서 힙을 부르거나 게임 스레드가 쥔 잠금을 기다리면
 *          우선순위 역전으로 끊김(언더런)이 난다. 평균이 아니라 최악 값의 문제라 평소 측정에 드러나지 않는다 — 구간 안에서 일어난 수를 센다.
 *          구간은 그 주인(오디오 렌더 스레드)이 든 셈 칸(`RealtimeViolationCounters`)을 이 스레드에 걸고(`RealtimeSection`), 할당기(`Memory`)와 잠금(`sw::mutex`)이
 *          걸린 칸이 있으면 센다. 칸에 단언 깃발을 켜면 첫 위반에서 멈춘다(`AudioRenderThread` 는 켠다).
 *          언리얼 Audio Mixer · Wwise · FMOD 가 오디오 스레드에서 할당 · 잠금을 하지 않는 규칙을 실행 중에 확인하는 자리다.
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 실시간 구간에서 일어난 위반 수입니다. 구간의 주인이 들고, 아무 스레드나 읽습니다. */
    struct RealtimeViolationCounters
    {
        DevCounter   _allocationCount;             ///< 구간 안 힙 할당 수(개발 전용 칸 — 배포는 세지 않고 칸도 없다)
        DevCounter   _freeCount;                   ///< 구간 안 힙 해제 수(마지막 `shared_ptr` 를 놓는 것도 여기 든다)
        DevCounter   _lockCount;                   ///< 구간 안 `sw::mutex` · `sw::shared_mutex` 획득 수
        atomic<bool> _bAssertOnViolation{ false }; ///< 켜면 첫 위반에서 단언한다
    };

    /**
     * @class RealtimeSection
     * @brief 이 스레드에 실시간 구간을 겁니다(범위 객체). 겹치면 안쪽 구간이 끝날 때 바깥 구간으로 돌아갑니다. 파일 머리말 참고.
     */
    class SW_API RealtimeSection
    {
    public:
        explicit RealtimeSection( RealtimeViolationCounters& counters );
        ~RealtimeSection();

        RealtimeSection( const RealtimeSection& )            = delete;
        RealtimeSection& operator=( const RealtimeSection& ) = delete;

        /** @brief 이 스레드에 구간이 걸려 있으면 할당 하나를 셉니다(할당기가 부른다). */
        static void recordAllocation();
        /** @brief 이 스레드에 구간이 걸려 있으면 해제 하나를 셉니다(할당기가 부른다). */
        static void recordFree();
        /** @brief 이 스레드에 구간이 걸려 있으면 잠금 획득 하나를 셉니다(잠금 타입이 부른다). */
        static void recordLock();

    private:
#if defined( SW_DO_CHECKS )
        RealtimeViolationCounters* _pOuter; ///< 이 구간 앞에 걸려 있던 칸(겹친 구간)
#endif
    };
} // namespace sw
