/**
 * @file TokenBucketMap.h
 * @brief 키(계정 · 주소 해시)마다의 토큰 버킷입니다. 처음 보는 키는 가득 찬 버킷으로 시작하고, 추적하는 키가 상한에 닿으면 가득 찬(= 새것과 같은) 버킷을 지웁니다.
 * @details 계정 키트 · 서비스 틀 · 채팅이 같은 모양을 손으로 쓰지 않게 여기 둔다. 상한에서 지울 것이 없으면(모두 쓰는 중) 그대로 더한다 — 상한은 청소 시점이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"

#include "GameFramework/Base/Online/Guard/TokenBucket.h"

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
        }

        /** @brief @p key 의 버킷에서 하나를 씁니다. 없으면 false 와 기다릴 시간입니다. */
        [[nodiscard]] bool tryConsume( uint64 key, int64 nowMs, int64& outRetryAfterMs )
        {
            auto bucketIt = _mapKeyToBucket.find( key );
            if ( bucketIt == _mapKeyToBucket.end() )
            {
                if ( _mapKeyToBucket.size() >= _maxTrackedKeyCount )
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

    private:
        void eraseFullBuckets( int64 nowMs )
        {
            for ( auto it = _mapKeyToBucket.begin(); it != _mapKeyToBucket.end(); )
            {
                if ( it->second.isFull( nowMs ) )
                    it = _mapKeyToBucket.erase( it );
                else
                    ++it;
            }
        }

        unordered_map<uint64, TokenBucket> _mapKeyToBucket{};
        size_t                             _maxTrackedKeyCount{ 4096 };
        int64                              _refillIntervalMs{ 1000 };
        int32                              _capacity{ 1 };
    };
} // namespace sw
