/**
 * @file LockFreeObjectPool.h
 * @brief 뮤텍스 없이 lock-free 큐 위에서 동작하는 객체 풀입니다.
 * @details 렌더 스레드와 워커 스레드가 객체를 자주 만들고 지울 때 생기는 병목을 줄이려고 만들었습니다.
 *
 * 내부 큐로 sw::ConcurrentQueue(다중 생산자 · 다중 소비자)를 씁니다.
 *   - acquire(): 여러 스레드(소비자)가 동시에 불러도 됩니다
 *   - release(): 여러 스레드(생산자)가 동시에 불러도 됩니다
 *
 * 소유권 모델:
 *   - acquire()    → T* 를 반환합니다(nullptr 이면 풀이 바닥났습니다)
 *   - release(T*&) → 소멸자를 부르고 풀에 돌려준 뒤, 포인터를 nullptr 로 만듭니다
 *     * 포인터를 참조로 받아 반납 직후 비우므로, 해제한 객체를 다시 쓰는 실수를 막습니다
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Container/array.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class LockFreeObjectPool
     * @brief MPMC lock-free 객체 풀입니다. 최대 Capacity 개의 객체를 스레드 안전하게 재사용합니다.
     * @tparam T 풀링할 객체 타입
     * @tparam Capacity 풀에 미리 잡아 둘 최대 객체 수(ConcurrentQueue 의 요구 사항이라 2의 거듭제곱이어야 합니다)
     */
    // ------------------------------------------------------------------------------
    // 1) LockFreeObjectPool — acquire(placement new) / release(~T 뒤 큐에 반납)
    //    Capacity 는 2의 거듭제곱. 바닥나면 acquire 는 nullptr
    // ------------------------------------------------------------------------------
    template <typename T, uint32 Capacity = 512>
    /** @brief MPMC lock-free 객체 풀입니다. 저장 공간은 고정 배열입니다. */
    class LockFreeObjectPool
    {
        static_assert( ( Capacity & ( Capacity - 1 ) ) == 0,
                       "LockFreeObjectPool Capacity must be a power of 2 (ConcurrentQueue requirement)" );

    public:
        /**
         * @brief 자리가 나기를 기다릴 때 yield 만으로 버티는 횟수입니다. 그 뒤로는 짧게 잠듭니다.
         * @details 기다림을 끝내는 기준은 **횟수가 아니라 시간**입니다(아래 참고). 이 값은 "CPU 를 쓰면서 기다릴 구간" 의 길이일
         *          뿐이라 정확할 필요가 없습니다.
         */
        static constexpr uint32 kReleaseYieldAttempt = 256;

        /**
         * @brief 자리가 나기를 기다리는 최대 **시간**(밀리초)입니다.
         * @details **횟수로 끊으면 안 됩니다.** 이전 구현은 1024번 시도한 뒤 포기했는데, 코어가 적은 CI 러너에서 정상적인 반납이
         *          그 단언에 걸려 프로세스가 죽었습니다. `yield()` 1024번은 몇 마이크로초면 끝나지만, 칸을 가져간 소비자가 순번을
         *          공개하기 전에 선점되면 그 틈은 스케줄러 퀀텀(밀리초 단위) 동안 열려 있습니다. 시간으로 끊어야 "기계가 느린 것" 과
         *          "정말 자리가 없는 것" 을 구별할 수 있습니다. 여기에 걸리는 것은 버그 경로뿐이라 넉넉하게 줍니다.
         */
        static constexpr uint32 kReleaseWaitMilli = 1000;

        /** @brief 저장 공간의 슬롯을 모두 유휴 큐에 넣습니다. */
        LockFreeObjectPool()
        {
            for ( uint32 poolIndex = 0; poolIndex < Capacity; ++poolIndex )
            {
                T* pPtr = reinterpret_cast<T*>( &_arrStorage[poolIndex * sizeof( T )] );
                _freeQueue.enqueue( pPtr );
            }
        }

        /**
         * @brief 풀이 파괴되기 전에 모든 객체가 반납됐는지 확인합니다.
         * @warning SW_DEBUG 빌드에서는 누수를 일찍 찾도록 assert 합니다. acquire 한 채로 풀이 파괴되면 그 객체의 T 소멸자는 불리지 않습니다.
         */
        ~LockFreeObjectPool()
        {
            SW_ASSERT( getActiveCount() == 0 &&
                       "[LockFreeObjectPool] 아직 acquire된 객체가 있습니다. "
                       "풀 소멸 전에 모든 객체를 release 하세요." );
        }

        /** @brief 복사를 금지합니다. */
        LockFreeObjectPool( const LockFreeObjectPool& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        LockFreeObjectPool& operator=( const LockFreeObjectPool& ) = delete;

        /**
         * @brief 풀에서 빈 객체 하나를 가져와 생성자를 부릅니다(placement new).
         * @tparam Args 생성자에 넘길 인자 타입들
         * @param args 생성자에 넘길 인자들
         * @return 쓸 수 있는 객체 포인터. 풀이 바닥났으면 nullptr 입니다.
         */
        template <typename... Args>
        SW_INLINE T* acquire( Args&&... args )
        {
            T* pPtr{ nullptr };
            if ( _freeQueue.dequeue( pPtr ) && pPtr != nullptr )
            {
                sw_placement_new( pPtr ) T( std::forward<Args>( args )... );
                _activeCount.fetch_add( 1, std::memory_order_relaxed );
                return pPtr;
            }
            return nullptr;
        }

        /**
         * @brief 이 풀의 저장 공간에서 나온 포인터인지 확인합니다(반납하기 전에 걸러 내는 용도).
         * @details 주소 범위와 슬롯 간격으로 판정하므로 기다릴 필요가 없습니다. 예전에는 다른 풀의 포인터도 "자리가 나지 않는다" 는
         *          증상으로 알아냈는데, 그 증상은 경합 중인 정상 반납과 구별되지 않았습니다. 같은 단언이 양쪽에 걸려 있었던 것입니다.
         * @note 이미 반납된 포인터는 여기서 가려내지 못합니다(범위 안에 있기 때문입니다). 그쪽은 대기 시간으로 가려냅니다.
         */
        SW_INLINE bool owns( const T* pPtr ) const
        {
            if ( pPtr == nullptr )
                return false;
            const uintptr_t base  = reinterpret_cast<uintptr_t>( _arrStorage.data() );
            const uintptr_t value = reinterpret_cast<uintptr_t>( pPtr );
            if ( value < base || value >= base + static_cast<uintptr_t>( Capacity ) * sizeof( T ) )
                return false;
            // 슬롯 경계에 정확히 맞아야 한다. 블록 중간을 가리키는 포인터는 이 풀의 것이 아니다.
            return ( ( value - base ) % sizeof( T ) ) == 0;
        }

        /**
         * @brief 객체의 소멸자를 부르고 메모리를 풀에 돌려준 뒤, 포인터를 nullptr 로 만듭니다.
         * @param pPtr 반납할 객체 포인터(참조로 받아 반납 뒤 nullptr 로 만듭니다)
         * @note release 뒤 pPtr 은 반드시 nullptr 입니다. 해제한 객체를 다시 쓰는 실수를 막습니다.
         */
        SW_INLINE void release( T*& pPtr )
        {
            if ( pPtr == nullptr )
                return;

            // 다른 풀의 포인터라면 기다릴 이유가 없다. 바로, 정확하게 걸러 낸다.
            if ( owns( pPtr ) == false )
            {
                SW_LOG_ASSERT( false, "LockFreeObjectPool::release: the pointer is not from this pool's storage." );
                pPtr = nullptr;
                return;
            }

            pPtr->~T();

            // "가득 찼다" 는 응답이 곧 이중 반납을 뜻하지는 않는다. 내부 MPMC 큐(Vyukov)는 소비자가 칸을 가져간 뒤 그 칸의 순번을
            // 아직 공개하지 않은 짧은 순간에도 생산자에게 가득 찼다고 응답한다. 자리 수(`Capacity`)와 블록 수가 같으므로 그 순간은
            // 반드시 지나간다. 그러니 한 번 실패했다고 물러서면 안 된다. 물러서면 그 블록이 자유 목록으로 돌아가지 못해 풀이 조용히
            // 줄어들고, 오래 돌수록 `acquire` 가 nullptr 을 더 자주 반환한다.
            //
            // 여기까지 온 포인터는 이 풀의 블록이므로, 이미 반납된 것만 아니라면 자리는 반드시 난다. 그 하나를 가르는 기준이
            // 시간(kReleaseWaitMilli)이다.
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds( kReleaseWaitMilli );
            uint32 attemptCount = 0;
            while ( _freeQueue.enqueue( pPtr ) == false )
            {
                ++attemptCount;
                if ( attemptCount <= kReleaseYieldAttempt )
                    std::this_thread::yield();
                else
                    std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );

                if ( std::chrono::steady_clock::now() < deadline )
                    continue;

                // 반납에 실패하면 카운트를 줄이지 않는다. `_activeCount` 를 줄이면 0 에서 언더플로가 나 40억이 되고,
                // 소멸자의 "모두 반납됐는가" 단언이 엉뚱한 결과를 낸다.
                SW_LOG_ASSERT( false, "LockFreeObjectPool::release: free queue stayed full for the whole wait — this block was already released." );
                pPtr = nullptr;
                return;
            }

            _activeCount.fetch_sub( 1, std::memory_order_relaxed );
            pPtr = nullptr; // 호출한 쪽이 실수로 다시 쓰지 못하게 바로 무효화한다
        }

        /** @brief 지금 acquire 된 객체 수를 반환합니다. */
        SW_INLINE uint32 getActiveCount() const { return _activeCount.load( std::memory_order_relaxed ); }

        /** @brief 풀에 남은 빈 객체 수를 반환합니다. */
        SW_INLINE uint32 getAvailableCount() const
        {
            uint32 active = getActiveCount();
            return Capacity >= active ? ( Capacity - active ) : 0;
        }

        /** @brief 용량을 반환합니다. */
        constexpr uint32 capacity() const { return Capacity; }

    private:
        alignas( alignof( T ) ) std::array<uint8, Capacity * sizeof( T )> _arrStorage;
        ConcurrentQueue<T*, Capacity> _freeQueue; ///< 다중 생산자 · 다중 소비자. acquire/release 를 스레드 안전하게 만든다
        atomic<uint32>                _activeCount{ 0 };
    };
} // namespace sw
