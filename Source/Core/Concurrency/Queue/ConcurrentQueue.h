/**
 * @file ConcurrentQueue.h
 * @brief 고정 용량 MPMC lock-free 큐입니다(Vyukov 방식의 시퀀스 번호 링).
 * @note 뮤텍스를 쓰지 않습니다. 이 큐는 막히지 않는 대신, **가득 차면 `enqueue` 가 false 를
 *       반환합니다**(그 처리는 호출하는 쪽의 책임입니다).
 * @details 용량은 둘 중 하나로 정한다 — 컴파일 상수(`ConcurrentQueue<T, 1024>`, 버퍼가 객체 안)이거나, 상한이 설정으로 정해지는 자리는
 *          `ConcurrentQueue<T, kRuntimeQueueCapacity>` 를 만들 때 개수를 넘긴다(버퍼는 sw 할당자, 2 의 거듭제곱으로 올림). 알고리즘은 같다.
 *          주의(Vyukov 링의 성질): 꺼내는 쪽이 칸을 예약하고 시퀀스를 되돌리기 전에 선점되면, 그 칸을 다시 쓰려는 `enqueue` 는 남은 수가
 *          용량보다 적어도 false 이고, 앞 칸을 예약만 한 생산자가 있으면 `dequeue` 는 뒤 칸이 차 있어도 false 다 — 둘 다 잠깐이다.
 *          "가득 참이 생길 수 없는" 자리는 `size() < capacity()` 인 false 를 잠깐으로 보고 다시 시도한다(`JoltJobSystem`).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

// `popAll( vector<T>& )` 가 쓴다. 본문에 나오는 `sw::array` 는 주석 속 언급뿐이라 array.h 는 넣지 않는다.
// 주의: 토큰만 세고 include 를 지우면 이 헤더가 혼자서는 컴파일되지 않을 수 있다(단독 컴파일로 확인할 것).

namespace sw
{
    /** @brief `ConcurrentQueue` 의 용량 인자로 주면 용량을 만들 때 받습니다(`std::dynamic_extent` 와 같은 자리). */
    inline constexpr uint32 kRuntimeQueueCapacity = 0;

    namespace ConcurrentQueueInternal
    {
        /** @brief 캐시 라인에 정렬된 슬롯입니다. 시퀀스 번호로 소유권을 넘깁니다. */
        template <typename T>
        struct alignas( 64 ) Cell
        {
            atomic<uint32> _sequence{ 0 };
            T              _data{};
        };

        /** @brief 컴파일 상수 용량의 버퍼입니다(객체 안). */
        template <typename T, uint32 Capacity>
        class Storage
        {
        protected:
            Storage() = default;

            Cell<T>*                getCells() { return _arrBuffer.data(); }
            static constexpr uint32 getMask() { return Capacity - 1; }

        private:
            // 여러 스레드가 이 버퍼를 일부러 동시에 만진다(슬롯마다 원자적 sequence 로 동기화한다).
            // 그래서 레이스 탐지기가 붙은 sw::array 를 쓰지 않는다. 이유는 Core/Container/array.h 머리말에 있다.
            std::array<Cell<T>, Capacity> _arrBuffer{};
        };

        /** @brief 만들 때 받은 용량의 버퍼입니다(sw 할당자, 2 의 거듭제곱으로 올림). 레이스 탐지기가 붙은 `vector` 를 쓰지 않는 까닭은 위와 같다. */
        template <typename T>
        class Storage<T, kRuntimeQueueCapacity>
        {
        protected:
            explicit Storage( uint32 requestedCapacity )
                : _pCells{ nullptr }
                , _mask{ 0 }
            {
                uint32 capacity = 2;
                while ( capacity < requestedCapacity && capacity < ( 1u << 31 ) )
                {
                    capacity <<= 1;
                }
                _pCells = sw_new_array_func<Cell<T>>( capacity );
                _mask   = capacity - 1;
            }

            ~Storage() { sw_delete_array_func( _pCells, static_cast<size_t>( _mask ) + 1 ); }

            Storage( const Storage& )            = delete;
            Storage& operator=( const Storage& ) = delete;

            Cell<T>* getCells() { return _pCells; }
            uint32   getMask() const { return _mask; }

        private:
            Cell<T>* _pCells;
            uint32   _mask;
        };
    } // namespace ConcurrentQueueInternal

    /**
     * @brief 고정 용량 다중 생산자 · 다중 소비자 큐입니다(시퀀스 번호 기반).
     * @tparam T 요소 타입
     * @tparam Capacity 용량(2의 거듭제곱). `kRuntimeQueueCapacity` 면 만들 때 받는다
     */
    // ------------------------------------------------------------------------------
    // 1) ConcurrentQueue — MPMC 시퀀스 번호 링. 가득 차거나 비었으면 enqueue/dequeue 가 false
    // ------------------------------------------------------------------------------
    /** @brief 고정 용량 다중 생산자 · 다중 소비자 큐입니다. */
    template <typename T, uint32 Capacity = 1024>
    class ConcurrentQueue : private ConcurrentQueueInternal::Storage<T, Capacity>
    {
        static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be a power of 2!" );

        using Cell        = ConcurrentQueueInternal::Cell<T>;
        using StorageBase = ConcurrentQueueInternal::Storage<T, Capacity>;

    public:
        /** @brief 컴파일 상수 용량 — 슬롯마다 시퀀스를 자기 인덱스로 두고, 위치를 0 으로 맞춥니다. */
        template <uint32 kCapacity = Capacity, std::enable_if_t<kCapacity != kRuntimeQueueCapacity, int> = 0>
        ConcurrentQueue()
            : StorageBase{}
        {
            initializeSlots();
        }

        /** @brief 실행 때 용량 — @p capacity 이상인 2 의 거듭제곱(최소 2)으로 버퍼를 잡고, 슬롯 시퀀스와 위치를 맞춥니다. */
        template <uint32 kCapacity = Capacity, std::enable_if_t<kCapacity == kRuntimeQueueCapacity, int> = 0>
        explicit ConcurrentQueue( uint32 capacity )
            : StorageBase{ capacity }
        {
            initializeSlots();
        }

        /** @brief 버퍼를 그대로 버립니다(잠금 없음). */
        ~ConcurrentQueue() = default;

        /** @brief 복사해서 넣습니다. 가득 차 있으면 false 입니다. */
        SW_INLINE bool enqueue( const T& item )
        {
            uint32 pos{ 0 };
            Cell*  pCell = reserveEnqueueCell( pos );
            if ( pCell == nullptr )
                return false;
            pCell->_data = item;
            pCell->_sequence.store( pos + 1, std::memory_order_release );
            return true;
        }

        /** @brief 이동해서 넣습니다. 가득 차 있으면 false 입니다. */
        SW_INLINE bool enqueue( T&& item )
        {
            uint32 pos{ 0 };
            Cell*  pCell = reserveEnqueueCell( pos );
            if ( pCell == nullptr )
                return false;
            pCell->_data = std::move( item );
            pCell->_sequence.store( pos + 1, std::memory_order_release );
            return true;
        }

        /** @brief 맨 앞 요소를 꺼냅니다. 비어 있으면 false 입니다. */
        SW_INLINE bool dequeue( T& outItem )
        {
            Cell*  pCell{ nullptr };
            uint32 pos = _dequeuePos.load( std::memory_order_relaxed );

            for ( ;; )
            {
                pCell            = &this->getCells()[pos & this->getMask()];
                uint32      seq  = pCell->_sequence.load( std::memory_order_acquire );
                const int32 diff = static_cast<int32>( seq - ( pos + 1 ) );

                if ( diff == 0 )
                {
                    if ( _dequeuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
                        break;
                }
                else if ( diff < 0 )
                    return false;
                else
                    pos = _dequeuePos.load( std::memory_order_relaxed );
            }

            outItem = std::move( pCell->_data );
            pCell->_sequence.store( pos + this->getMask() + 1, std::memory_order_release );
            return true;
        }

        /** @brief 대기 중인 항목을 모두 꺼내 outList 뒤에 붙이고, 꺼낸 개수를 반환합니다. */
        uint32 popAll( vector<T>& outList )
        {
            uint32 popCount = 0;
            T      item{};
            while ( dequeue( item ) )
            {
                outList.push_back( std::move( item ) );
                ++popCount;
            }
            return popCount;
        }

        /** @brief 대기 중인 항목을 최대 maxCount 개까지 꺼내 버퍼에 담고, 꺼낸 개수를 반환합니다. */
        uint32 popAll( T* pOutBuffer, uint32 maxCount )
        {
            if ( pOutBuffer == nullptr || maxCount == 0 )
                return 0;

            uint32 popCount = 0;
            while ( popCount < maxCount )
            {
                if ( dequeue( pOutBuffer[popCount] ) == false )
                    break;
                ++popCount;
            }
            return popCount;
        }

        /** @brief 남은 요소를 모두 꺼내 버립니다. */
        void clear()
        {
            T dummy{};
            while ( dequeue( dummy ) ) {}
        }

        /** @brief 현재 요소 수의 근삿값을 반환합니다. 다른 스레드가 동시에 넣고 빼므로 정확하지 않습니다. */
        SW_INLINE uint32 size() const
        {
            const uint32 head = _dequeuePos.load( std::memory_order_relaxed );
            const uint32 tail = _enqueuePos.load( std::memory_order_relaxed );
            return ( tail - head );
        }

        /** @brief 비어 있는지 반환합니다. */
        SW_INLINE bool empty() const { return size() == 0; }

        /** @brief 용량을 반환합니다(실행 때 용량이면 올린 값). */
        uint32 capacity() const { return this->getMask() + 1; }

    private:
        /** @brief 슬롯마다 시퀀스를 자기 인덱스로 두고, 위치를 0 으로 맞춥니다. */
        void initializeSlots()
        {
            Cell*        pCells    = this->getCells();
            const uint32 cellCount = this->getMask() + 1;
            for ( uint32 slotIndex = 0; slotIndex < cellCount; ++slotIndex )
            {
                pCells[slotIndex]._sequence.store( slotIndex, std::memory_order_relaxed );
            }
            _enqueuePos.store( 0, std::memory_order_relaxed );
            _dequeuePos.store( 0, std::memory_order_relaxed );
        }

        /**
         * @brief 넣을 슬롯 하나를 CAS 로 예약합니다. 가득 차 있으면 nullptr 입니다.
         * @note 예약한 슬롯은 호출한 쪽이 값을 쓴 뒤 `_sequence` 를 `outPos + 1` 로 release 저장해야 소비자에게 보입니다.
         */
        SW_INLINE Cell* reserveEnqueueCell( uint32& outPos )
        {
            uint32 pos = _enqueuePos.load( std::memory_order_relaxed );
            for ( ;; )
            {
                Cell*        pCell = &this->getCells()[pos & this->getMask()];
                const uint32 seq   = pCell->_sequence.load( std::memory_order_acquire );
                const int32  diff  = static_cast<int32>( seq - pos );

                if ( diff == 0 )
                {
                    if ( _enqueuePos.compare_exchange_weak( pos, pos + 1, std::memory_order_relaxed ) )
                    {
                        outPos = pos;
                        return pCell;
                    }
                }
                else if ( diff < 0 )
                    return nullptr;
                else
                    pos = _enqueuePos.load( std::memory_order_relaxed );
            }
        }

    private:
        alignas( 64 ) atomic<uint32> _enqueuePos{ 0 };
        alignas( 64 ) atomic<uint32> _dequeuePos{ 0 };
    };
} // namespace sw
