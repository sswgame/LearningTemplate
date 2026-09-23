/**
 * @file RHIGpuTimestamp.h
 * @brief GPU 타임스탬프 칸을 마이크로초 목록으로 푸는 규칙 — 백엔드 넷의 공통부
 * @details 백엔드마다 다른 것은 틱을 **읽는 방법**(쿼리 객체 · 리드백 버퍼 · 가용 비트)과 틱의 단위뿐이다. 기준점을 고르고
 *          (가장 이른 시각 — 번호가 낮은 칸이 아니다) 안 적힌 칸을 음수로 표시하는 규칙은 넷이 같은 스무 줄을 각자 들고 있었다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /**
     * @class RHIGpuTimestamp
     * @brief 읽힌 틱 배열과 준비 비트를 받아 마이크로초 목록을 냅니다. 디바이스가 없어도 돌아 `RHISupportTest` 가 검증한다.
     */
    class SW_API RHIGpuTimestamp
    {
    public:
        /**
         * @brief 준비된 칸의 틱을 기준점(가장 이른 틱) 기준 마이크로초로 풉니다.
         * @details 기준점은 **가장 이른 시각**이다 — 번호가 낮은 칸이 아니다. 프레임 시작 표식은 번호가 큰 칸에 적히므로(패스 칸과
         *          안 겹치게 뒤쪽을 쓴다), 낮은 번호를 기준으로 삼으면 그 값이 음수가 되어 0 으로 잘린다. 어느 칸을 기준으로 삼든
         *          구간 차이는 같다.
         * @param pTick 칸마다의 틱, `constant::kMaxGpuTimestampSlot` 개. 준비되지 않은 칸의 값은 읽지 않는다.
         * @param readyMask 이번에 읽힌 칸의 비트.
         * @param microPerTick 틱 하나가 몇 마이크로초인가 (DX: 1e6 / 주파수 · GL: 1e-3 · Vulkan: period / 1e3).
         * @param outListMicro 칸 수만큼 채운다. 안 적힌 칸은 -1 — 호출자가 그 쌍을 통째로 버리는 약속이다. readyMask 가 0 이면 비운다.
         * @return 하나라도 풀었으면 true.
         */
        static bool resolveMicro( const uint64* pTick, uint32 readyMask, float64 microPerTick, vector<float32>& outListMicro );
    };
} // namespace sw
