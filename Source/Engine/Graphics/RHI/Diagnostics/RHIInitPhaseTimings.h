/**
 * @file RHIInitPhaseTimings.h
 * @brief RHI 디바이스 초기화의 단계별 시간입니다. 기동의 60% 안팎이 RHI 라(실측, 성능 조사 #17) 어느 단계에서 가는지 한 줄로 남깁니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class RHIInitPhaseTimings
     * @brief `IRHIDevice::initialize` 가 시작을 찍고, 백엔드가 단계 끝마다 이름을 찍습니다(`IRHIDevice::markInitPhase`). 마지막 찍은 뒤부터 끝까지는 "rest" 입니다.
     * @details 언리얼은 RHI 초기화를 `SCOPED_BOOT_TIMING` 구간으로 나눠 잰다(기억에 의존). 여기는 단계 수가 작고 고정이라 고정 배열에 이름 포인터(정적 글)만 든다.
     */
    class SW_API RHIInitPhaseTimings
    {
    public:
        /** @brief 담는 단계 수의 상한입니다. 넘는 단계는 앞 단계에 섞여 들어간다(단언). */
        static constexpr uint32 kMaxPhaseCount = 12;

        /** @brief 지금을 시작으로 하고 찍은 단계를 비웁니다. */
        void begin();
        /** @brief 지금 끝난 단계의 이름을 찍습니다. @p pPhaseName 은 프로그램 내내 사는 글(문자열 상수)이어야 합니다. */
        void mark( const utf8* pPhaseName );
        /** @brief "instance 120 ms, device 30 ms, rest 5 ms (total 155 ms)" 꼴로 돌려줍니다. 지금을 끝으로 봅니다. */
        [[nodiscard]] string describe() const;

    private:
        const utf8* _arrPhaseName[kMaxPhaseCount]{};
        int64       _arrPhaseEndMicro[kMaxPhaseCount]{};
        uint32      _phaseCount{ 0 };
        int64       _startMicro{ 0 };
    };
} // namespace sw
