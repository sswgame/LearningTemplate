/**
 * @file TokenBucket.h
 * @brief 토큰 버킷 — @p refillIntervalMs 마다 하나씩 차고 @p capacity 까지 쌓이며, 쓸 때 꺼냅니다. 로그인 시도 · 요청 · 도배 제한이 함께 씁니다.
 * @details 시각은 부르는 쪽이 밀리초로 넘긴다(시험은 가짜 시각, 서버는 한 가지 시계). 시각이 되돌아가면 채우지 않는다. 정수만 쓴다.
 *          처음에는 가득 차 있다 — 처음 몇 번은 몰아 쓸 수 있고(버스트) 그 뒤로는 간격마다 하나다. Envoy · nginx `limit_req` 와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class TokenBucket
     * @brief 버킷 하나입니다(계정 · 연결마다 하나). 생성자가 없어 헤더 기본값이 유일한 초기값이다.
     */
    class TokenBucket
    {
    public:
        void initialize( int32 capacity, int64 refillIntervalMs, int64 nowMs )
        {
            _capacity         = capacity > 0 ? capacity : 1;
            _refillIntervalMs = refillIntervalMs > 0 ? refillIntervalMs : 1;
            _tokenCount       = _capacity;
            _lastRefillMs     = nowMs;
        }

        /** @brief @p count 개가 있으면 꺼내고 true, 없으면 꺼내지 않고 false 입니다. */
        [[nodiscard]] bool tryConsume( int64 nowMs, int32 count = 1 )
        {
            refill( nowMs );
            if ( _tokenCount < count )
                return false;
            _tokenCount -= count;
            return true;
        }

        /** @brief @p count 개가 찰 때까지 남은 밀리초입니다(지금 있으면 0). 거절 응답의 "다시 해도 되는 때" 입니다. */
        int64 computeWaitMs( int64 nowMs, int32 count = 1 ) const
        {
            const int32 tokenCount = computeTokenCount( nowMs );
            if ( tokenCount >= count )
                return 0;
            const int64 missingCount = static_cast<int64>( count - tokenCount );
            const int64 elapsedMs    = nowMs > _lastRefillMs ? nowMs - _lastRefillMs : 0;
            return missingCount * _refillIntervalMs - elapsedMs % _refillIntervalMs;
        }

        /** @brief 지금 쓸 수 있는 수입니다(버킷은 바꾸지 않는다). */
        int32 computeTokenCount( int64 nowMs ) const
        {
            if ( nowMs <= _lastRefillMs )
                return _tokenCount;
            const int64 addedCount = ( nowMs - _lastRefillMs ) / _refillIntervalMs;
            const int64 tokenCount = static_cast<int64>( _tokenCount ) + addedCount;
            return tokenCount < _capacity ? static_cast<int32>( tokenCount ) : _capacity;
        }

        /** @brief 가득 찼는가입니다 — 오래 안 쓴 버킷을 지울 때 본다(가득 찬 버킷은 새로 만든 것과 같다). */
        bool  isFull( int64 nowMs ) const { return computeTokenCount( nowMs ) >= _capacity; }
        int32 getCapacity() const { return _capacity; }

    private:
        void refill( int64 nowMs )
        {
            if ( nowMs <= _lastRefillMs )
                return;
            const int64 addedCount = ( nowMs - _lastRefillMs ) / _refillIntervalMs;
            if ( addedCount <= 0 )
                return;
            const int64 tokenCount = static_cast<int64>( _tokenCount ) + addedCount;
            if ( tokenCount >= _capacity )
            {
                _tokenCount   = _capacity;
                _lastRefillMs = nowMs;
                return;
            }
            _tokenCount = static_cast<int32>( tokenCount );
            _lastRefillMs += addedCount * _refillIntervalMs; // 남은 조각 시간은 다음 채우기로 넘긴다
        }

        int64 _lastRefillMs{ 0 };
        int64 _refillIntervalMs{ 1000 };
        int32 _capacity{ 1 };
        int32 _tokenCount{ 1 };
    };
} // namespace sw
