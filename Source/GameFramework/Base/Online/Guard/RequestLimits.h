/**
 * @file RequestLimits.h
 * @brief 요청 크기와 문자열 상한입니다. 서비스 프레임워크가 본문을 읽기 전에, 키트가 필드를 읽을 때 같은 값을 봅니다. 조용히 자르지 않고 거절합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 서비스 요청 상한입니다. */
    struct RequestLimits
    {
        static constexpr int32 kMaxRequestBodySize = 64 * 1024; ///< 서비스 요청 본문 상한. 스트림 프레임 상한과 별개이며 서비스는 작게 둔다
        static constexpr int32 kMaxDisplayNameSize = 32;        ///< 표시 이름(UTF-8 바이트)
        static constexpr int32 kMaxFreeTextSize    = 1024;      ///< 자유 글(사유 · 메모)
    };
} // namespace sw
