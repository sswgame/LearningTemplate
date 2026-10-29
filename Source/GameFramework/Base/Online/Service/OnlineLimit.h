/**
 * @file OnlineLimit.h
 * @brief 온라인 서비스 여러 곳이 함께 지키는 상한입니다. 저장소, 캐시, 프로토콜이 따로 적으면 한쪽만 바뀌므로 여기 하나만 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 온라인 서비스(저장소, 캐시, 프로토콜, 서비스 로직)가 나눠 쓰는 상한입니다. */
    struct OnlineLimit
    {
        static constexpr int32 kMaxKeySize       = 256;  ///< 저장소, 캐시, 우편함 키 한 개의 최대 길이
        static constexpr int32 kMaxBalanceCount  = 1024; ///< 프로토콜 메시지 하나가 싣는 잔액 수의 상한(경제, 관리자, 우편함)
        static constexpr int32 kMaxConflictRetry = 4;    ///< 저장소 충돌(낙관적 잠금) 때 서비스가 다시 시도하는 횟수(길드, 소셜, 리더보드)

        // 요청 크기와 문자열 상한 — 서비스 프레임워크가 본문을 읽기 전에, 키트가 필드를 읽을 때 같은 값을 본다. 조용히 자르지 않고 거절한다.
        static constexpr int32 kMaxRequestBodySize = 64 * 1024; ///< 서비스 요청 본문 상한. 스트림 프레임 상한과 별개이며 서비스는 작게 둔다
        static constexpr int32 kMaxDisplayNameSize = 32;        ///< 표시 이름(UTF-8 바이트)
        static constexpr int32 kMaxFreeTextSize    = 1024;      ///< 자유 글(사유 · 메모)
    };
} // namespace sw
