/**
 * @file PagedArray.h
 * @brief 성장해도 기존 원소의 주소가 변하지 않는 청크(페이지) 기반 배열입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    /**
     * @brief 고정 크기 청크에 원소를 담아, 성장 시에도 기존 원소 주소가 절대 변하지 않는 배열입니다.
     * @details `vector`는 `push_back` 때 재할당하면서 이미 넘겨준 원소 포인터를 전부 무효화한다.
     *          핸들 테이블처럼 "인덱스로 찾아 포인터를 돌려주는" 구조가 그 위에 올라가면, 한 스레드가
     *          원소를 추가하는 순간 다른 스레드가 들고 있던 포인터가 dangling 이 되어 해석 불가능한
     *          크래시(GPU라면 NULL 주소 참조)로 이어진다. 이 컨테이너는 원소를 청크 단위로 따로
     *          할당하므로 그런 무효화가 원천적으로 없다.
     *
     *          **동시성 계약**: 읽기(`at`/`size`)는 락이 없고, 쓰기(`pushBack`/`clear`)와 동시에
     *          일어나도 안전하다. `pushBack`이 청크 포인터를 먼저 쓰고 원소 개수를 release 로 발행하며,
     *          읽기는 개수를 acquire 로 읽고 그 범위 안에서만 접근하기 때문이다. 다만 **쓰기끼리는
     *          직렬화되지 않으므로**, 여러 스레드가 쓴다면 소유자가 뮤텍스로 보호해야 한다
     *          (읽기 경로에는 락이 필요 없다는 것이 이 설계의 요점이다).
     *
     *          원소 자체의 내용을 읽는 것과 그 원소를 다른 스레드가 덮어쓰는 것 사이의 동기화는
     *          이 컨테이너의 책임이 아니다 — 그건 상위 계층(세대 검사, 지연 해제 큐 등)이 맡는다.
     */
    template <typename T, uint32 kElementPerChunk = 256>
    class PagedArray
    {
    public:
        /** @brief 빈 배열을 만듭니다. */
        PagedArray() = default;

        /** @brief 모든 청크를 해제합니다. */
        ~PagedArray() { releaseChunks(); }

        /** @brief 복사를 금지합니다. */
        PagedArray( const PagedArray& ) = delete;
        /** @brief 대입을 금지합니다. */
        PagedArray& operator=( const PagedArray& ) = delete;

        /** @brief 발행된 원소 개수입니다. 읽기 전용 스레드에서 락 없이 호출해도 됩니다. */
        uint32 size() const { return _count.load( std::memory_order_acquire ); }

        /** @brief 비어 있으면 true 입니다. */
        bool empty() const { return size() == 0; }

        /** @brief 인덱스가 발행 범위 밖이면 nullptr, 아니면 원소 포인터입니다. 락이 없습니다. */
        T* at( uint32 index )
        {
            if ( index >= size() )
                return nullptr;
            return std::addressof( _arrChunk[index / kElementPerChunk][index % kElementPerChunk] );
        }

        /** @brief 인덱스가 발행 범위 밖이면 nullptr, 아니면 원소 포인터입니다. 락이 없습니다. */
        const T* at( uint32 index ) const
        {
            if ( index >= size() )
                return nullptr;
            return std::addressof( _arrChunk[index / kElementPerChunk][index % kElementPerChunk] );
        }

        /**
         * @brief 뒤에 원소를 추가하고 그 인덱스를 반환합니다. 실패하면 kInvalidIndex 입니다.
         * @details 청크 포인터를 먼저 쓴 뒤 개수를 release 로 발행하므로, 동시에 읽는 스레드는
         *          범위 안이라고 판단한 인덱스에 대해 항상 유효한 청크를 보게 됩니다.
         */
        uint32 pushBack( T value )
        {
            const uint32 index    = _count.load( std::memory_order_relaxed );
            const uint32 chunkIdx = index / kElementPerChunk;
            if ( chunkIdx >= kMaxChunk )
                return kInvalidIndex;

            if ( _arrChunk[chunkIdx] == nullptr )
            {
                _arrChunk[chunkIdx] = new T[kElementPerChunk]{};
                if ( _arrChunk[chunkIdx] == nullptr )
                    return kInvalidIndex;
            }
            _arrChunk[chunkIdx][index % kElementPerChunk] = std::move( value );

            // 원소와 청크 포인터를 모두 쓴 뒤에 개수를 발행한다 — 읽기 쪽의 acquire 와 짝을 이룬다.
            _count.store( index + 1, std::memory_order_release );
            return index;
        }

        /** @brief 모든 원소를 버립니다. 다른 스레드가 읽는 중이면 호출하면 안 됩니다. */
        void clear()
        {
            _count.store( 0, std::memory_order_release );
            releaseChunks();
        }

        /** @brief 발행 범위의 모든 원소에 fn(T&)를 호출합니다. */
        template <typename Fn>
        void forEach( Fn&& fn )
        {
            const uint32 count = size();
            for ( uint32 index = 0; index < count; ++index )
            {
                fn( _arrChunk[index / kElementPerChunk][index % kElementPerChunk] );
            }
        }

        /** @brief 인덱스로 접근할 수 없음을 나타내는 값입니다. */
        static constexpr uint32 kInvalidIndex = ~0u;

    private:
        /** @brief 할당된 청크를 모두 해제하고 포인터를 비웁니다. */
        void releaseChunks()
        {
            for ( T*& pChunk : _arrChunk )
            {
                delete[] pChunk;
                pChunk = nullptr;
            }
        }

        /** @brief 청크 포인터 배열 — 한 번 쓰인 원소 주소는 끝까지 변하지 않습니다. */
        static constexpr uint32 kMaxChunk = 4096;

        T*             _arrChunk[kMaxChunk]{};
        atomic<uint32> _count{ 0 };
    };
} // namespace sw
