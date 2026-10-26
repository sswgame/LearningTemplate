/**
 * @file SecureRandom.h
 * @brief 운영체제의 암호학적 난수(Windows `BCryptGenRandom` · 리눅스 `getrandom`)를 읽는 하나의 창구입니다.
 * @details UUID · 챌린지 비밀 · 멱등 키 · 세션 ID 처럼 다른 실행 · 다른 호스트와 겹치지 않고 미리 알 수 없어야 하는 값에 씁니다.
 *          `std::random_device` 는 구현이 결정적 엔진이어도 표준을 지키고(옛 MinGW), 엔트로피를 확인할 길이 없고, 실패가 예외라 쓰지 않습니다.
 *          되풀이되어야 하는 수열(테스트 · 리플레이 · 절차 생성)은 이것이 아니라 `DeterministicRandom32` · `DeterministicRandom64` 입니다.
 * @note Math 티어라 `span` 을 받지 못해(Container 가 위 티어) 포인터와 바이트 수를 받습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 운영체제 난수입니다(전부 static, 스레드 안전). */
    struct SW_API SecureRandom
    {
        /**
         * @brief @p pOutBytes 의 @p byteCount 바이트를 운영체제 난수로 채웁니다.
         * @details 운영체제가 난수를 주지 못하면 이유를 stderr 에 남기고 멈춥니다 — 예측 가능한 값으로 대신하면 비밀이 비밀이 아니게 되므로 폴백을 두지 않습니다.
         */
        static void fill( uint8* pOutBytes, size_t byteCount );

        /** @brief 운영체제 난수 64 비트입니다. */
        [[nodiscard]] static uint64 nextUint64();
    };
} // namespace sw
