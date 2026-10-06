/**
 * @file RequestLimits.h
 * @brief 요청 크기 · 문자열 상한 — 서비스 틀이 몸을 읽기 전에, 키트가 칸을 읽을 때 같은 수를 봅니다(조용히 자르지 않고 거절).
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 서비스 요청 상한입니다. */
    struct RequestLimits
    {
        static constexpr int32 kMaxRequestBodySize = 64 * 1024; ///< 서비스 요청 몸 상한(스트림 프레임 상한과 별개 — 서비스는 작게)
        static constexpr int32 kMaxDisplayNameSize = 32;        ///< 표시 이름(UTF-8 바이트)
        static constexpr int32 kMaxFreeTextSize    = 1024;      ///< 자유 글(사유 · 메모)
    };
} // namespace sw
