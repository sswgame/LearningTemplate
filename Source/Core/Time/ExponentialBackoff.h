/**
 * @file ExponentialBackoff.h
 * @brief 다시 연결하기 전에 기다릴 시간을 지수로 늘리는 값 형식입니다 — 첫 실패는 바로, 그다음은 첫 지연부터 두 배씩, 상한까지.
 * @details 온라인 서비스 클라이언트 · 캐시 저장소(RESP) 연결이 같은 규칙을 따로 들고 있었다(R8). 성공하면 `reset` 으로 처음부터 다시 센다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class ExponentialBackoff
     * @brief 실패마다 `takeDelayMs` 가 이번에 기다릴 시간을 돌려주고 다음 실패의 시간으로 늘린다. 스레드 안전하지 않다(소유자의 잠금 아래에서 쓴다).
     */
    class ExponentialBackoff
    {
    public:
        /** @param firstDelayMs 두 번째 실패부터의 첫 대기(ms) */
        constexpr explicit ExponentialBackoff( int64 firstDelayMs )
            : _firstDelayMs{ firstDelayMs }
        {
        }

        /** @brief 이번 실패의 대기(ms)를 돌려주고 다음 실패의 대기로 늘립니다(두 배, @p maxDelayMs 까지). 처음 실패는 0 이다(바로 다시 시도). */
        int64 takeDelayMs( int64 maxDelayMs )
        {
            const int64 delayMs = _nextDelayMs;
            const int64 doubled = _nextDelayMs * 2;
            _nextDelayMs        = _nextDelayMs <= 0 ? _firstDelayMs : ( doubled < maxDelayMs ? doubled : maxDelayMs );
            return delayMs;
        }
        /** @brief 다음 실패에 기다릴 시간(ms)입니다. 바뀌지 않는다. */
        int64 getNextDelayMs() const { return _nextDelayMs; }
        /** @brief 첫 대기(ms)입니다. */
        int64 getFirstDelayMs() const { return _firstDelayMs; }
        /** @brief 성공 - 다음 실패는 다시 바로 시도부터 센다. */
        void reset() { _nextDelayMs = 0; }

    private:
        int64 _firstDelayMs;
        int64 _nextDelayMs{ 0 };
    };
} // namespace sw
