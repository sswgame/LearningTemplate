/**
 * @file TokenBucketMap.h
 * @brief 키(계정 · 주소 해시)마다의 토큰 버킷입니다. 처음 보는 키는 가득 찬 버킷으로 시작하고, 추적하는 키가 청소 문턱에 닿으면 가득 찬(= 새것과 같은) 버킷을 지웁니다.
 * @details 계정 키트, 서비스 프레임워크, 채팅이 같은 코드를 각자 쓰지 않게 여기 둔다. 청소로 지울 것이 없으면(모두 쓰는 중) 그대로 더한다.
 *          청소 뒤 문턱은 max(상한, 남은 수 × 2) 로 올린다 — 예전에는 상한에서 모두 쓰는 중이면 새 키마다 전체를 훑어, 키를 많이 가진 쪽이
 *          호출마다 O(N) 을 치르게 할 수 있었다(감사 O-18). 두 배 문턱이면 청소 사이에 남은 수만큼 더해져야 하므로 상각 O(1) 이다(해시 표 늘리기와 같은 셈).
 *          메모리는 쓰는 중인 키 수의 두 배까지이고, 쓰는 중인 키 수는 보충 창 안에 들어온 요청 수로 묶인다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Runtime/Online/Guard/TokenBucket.h"

namespace sw
{
    /**
     * @class TokenBucketMap
     * @brief 키마다 버킷입니다. 생성자가 없어 헤더 기본값이 유일한 초기값이다.
     */
    class TokenBucketMap
    {
    public:
        void initialize( int32 capacity, int64 refillIntervalMs, size_t maxTrackedKeyCount )
        {
            _mapKeyToBucket.clear();
            _capacity           = capacity;
            _refillIntervalMs   = refillIntervalMs;
            _maxTrackedKeyCount = maxTrackedKeyCount;
            _sweepThreshold     = maxTrackedKeyCount;
            _sweepCount         = 0;
        }

        /** @brief @p key 의 버킷에서 하나를 씁니다. 없으면 false 와 기다릴 시간입니다. */
        [[nodiscard]] bool tryConsume( uint64 key, int64 nowMs, int64& outRetryAfterMs )
        {
            auto bucketIt = _mapKeyToBucket.find( key );
            if ( bucketIt == _mapKeyToBucket.end() )
            {
                if ( _mapKeyToBucket.size() >= _sweepThreshold )
                    eraseFullBuckets( nowMs );
                TokenBucket bucket;
                bucket.initialize( _capacity, _refillIntervalMs, nowMs );
                bucketIt = _mapKeyToBucket.emplace( key, bucket ).first;
            }
            if ( bucketIt->second.tryConsume( nowMs ) )
                return true;
            outRetryAfterMs = bucketIt->second.computeWaitMs( nowMs );
            return false;
        }

        size_t getTrackedKeyCount() const { return _mapKeyToBucket.size(); }
        /** @brief 전체 훑기 횟수입니다. 상각 비용을 테스트가 본다. */
        size_t getSweepCount() const { return _sweepCount; }

    private:
        void eraseFullBuckets( int64 nowMs )
        {
            ++_sweepCount;
            for ( auto it = _mapKeyToBucket.begin(); it != _mapKeyToBucket.end(); )
            {
                if ( it->second.isFull( nowMs ) )
                    it = _mapKeyToBucket.erase( it );
                else
                    ++it;
            }
            const size_t doubledCount = _mapKeyToBucket.size() * 2;
            _sweepThreshold           = doubledCount > _maxTrackedKeyCount ? doubledCount : _maxTrackedKeyCount;
        }

    private:
        unordered_map<uint64, TokenBucket> _mapKeyToBucket{};
        size_t                             _maxTrackedKeyCount{ 4096 };
        size_t                             _sweepThreshold{ 4096 }; ///< 새 키가 이 수에 닿으면 청소한다. 청소마다 다시 정한다
        size_t                             _sweepCount{ 0 };
        int64                              _refillIntervalMs{ 1000 };
        int32                              _capacity{ 1 };
    };
} // namespace sw
