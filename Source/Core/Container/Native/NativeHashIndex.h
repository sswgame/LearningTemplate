/**
 * @file NativeHashIndex.h
 * @brief 밀집 해시 표(`internal::DenseHashTable`)의 자체 색인입니다(기본 구성) — 버킷 표 + 노드 안 체인(`_next`). 쓰는 쪽은 `Core/Container/DenseHashTable.h` 를 include 합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Hash/HashUtil.h"

namespace sw::internal
{
    /** @brief 해시 태그로 밀집 배열 번호를 찾는 색인입니다. 노드 배열은 표가 들고 호출마다 넘깁니다(체인이 노드 안에 있다). */
    template <typename Node>
    class DenseHashIndex
    {
    public:
        static constexpr bool kBitwiseRelocatable = true;

        void clear() noexcept
        {
            for ( uint32& bucket : _listBucket )
            {
                bucket = kEmptySlot;
            }
        }

        /** @brief 태그가 같고 @p isMatch( node, index ) 인 노드의 번호입니다. 없으면 빈 자리 값입니다. */
        template <typename TMatch>
        SW_INLINE uint32 find( const vector<Node>& listNode, uint32 tag, TMatch&& isMatch ) const
        {
            if ( _listBucket.empty() )
                return kEmptySlot;
            // 무검사 접근: 버킷 번호는 표 크기로 접은 값이고 체인 번호는 이 표가 쓴 값이다 — 조회 핫 루프에 범위 검사를 두지 않는다
            const uint32* pBucketData  = std::as_const( _listBucket ).data();
            const Node*   pDenseData   = listNode.data();
            uint32        currentIndex = pBucketData[computeBucketIndex( tag, _listBucket.size() )];
            while ( currentIndex != kEmptySlot )
            {
                if ( pDenseData[currentIndex]._tag == tag && isMatch( pDenseData[currentIndex], currentIndex ) )
                    return currentIndex;
                currentIndex = pDenseData[currentIndex]._next;
            }
            return kEmptySlot;
        }

        /** @brief 원소 수가 버킷 수에 닿으면(부하율 1) 버킷을 두 배로 늘립니다. 버킷이 없으면 최소 개수로 만듭니다. */
        SW_INLINE void reserveForInsert( vector<Node>& listNode )
        {
            if ( _listBucket.empty() || listNode.size() >= _listBucket.size() )
                rehash( listNode, _listBucket.empty() ? kMinBucketCount : _listBucket.size() * 2 );
        }

        /** @brief 태그를 든 노드 @p index 를 체인 머리에 겁니다. */
        SW_INLINE void link( vector<Node>& listNode, uint32 index )
        {
            const size_t bucketIndex = computeBucketIndex( listNode[index]._tag, _listBucket.size() );
            listNode[index]._next    = _listBucket[bucketIndex];
            _listBucket[bucketIndex] = index;
        }

        /** @brief 태그가 같고 @p isMatch( node, index ) 인 노드를 체인에서 떼고 번호를 돌려줍니다. 없으면 빈 자리 값입니다. */
        template <typename TMatch>
        uint32 unlink( vector<Node>& listNode, uint32 tag, TMatch&& isMatch )
        {
            if ( _listBucket.empty() )
                return kEmptySlot;
            const size_t bucketIndex   = computeBucketIndex( tag, _listBucket.size() );
            uint32       previousIndex = kEmptySlot;
            for ( uint32 currentIndex = _listBucket[bucketIndex]; currentIndex != kEmptySlot; currentIndex = listNode[currentIndex]._next )
            {
                if ( listNode[currentIndex]._tag == tag && isMatch( std::as_const( listNode[currentIndex] ), currentIndex ) )
                {
                    if ( previousIndex == kEmptySlot )
                        _listBucket[bucketIndex] = listNode[currentIndex]._next;
                    else
                        listNode[previousIndex]._next = listNode[currentIndex]._next;
                    return currentIndex;
                }
                previousIndex = currentIndex;
            }
            return kEmptySlot;
        }

        /** @brief 태그 @p tag 의 체인에서 @p oldIndex 를 가리키던 자리를 @p newIndex 로 바꿉니다(노드가 자리를 옮겼다). */
        void relocate( vector<Node>& listNode, uint32 tag, uint32 oldIndex, uint32 newIndex )
        {
            uint32* pLink = &_listBucket[computeBucketIndex( tag, _listBucket.size() )];
            while ( *pLink != oldIndex )
            {
                pLink = &listNode[*pLink]._next;
            }
            *pLink = newIndex;
        }

        /** @brief 버킷 수를 count 이상인 2의 거듭제곱으로 늘립니다(`reserve( 3000 )` 이면 4096). `computeBucketIndex` 가 마스크로 자르는 전제다. */
        void rehash( vector<Node>& listNode, size_t count )
        {
            if ( count <= _listBucket.size() )
                return;
            size_t bucketCount = _listBucket.empty() ? kMinBucketCount : _listBucket.size();
            while ( bucketCount < count )
            {
                bucketCount *= 2;
            }
            rebuild( listNode, bucketCount );
        }

        /** @brief 버킷 수를 원소 수 이상인 가장 작은 2의 거듭제곱(최소 `kMinBucketCount`)으로 줄입니다. 원소가 없으면 버퍼를 놓습니다. */
        void shrink( vector<Node>& listNode )
        {
            if ( listNode.empty() )
            {
                _listBucket.clear();
                _listBucket.shrink_to_fit();
                return;
            }
            size_t bucketCount = kMinBucketCount;
            while ( bucketCount < listNode.size() )
            {
                bucketCount *= 2;
            }
            if ( bucketCount < _listBucket.size() )
            {
                rebuild( listNode, bucketCount );
                _listBucket.shrink_to_fit();
            }
        }

    private:
        static constexpr uint32 kEmptySlot = constant::invalid_index::kUint32;

        /** @brief 버킷 수의 최솟값입니다. 버킷 수는 항상 2의 거듭제곱입니다(`computeBucketIndex` 가 마스크로 자르기 때문입니다). */
        static constexpr size_t kMinBucketCount = 16;

        /**
         * @brief 해시 태그를 버킷 번호로 바꿉니다. 곱셈 한 번 · 접기 한 번 · 마스크 한 번입니다. 버킷 수는 항상 2의 거듭제곱입니다(`rehash`).
         * @details `hash % 버킷 수` 를 쓰지 않습니다. 64비트 나눗셈은 이 CPU 에서 수십 사이클이라 캐시에 든 조회 하나와 맞먹습니다.
         *          피보나치 상수를 곱하고 위쪽 절반을 아래쪽 절반에 접어 넣으므로, 아래 비트가 고르지 않은
         *          해시도 고르게 퍼집니다. libstdc++ · libc++ 의 std::hash 는 정수 · 포인터에 대해 항등 함수라서, 2의 거듭제곱 크기에
         *          `%` 만 쓰면 아래 비트만 남아 8바이트 정렬 포인터가 버킷 여덟 개 중 하나에 몰립니다.
         *          순회는 밀집 배열을 따라가므로 버킷 배치가 바뀌어도 순서는 그대로입니다.
         */
        static size_t computeBucketIndex( uint32 tag, size_t bucketCount ) noexcept
        {
            const uint64 product = static_cast<uint64>( tag ) * HashUtil::kGoldenRatio64;
            return static_cast<size_t>( product ^ ( product >> 32 ) ) & ( bucketCount - 1 );
        }

        /** @brief 버킷 표를 bucketCount(2의 거듭제곱) 개로 다시 만들고 체인을 다시 잇습니다. */
        void rebuild( vector<Node>& listNode, size_t bucketCount )
        {
            _listBucket.assign( bucketCount, kEmptySlot );
            for ( uint32 denseIndex = 0; denseIndex < static_cast<uint32>( listNode.size() ); ++denseIndex )
                link( listNode, denseIndex ); // 태그가 해시를 들고 있다 — 키를 다시 해시하지 않는다
        }

    private:
        vector<uint32> _listBucket; ///< 버킷마다 체인 첫 노드의 인덱스
    };
} // namespace sw::internal
