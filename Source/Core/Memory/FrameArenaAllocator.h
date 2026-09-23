/**
 * @file FrameArenaAllocator.h
 * @brief 프레임 아레나 할당자와 게임 스레드 · 렌더 스레드용 더블 버퍼입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) FrameArenaAllocator — 청크 안에서 선형으로 할당하고, 프레임 끝의 reset 은 O(1)
    //    실제로 해제하지 않고 오프셋만 되돌린다. 스코프 단위 되돌리기는 Marker
    // ------------------------------------------------------------------------------
    /**
     * @class FrameArenaAllocator
     * @brief 청크 기반 선형 할당자입니다. 프레임 끝에 오프셋을 한 번에 되돌려 동적 할당 비용을 줄입니다.
     */
    class SW_API FrameArenaAllocator
    {
    public:
        /**
         * @brief 첫 청크를 잡고 할당자를 준비합니다.
         * @param defaultCapacity 초기 청크 크기(바이트)
         */
        explicit FrameArenaAllocator( size_t defaultCapacity = constant::kDefaultFrameArenaCapacity );
        /** @brief 소유한 청크 버퍼를 해제합니다. */
        ~FrameArenaAllocator();

        /** @brief 복사를 금지합니다. */
        FrameArenaAllocator( const FrameArenaAllocator& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        FrameArenaAllocator& operator=( const FrameArenaAllocator& ) = delete;

        /**
         * @brief 지정한 크기 · 정렬로 현재 청크에서 잘라 냅니다. 모자라면 새 청크를 붙입니다.
         * @param size 할당할 바이트 수
         * @param alignment 정렬 기준(2의 거듭제곱)
         * @return 할당된 주소
         */
        SW_INLINE void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) )
        {
            if ( size == 0 )
                return nullptr;

            if ( alignment == 0 || MathUtil::isPowerOfTwo( alignment ) == false )
                alignment = alignof( std::max_align_t );

            if ( _currentChunkIndex < _listChunk.size() )
            {
                Chunk&          chunk   = _listChunk[_currentChunkIndex];
                const uintptr_t current = reinterpret_cast<uintptr_t>( chunk._pBuffer + chunk._offset );
                const uintptr_t aligned = MathUtil::align( current, static_cast<uintptr_t>( alignment ) );
                const size_t    padding = static_cast<size_t>( aligned - current );

                if ( fitsInChunk( chunk._capacity, chunk._offset, padding, size ) )
                {
                    chunk._offset += padding + size;
                    _usedBytes += padding + size;
                    return reinterpret_cast<void*>( aligned );
                }
            }

            return allocateSlow( size, alignment );
        }

        /**
         * @brief 아레나에 객체를 생성합니다(placement new).
         */
        template <typename T, typename... Args>
        T* construct( Args&&... args )
        {
            static_assert( std::is_trivially_destructible_v<T>,
                           "FrameArenaAllocator only supports Trivially Destructible types (destructors are not invoked on reset)!" );
            void* pMem = allocate( sizeof( T ), alignof( T ) );
            if constexpr ( std::is_aggregate_v<T> )
                return sw_placement_new( pMem ) T{ std::forward<Args>( args )... };
            else
                return sw_placement_new( pMem ) T( std::forward<Args>( args )... );
        }

        /**
         * @brief 오프셋을 0 으로 되돌립니다. 청크 메모리는 그대로 둡니다.
         */
        void reset();

        /**
         * @struct Marker
         * @brief createMarker / rollbackToMarker 에 쓰는 청크 · 오프셋 스냅샷입니다.
         */
        struct Marker
        {
            size_t _chunkIndex{ 0 };
            size_t _offset{ 0 };
            size_t _usedBytes{ 0 };
        };

        /**
         * @brief 현재 청크 인덱스 · 오프셋 · 사용량을 캡처합니다.
         */
        Marker createMarker() const;

        /**
         * @brief 마커를 찍은 뒤의 할당을 모두 되돌립니다.
         * @param marker createMarker() 로 찍은 상태
         */
        void rollbackToMarker( const Marker& marker );

        /** @brief 지금까지 확보한 청크 용량의 합(바이트)입니다. */
        size_t getTotalAllocatedBytes() const { return _totalAllocatedBytes; }
        /** @brief 현재 프레임에서 잘라 쓴 바이트 수입니다. */
        size_t getUsedBytes() const { return _usedBytes; }

    private:
        /** @brief 청크 하나의 버퍼 · 용량 · 쓰기 오프셋입니다. */
        struct Chunk
        {
            uint8* _pBuffer{ nullptr };
            size_t _capacity{ 0 };
            size_t _offset{ 0 };
        };

        /**
         * @brief `offset + padding + size` 가 capacity 안에 드는지 **오버플로 없이** 확인합니다.
         * @details 덧셈으로 쓰면 큰 `size` 에서 합이 오버플로해 검사를 통과하고, **청크 밖을 가리키는 주소**가 정상 할당인 것처럼
         *          반환됩니다. `offset <= capacity` 는 이 클래스의 불변 조건이지만 여기서도 확인합니다. 비용이 없고, 그 조건이
         *          깨졌을 때 뺄셈이 언더플로하기 때문입니다.
         */
        static bool fitsInChunk( size_t capacity, size_t offset, size_t padding, size_t size )
        {
            if ( offset > capacity )
                return false;
            const size_t remaining = capacity - offset;
            if ( padding > remaining )
                return false;
            return size <= remaining - padding;
        }

        /**
         * @brief minSize 이상을 담는 새 청크를 할당하고 현재 청크로 바꿉니다.
         * @return 할당에 실패하면 false 이고, 그때 청크 표에는 **아무것도 넣지 않습니다.**
         */
        bool allocateNewChunk( size_t minSize );

        /**
         * @brief 청크 용량이 모자랄 때 다음 청크를 찾거나 새로 할당하는 느린 경로입니다.
         */
        void* allocateSlow( size_t size, size_t alignment );

        size_t        _defaultCapacity;
        size_t        _totalAllocatedBytes;
        size_t        _usedBytes;
        size_t        _currentChunkIndex;
        vector<Chunk> _listChunk;
    };

    // ------------------------------------------------------------------------------
    // 2) FrameDoubleBuffer — 게임 스레드가 N 번째를 쓰는 동안 렌더 스레드가 N-1 번째를 읽는다
    //    swapAndResetPrevious 가 인덱스를 바꾸고, 새로 활성이 된 쪽만 reset 한다
    // ------------------------------------------------------------------------------
    class FrameDoubleBuffer
    {
    public:
        /** @brief 양쪽 아레나를 같은 용량으로 준비합니다. */
        explicit FrameDoubleBuffer( size_t arenaCapacity = constant::kDefaultFrameArenaCapacity )
            : _arrArena{ FrameArenaAllocator{ arenaCapacity }, FrameArenaAllocator{ arenaCapacity } }
            , _activeBufferIndex{ 0 } {}

        /** @brief 양쪽 아레나의 청크를 해제합니다. */
        ~FrameDoubleBuffer() = default;

        /** @brief 활성 아레나에서 정렬해 할당합니다. */
        SW_INLINE void* allocate( size_t size, size_t alignment = alignof( std::max_align_t ) )
        {
            const uint32 activeIdx = _activeBufferIndex.load( std::memory_order_relaxed );
            return _arrArena[activeIdx].allocate( size, alignment );
        }

        /** @brief 활성 인덱스를 바꾸고, 새로 활성이 된 쪽을 reset 합니다. */
        SW_INLINE void swapAndResetPrevious()
        {
            const uint32 newIdx = 1 - _activeBufferIndex.load( std::memory_order_relaxed );
            _arrArena[newIdx].reset();
            _activeBufferIndex.store( newIdx, std::memory_order_release );
        }

        /** @brief 지금 쓰는 버퍼 인덱스(0 또는 1)입니다. */
        SW_INLINE uint32 getCurrentIndex() const { return _activeBufferIndex.load( std::memory_order_acquire ); }
        /** @brief 활성 아레나의 사용 바이트 수입니다. */
        SW_INLINE size_t getCurrentUsedBytes() const
        {
            const uint32 activeIdx = _activeBufferIndex.load( std::memory_order_relaxed );
            return _arrArena[activeIdx].getUsedBytes();
        }

    private:
        FrameArenaAllocator _arrArena[2];
        atomic<uint32>      _activeBufferIndex;
    };
} // namespace sw
