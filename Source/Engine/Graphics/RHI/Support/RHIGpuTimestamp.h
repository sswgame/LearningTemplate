/**
 * @file RHIGpuTimestamp.h
 * @brief GPU 타임스탬프 칸을 마이크로초 목록으로 푸는 규칙입니다. 네 백엔드의 공통부입니다.
 * @details 백엔드마다 다른 것은 틱을 **읽는 방법**(쿼리 객체 · 리드백 버퍼 · 가용 비트)과 틱의 단위뿐입니다. 기준점을 고르고
 *          (가장 이른 시각이지 번호가 낮은 칸이 아닙니다) 안 적힌 칸을 음수로 표시하는 규칙은 넷이 같으므로 여기 한 벌만 둡니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @struct RHIGpuTimestampFrame
     * @brief 이미 끝난 한 프레임의 타임스탬프입니다(`IRHIDevice::readTimestamps`).
     * @details 칸 값은 기준점(그 프레임에서 가장 이른 칸) 뒤 마이크로초이고, 기준점 자체는 GPU 시계의 나노초입니다. 엔진 표(`GPU.<패스>`)는
     *          차이만 쓰고, 외부 프로파일러(Tracy GPU 타임라인)는 기준점으로 절대 시각을 만들어 CPU 시계와 맞춥니다
     *          (`IRHIDevice::readGpuClockNanos` 와 같은 시계 영역).
     */
    struct RHIGpuTimestampFrame
    {
        vector<float32> _listMicro;        ///< 칸마다 기준점 뒤 마이크로초. 안 적힌 칸은 음수, 읽을 것이 없으면 비어 있다
        int64           _originNanos{ 0 }; ///< 기준점(가장 이른 칸)의 GPU 시계 나노초
    };
} // namespace sw

namespace sw
{
    /**
     * @class RHIGpuTimestamp
     * @brief 읽힌 틱 배열과 준비 비트를 받아 마이크로초 목록을 냅니다. 디바이스가 없어도 돌아 `RHIGpuTimestampTest` 가 검증합니다.
     */
    class SW_API RHIGpuTimestamp
    {
    public:
        /**
         * @brief 준비된 칸의 틱을 기준점(가장 이른 틱) 기준 마이크로초로 풀고, 기준점을 나노초로 둡니다.
         * @details 기준점은 **가장 이른 시각**이지 번호가 낮은 칸이 아닙니다. 프레임 시작 표식은 번호가 큰 칸에 적히므로(패스 칸과
         *          안 겹치게 뒤쪽을 씁니다), 낮은 번호를 기준으로 삼으면 그 값이 음수가 되어 0 으로 잘립니다. 어느 칸을 기준으로 삼든
         *          구간 차이는 같습니다.
         * @param pTick 칸마다의 틱, `constant::kMaxGpuTimestampSlot` 개. 준비되지 않은 칸의 값은 읽지 않습니다.
         * @param readyMask 이번에 읽힌 칸의 비트.
         * @param nanosPerTick 틱 하나가 몇 나노초인가 (DX: 1e9 / 주파수 · GL: 1 · Vulkan: timestampPeriod). `readGpuClockNanos` 와 같은 환산이어야 합니다.
         * @param outFrame 칸 수만큼 채웁니다. 안 적힌 칸은 -1 이고, 부르는 쪽이 그 쌍을 통째로 버리는 약속입니다. readyMask 가 0 이면 비웁니다.
         * @return 하나라도 풀었으면 true.
         */
        static bool resolve( const uint64* pTick, uint32 readyMask, float64 nanosPerTick, RHIGpuTimestampFrame& outFrame );

        /** @brief 틱을 GPU 시계 나노초로 바꿉니다(`resolve` 의 기준점과 `readGpuClockNanos` 가 같은 환산을 쓰게 한 곳에 둡니다). */
        static int64 convertTickToNanos( uint64 tick, float64 nanosPerTick );
    };
} // namespace sw
