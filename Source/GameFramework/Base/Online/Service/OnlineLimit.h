/**
 * @file OnlineLimit.h
 * @brief 온라인 서비스 여러 곳이 함께 지키는 상한입니다. 저장소 · 캐시 · 프로토콜이 따로 적으면 한쪽만 바뀌므로 여기 하나만 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 온라인 서비스(저장소 · 캐시 · 프로토콜 · 서비스 로직)가 나눠 쓰는 상한입니다. */
    struct OnlineLimit
    {
        static constexpr int32 kMaxKeySize       = 256;  ///< 저장소 · 캐시 · 우편함 키 한 개의 최대 길이
        static constexpr int32 kMaxBalanceCount  = 1024; ///< 프로토콜 메시지 하나가 싣는 잔액 수의 상한(경제 · 관리자 · 우편함)
        static constexpr int32 kMaxConflictRetry = 4;    ///< 저장소 충돌(낙관적 잠금) 때 서비스가 다시 시도하는 횟수(길드 · 소셜 · 리더보드)
    };
} // namespace sw
