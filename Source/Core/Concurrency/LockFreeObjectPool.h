/**
 * @file DataStructure/LockFreeObjectPool.h
 * @brief 뮤텍스를 사용하지 않고 원자적 락-프리 큐를 기반으로 동작하는 객체 풀(Object Pool) 선언입니다.
 * @details 렌더링 스레드와 워커 스레드 간의 빈번한 객체 생성/해제 시 병목을 줄이기 위해 설계되었습니다.
 *
 * 내부 큐로 sw::ConcurrentQueue(다중 생산자·다중 소비자)를 사용합니다.
 *   - acquire(): 여러 스레드(소비자)가 동시에 호출 가능
 *   - release(): 여러 스레드(생산자)가 동시에 호출 가능
 *
 * 소유권 모델:
 *   - acquire()  → T* 반환 (nullptr이면 풀 소진)
 *   - release(T*&) → 소멸자 호출 후 풀 반납, 포인터를 nullptr로 초기화
 *     * 포인터를 참조로 받아 반납 후 자동으로 비움 → 소멸 후 사용 방지
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/ConcurrentQueue.h"
#include "Core/Container/array.h"
#include "Core/Log/Logger.h"

namespace sw
{
    /**
     * @class LockFreeObjectPool
     * @brief 템플릿 기반 MPMC 락-프리 오브젝트 풀입니다. 최대 Capacity개까지의 객체를 스레드 안전하게 재사용합니다.
     * @tparam T 풀링할 객체 타입
     * @tparam Capacity 풀에 사전 할당할 최대 객체 수 (2의 거듭제곱 권장 — ConcurrentQueue 요구사항)
     */
    // ------------------------------------------------------------------------------
    // 1) LockFreeObjectPool — acquire(placement new) / release(~T + 큐 반납)
    //    Capacity 는 2의 거듭제곱. 소진 시 acquire 는 nullptr
    // ------------------------------------------------------------------------------
    template <typename T, uint32 Capacity = 512>
    /** @brief MPMC 락프리 오브젝트 풀. 스토리지는 고정 배열입니다. */
    class LockFreeObjectPool
    {
        static_assert( ( Capacity & ( Capacity - 1 ) ) == 0,
                       "LockFreeObjectPool Capacity must be a power of 2 (ConcurrentQueue requirement)" );

    public:
        /**
         * @brief 자리가 나기를 기다릴 때, 순수 yield 로 버티는 횟수입니다. 이후로는 짧게 재웁니다.
         * @details 기다림의 끝은 **횟수가 아니라 시간**이다(아래). 이 값은 "CPU 를 태우며 기다릴
         *          구간" 의 길이일 뿐이라 정확할 필요가 없다.
         */
        static constexpr uint32 kReleaseYieldAttempt = 256;

        /**
         * @brief 자리가 나기를 기다리는 최대 **시간**(밀리초).
         * @details **횟수로 끊으면 안 된다.** 앞선 판은 1024 번 시도하고 포기했는데, 코어가 적은
         *          CI 러너에서 진짜 반납이 그 단언에 걸려 프로세스가 죽었다 — `yield()` 1024 번은
         *          마이크로초만에 끝나지만, 칸을 집어간 소비자가 순번을 공개하기 전에 선점되면
         *          그 창은 스케줄러 퀀텀(밀리초) 동안 열려 있다. 시간으로 끊으면 "느린 기계" 와
         *          "정말 자리가 없다" 가 갈린다. 여기 걸리는 것은 버그 경로뿐이라 넉넉히 준다.
         */
        static constexpr uint32 kReleaseWaitMilli = 1000;

        /** @brief 스토리지 슬롯을 모두 유휴 큐에 넣습니다. */
        LockFreeObjectPool()
        {
            for ( uint32 poolIndex = 0; poolIndex < Capacity; ++poolIndex )
            {
                T* pPtr = reinterpret_cast<T*>( &_arrStorage[poolIndex * sizeof( T )] );
                _freeQueue.enqueue( pPtr );
            }
        }

        /**
         * @brief 풀 소멸 전 모든 객체가 반납됐는지 검증합니다.
         * @warning SW_DEBUG 빌드에서 누수 조기 발견용 assert를 수행합니다.
         *          acquire된 채 소멸하면 T 소멸자가 호출되지 않습니다.
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
         * @brief 풀에서 유휴(Free) 객체 하나를 가져와 생성자(Placement New)를 호출합니다.
         * @tparam Args 생성자에 전달할 가변 인자 타입들
         * @param args 생성자에 전달할 가변 인자 리스트
         * @return 사용 가능한 객체 포인터 (풀이 가득 찼다면 nullptr 반환)
         */
        template <typename... Args>
        SW_INLINE T* acquire( Args&&... args )
        {
            T* pPtr{ nullptr };
            if ( _freeQueue.dequeue( pPtr ) && pPtr != nullptr )
            {
                new ( pPtr ) T( std::forward<Args>( args )... );
                _activeCount.fetch_add( 1, std::memory_order_relaxed );
                return pPtr;
            }
            return nullptr;
        }

        /**
         * @brief 이 풀의 스토리지에서 나온 포인터인지 봅니다 (반납 전에 가려내는 용도).
         * @details 범위와 간격으로 판정하므로 **기다릴 필요가 없다.** 예전에는 남의 포인터도
         *          "자리가 안 난다" 는 증상으로 알아냈는데, 그 증상은 경합 중인 정상 반납과
         *          구별되지 않는다 — 같은 단언이 양쪽에 붙어 있었다.
         * @note 이미 반납된 포인터는 여기서 못 가린다(범위 안이다). 그쪽은 시간으로 가른다.
         */
        SW_INLINE bool owns( const T* pPtr ) const
        {
            if ( pPtr == nullptr )
                return false;
            const uintptr_t base  = reinterpret_cast<uintptr_t>( _arrStorage.data() );
            const uintptr_t value = reinterpret_cast<uintptr_t>( pPtr );
            if ( value < base || value >= base + static_cast<uintptr_t>( Capacity ) * sizeof( T ) )
                return false;
            // 슬롯 경계에 정확히 걸려야 한다 — 블록 중간을 가리키는 포인터는 이 풀의 것이 아니다.
            return ( ( value - base ) % sizeof( T ) ) == 0;
        }

        /**
         * @brief 객체 소멸자를 호출하고 풀에 메모리를 반납합니다. 포인터를 nullptr로 초기화합니다.
         * @param pPtr 반납할 객체 포인터 (참조로 받아 반납 후 nullptr로 만듦)
         * @note release 후 pPtr은 반드시 nullptr이 됩니다. 소멸 후 사용을 방지합니다.
         */
        SW_INLINE void release( T*& pPtr )
        {
            if ( pPtr == nullptr )
                return;

            // 남의 포인터는 기다릴 이유가 없다 — 즉시, 정확히 가린다.
            if ( owns( pPtr ) == false )
            {
                SW_LOG_ASSERT( false, "LockFreeObjectPool::release: the pointer is not from this pool's storage." );
                pPtr = nullptr;
                return;
            }

            pPtr->~T();

            // **"가득 찼다" 는 답이 곧 이중 반납은 아니다.** 내부 MPMC 큐(Vyukov)는 소비자가 칸을
            // 집어간 뒤 그 칸의 **순번을 아직 공개하지 않은 찰나**에도 생산자에게 가득 찼다고
            // 답한다. 자리 수(`Capacity`)와 블록 수가 같으므로 그 찰나는 반드시 지나간다 —
            // 그러니 한 번의 실패로 물러서면 안 된다. 물러서면 그 블록이 자유 목록으로 돌아가지
            // 못해 **풀이 조용히 줄어들고**, 오래 돌수록 `acquire` 가 더 자주 널을 돌려준다.
            //
            // 여기까지 온 포인터는 이 풀의 블록이다. 그러니 자리는 **반드시** 난다 — 이미 반납된
            // 것만 아니라면. 그 하나를 가르는 기준이 시간이다(kReleaseWaitMilli).
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

                // **반납이 실패하면 세지 않는다.** `_activeCount` 를 줄이면 0 에서 뒤집혀
                // 40억이 되고, 소멸자의 "다 반납됐나" 단언이 엉뚱한 말을 하게 된다.
                SW_LOG_ASSERT( false, "LockFreeObjectPool::release: free queue stayed full for the whole wait — this block was already released." );
                pPtr = nullptr;
                return;
            }

            _activeCount.fetch_sub( 1, std::memory_order_relaxed );
            pPtr = nullptr; // 호출자가 실수로 사용하지 못하도록 즉시 무효화
        }

        /** @brief 현재 acquire된 객체 수를 반환합니다. */
        SW_INLINE uint32 getActiveCount() const { return _activeCount.load( std::memory_order_relaxed ); }

        /** @brief 풀에 남은 유휴 객체 수를 반환합니다. */
        SW_INLINE uint32 getAvailableCount() const
        {
            uint32 active = getActiveCount();
            return Capacity >= active ? ( Capacity - active ) : 0;
        }

        /** @brief 현재 용량을 반환합니다. */
        constexpr uint32 capacity() const { return Capacity; }

    private:
        alignas( alignof( T ) ) std::array<uint8, Capacity * sizeof( T )> _arrStorage;
        ConcurrentQueue<T*, Capacity> _freeQueue; ///< 다중 생산자·다중 소비자 — acquire/release 스레드 안전
        atomic<uint32>                _activeCount{ 0 };
    };
} // namespace sw
