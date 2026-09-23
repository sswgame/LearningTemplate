/**
 * @file PagedArray.h
 * @brief 원소 주소가 옮겨지지 않는 청크(페이지) 배열입니다. 쓰기는 한 번에 한 스레드, 읽기는 락 없이 합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    /**
     * @brief 고정 크기 청크에 원소를 담아, 청크를 더 붙여도 이미 있는 원소의 주소가 바뀌지 않는 배열입니다.
     * @details `vector` 는 커질 때 재할당하면서 이미 넘겨준 원소 주소를 전부 무효로 만듭니다. 인덱스로 찾아 주소를
     *          돌려주는 표가 그 위에 있으면, 한 스레드가 원소를 더하는 순간 다른 스레드가 들고 있던 주소가 해제된
     *          메모리를 가리킵니다. 이 배열은 청크를 따로 할당하고 청크 표 자체는 고정 크기라서 그런 무효화가 없습니다.
     *
     *          원소는 인덱스로 바로 씁니다(`ensure`). 청크는 처음 닿을 때 만들어지며, 모든 원소를 값 초기화한 뒤에
     *          청크 포인터를 release 로 발행합니다. 그래서 읽는 쪽(`find`)은 청크 포인터 하나만 acquire 로 읽으면 되고,
     *          아직 없는 청크면 nullptr 을 받습니다.
     *
     *          쓰는 곳은 둘입니다. `SlotHandleTable` 은 슬롯을 여기에 두고 RHI 가 드로우마다 락 없이 찾습니다.
     *          `GameObjectManager` 는 objectId → GameObject* 표를 여기에 두고 핸들을 풀 때마다 락 없이 읽습니다.
     *          예전에는 둘이 청크 표를 각자 구현했습니다.
     *
     *          **동시성 계약**
     *          - `find` 는 락이 없고, 쓰기와 동시에 불러도 안전합니다.
     *          - `ensure` · `forEachElement` · `releaseChunks` 는 서로 직렬화되지 않습니다. 쓰는 스레드가 여럿이면
     *            소유자가 뮤텍스로 감싸야 합니다(두 사용처 모두 그렇게 씁니다).
     *          - `releaseChunks` 는 청크를 해제하므로 읽는 스레드가 없을 때만 부릅니다.
     *          - 원소 **내용**을 읽는 것과 다른 스레드가 그 원소를 고치는 것 사이의 동기화는 이 배열이 맡지 않습니다.
     *            원소 안의 원자값(세대 · 포인터)이나 상위 계층이 맡습니다.
     *
     * @tparam T                원소 타입. 값 초기화할 수 있어야 합니다.
     * @tparam kElementPerChunk 청크 하나의 원소 수. 2의 거듭제곱이면 인덱스 계산이 시프트와 마스크가 됩니다.
     * @tparam kMaxChunk        청크 표의 칸 수. `kElementPerChunk * kMaxChunk` 가 다룰 수 있는 인덱스 범위입니다.
     */
    template <typename T, uint32 kElementPerChunk = 256, uint32 kMaxChunk = 4096>
    class PagedArray
    {
    public:
        /** @brief 다룰 수 있는 인덱스 개수입니다. */
        static constexpr uint64 kCapacity = static_cast<uint64>( kElementPerChunk ) * kMaxChunk;

        /** @brief 청크 없이 시작합니다. */
        PagedArray()
        {
            for ( atomic<T*>& chunk : _arrChunk )
            {
                chunk.store( nullptr, std::memory_order_relaxed );
            }
        }

        /** @brief 모든 청크를 해제합니다. */
        ~PagedArray() { releaseChunks(); }

        /** @brief 복사를 금지합니다. */
        PagedArray( const PagedArray& ) = delete;
        /** @brief 대입을 금지합니다. */
        PagedArray& operator=( const PagedArray& ) = delete;

        /** @brief @p index 가 이 배열이 다룰 수 있는 범위인지 반환합니다. */
        static constexpr bool isInRange( uint64 index ) { return index < kCapacity; }

        /** @brief 원소 주소를 반환합니다. 범위 밖이거나 청크가 아직 없으면 nullptr 입니다. 락을 잡지 않습니다. */
        SW_INLINE T* find( uint64 index ) { return const_cast<T*>( static_cast<const PagedArray*>( this )->find( index ) ); }

        /** @brief 원소 주소를 반환합니다. 범위 밖이거나 청크가 아직 없으면 nullptr 입니다. 락을 잡지 않습니다. */
        SW_INLINE const T* find( uint64 index ) const
        {
            if ( isInRange( index ) == false )
                return nullptr;
            const T* pChunk = _arrChunk[index / kElementPerChunk].load( std::memory_order_acquire );
            if ( pChunk == nullptr )
                return nullptr;
            return std::addressof( pChunk[index % kElementPerChunk] );
        }

        /**
         * @brief 원소 주소를 반환하고, 청크가 없으면 만들어 발행합니다. 범위 밖이면 nullptr 입니다.
         * @details 새 청크는 원소를 전부 값 초기화한 뒤에 release 로 발행합니다. 그래서 `find` 로 청크를 본 스레드는
         *          초기화가 끝난 원소만 봅니다. 쓰는 쪽끼리는 직렬화하지 않습니다.
         */
        T* ensure( uint64 index )
        {
            if ( isInRange( index ) == false )
                return nullptr;
            atomic<T*>& chunkSlot = _arrChunk[index / kElementPerChunk];
            T*          pChunk    = chunkSlot.load( std::memory_order_relaxed );
            if ( pChunk == nullptr )
            {
                pChunk = new T[kElementPerChunk]{};
                chunkSlot.store( pChunk, std::memory_order_release );
            }
            return std::addressof( pChunk[index % kElementPerChunk] );
        }

        /** @brief 만들어진 청크의 모든 원소에 @p fn(T&) 을 부릅니다. 청크는 해제하지 않습니다. */
        template <typename Fn>
        void forEachElement( Fn&& fn )
        {
            for ( atomic<T*>& chunkSlot : _arrChunk )
            {
                T* pChunk = chunkSlot.load( std::memory_order_relaxed );
                if ( pChunk == nullptr )
                    continue;
                for ( uint32 elementIndex = 0; elementIndex < kElementPerChunk; ++elementIndex )
                {
                    fn( pChunk[elementIndex] );
                }
            }
        }

        /** @brief 모든 청크를 해제하고 청크 표를 비웁니다. 읽는 스레드가 없을 때만 부릅니다. */
        void releaseChunks()
        {
            for ( atomic<T*>& chunkSlot : _arrChunk )
            {
                T* pChunk = chunkSlot.load( std::memory_order_relaxed );
                chunkSlot.store( nullptr, std::memory_order_relaxed );
                delete[] pChunk;
            }
        }

    private:
        /** @brief 청크 포인터 표입니다. 한 번 발행한 청크는 `releaseChunks` 전까지 그 자리에 있습니다. */
        atomic<T*> _arrChunk[kMaxChunk];
    };
} // namespace sw
