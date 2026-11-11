/**
 * @file ExponentialBackoff.h
 * @brief 다시 연결하기 전에 기다릴 시간을 지수로 늘리는 값 형식입니다 — 첫 지연부터 두 배씩, 상한까지, 지터를 섞어서.
 * @details 온라인 서비스 클라이언트 · 캐시 저장소(RESP) 연결이 같은 규칙을 따로 들고 있었다(R8). 성공하면 `reset` 으로 처음부터 다시 센다.
 *          지터: 첫 실패는 [0, 첫 지연](full jitter), 그다음은 [지연/2, 지연](equal jitter). 지터가 없으면 서버가 죽었을 때 모든 클라이언트가 같은 위상으로
 *          재시도해 복구 직후의 서버에 한꺼번에 몰린다(감사 C-11 — gRPC 연결 백오프 ±20 %, AWS SDK full jitter). 위 끝은 지터 없는 값이라 "그때까지는 다시 한다" 는 약속은 같다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Math/SecureRandom.h"

namespace sw
{
    /**
     * @class ExponentialBackoff
     * @brief 실패마다 `takeDelayMs` 가 이번에 기다릴 시간을 돌려주고 다음 실패의 시간으로 늘린다. 스레드 안전하지 않다(소유자의 잠금 아래에서 쓴다).
     */
    class ExponentialBackoff
    {
    public:
        /**
         * @param firstDelayMs 두 번째 실패부터의 첫 대기(ms)이자 첫 실패 지터의 위 끝
         * @param jitterSeed   지터 수열의 씨앗(0 이 아님). 클라이언트마다 달라야 흩어진다 — 실행 코드는 `SecureRandom`, 테스트는 고정 값
         */
        ExponentialBackoff( int64 firstDelayMs, uint32 jitterSeed )
            : _random{ jitterSeed }
            , _firstDelayMs{ firstDelayMs }
        {
        }

        /** @brief 씨앗을 운영체제 난수로 정한 백오프입니다(실행 코드의 기본 — 프로세스마다 지터 수열이 다르다). */
        static ExponentialBackoff createWithRandomSeed( int64 firstDelayMs ) { return ExponentialBackoff{ firstDelayMs, static_cast<uint32>( SecureRandom::nextUint64() ) | kNonZeroSeedBit }; }

        /** @brief 이번 실패의 대기(ms)를 지터를 섞어 돌려주고 다음 실패의 대기로 늘립니다(두 배, @p maxDelayMs 까지). */
        int64 takeDelayMs( int64 maxDelayMs )
        {
            const int64 baseMs  = _nextDelayMs;
            const int64 doubled = _nextDelayMs * 2;
            _nextDelayMs        = _nextDelayMs <= 0 ? _firstDelayMs : ( doubled < maxDelayMs ? doubled : maxDelayMs );
            const float32 unit  = _random.nextFloat();
            if ( baseMs <= 0 )
                return static_cast<int64>( static_cast<float32>( _firstDelayMs ) * unit );
            const int64 halfMs = baseMs / 2;
            return halfMs + static_cast<int64>( static_cast<float32>( baseMs - halfMs ) * unit );
        }
        /** @brief 다음 실패에 기다릴 지터 없는 시간(ms)입니다. 바뀌지 않는다. */
        int64 getNextDelayMs() const { return _nextDelayMs; }
        /** @brief 첫 대기(ms)입니다. */
        int64 getFirstDelayMs() const { return _firstDelayMs; }
        /** @brief 성공 - 다음 실패는 다시 바로 시도부터 센다. */
        void reset() { _nextDelayMs = 0; }

    private:
        static constexpr uint32 kNonZeroSeedBit = 1u; ///< xorshift 의 고정점 0 을 피한다

        DeterministicRandom32 _random;
        int64                 _firstDelayMs;
        int64                 _nextDelayMs{ 0 };
    };
} // namespace sw
