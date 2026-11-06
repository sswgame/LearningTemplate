/**
 * @file Futex.h
 * @brief 32비트 원자 워드 하나에서 잠들고, 그 주소로 깨우는 대기 원시 연산입니다(Windows `WaitOnAddress` · Linux `futex`).
 * @details 뮤텍스와 조건 변수 조합에서는 깨우는 쪽이 뮤텍스를 잡고, 깨어난 쪽도 그 뮤텍스를 **다시** 잡아야 돌아옵니다.
 *          워커 넷을 한꺼번에 깨우면 넷이 한 뮤텍스 앞에 줄을 서서 차례로 일어납니다(락 컨보이). 여기서는 잠드는 쪽이 자기
 *          워드 하나에서 잠들고 깨우는 쪽은 그 주소만 깨우므로, 잠금이 없고 여러 워커를 깨우는 일이 서로 독립적입니다.
 *          `TaskManager` 의 워커 재우기와 대기자 파킹이 이 위에 있습니다.
 *
 *          약속: `wait( word, expected )` 는 `word == expected` 인 동안만 잠들며, **이유 없이 깨어날 수 있습니다(spurious wakeup).**
 *          그러니 호출하는 쪽은 조건을 다시 확인하는 루프 안에서 써야 합니다. 깨우는 쪽은 워드를 **먼저 바꾼 뒤** 깨웁니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct Futex
     * @brief 원자 워드의 주소로 잠들고 깨웁니다. 워드는 `sw::atomic<uint32>` 하나입니다.
     */
    struct SW_API Futex
    {
        /** @brief @p word 가 @p expected 인 동안 잠듭니다. 값이 다르면 바로 돌아옵니다. 이유 없이 깨어날 수 있습니다. */
        static void wait( atomic<uint32>& word, uint32 expected );

        /**
         * @brief `wait` 과 같지만, @p timeoutMilli 가 지나면 값이 그대로여도 돌아옵니다.
         * @return 값이 바뀌었거나 누군가 깨웠으면 true, 시간이 다 됐으면 false.
         */
        static bool waitFor( atomic<uint32>& word, uint32 expected, uint32 timeoutMilli );

        /** @brief @p word 에서 잠든 스레드 하나를 깨웁니다. 잠든 스레드가 없으면 아무 일도 하지 않습니다. */
        static void wakeOne( atomic<uint32>& word );

        /** @brief @p word 에서 잠든 스레드를 모두 깨웁니다. */
        static void wakeAll( atomic<uint32>& word );
    };
} // namespace sw

namespace sw
{
    /**
     * @class FutexBroadcast
     * @brief 조건 하나를 여럿이 기다리는 대기입니다. 세대 워드 하나에서 잠들고, 조건을 바꾼 쪽이 모두 깨웁니다(C++20 `atomic::wait` · 언리얼 `FEvent` 자리).
     * @details 대기자는 `prepareWait` → 조건 다시 확인 → `commitWait` 또는 `cancelWait` 순으로 쓴다. 수를 먼저 올리고 조건을 보는 대기자와 조건을 바꾸고 수를 보는 `notifyAll` 이
     *          Dekker 식 짝을 이룬다. 잠든 대기자가 없으면 `notifyAll` 은 원자 변수 하나를 읽고 끝나므로 완료 경로에 시스템 콜이 없다.
     */
    class FutexBroadcast
    {
    public:
        FutexBroadcast()
            : _epoch{ 0 }
            , _waiterCount{ 0 }
        {
        }

        /** @brief 잠들 준비를 합니다. 대기자 수를 올리고 세대를 반환합니다. 부르는 쪽은 그 뒤에 조건을 다시 확인합니다. */
        [[nodiscard]] uint32 prepareWait()
        {
            const uint32 epoch = _epoch.load( std::memory_order_acquire );
            _waiterCount.fetch_add( 1, std::memory_order_seq_cst );
            std::atomic_thread_fence( std::memory_order_seq_cst );
            return epoch;
        }

        /** @brief 세대가 @p observedEpoch 인 동안 잠듭니다. @p timeoutMilli 가 0 이면 무제한입니다. 대기자 수는 여기서 내립니다. */
        void commitWait( uint32 observedEpoch, uint32 timeoutMilli )
        {
            if ( timeoutMilli == 0 )
                Futex::wait( _epoch, observedEpoch );
            else
                Futex::waitFor( _epoch, observedEpoch, timeoutMilli );
            _waiterCount.fetch_sub( 1, std::memory_order_seq_cst );
        }

        /** @brief 잠들지 않기로 했을 때 대기자 수만 내립니다. */
        void cancelWait() { _waiterCount.fetch_sub( 1, std::memory_order_seq_cst ); }

        /** @brief 대기자가 있으면 세대를 올려 모두 깨웁니다. 조건을 바꾼 원자 연산 뒤에 부릅니다. */
        void notifyAll()
        {
            // 조건을 바꾼 원자 연산 뒤에 메모리 펜스를 치고 대기자 수를 본다. 대기자 쪽의 "수 올리기 → 펜스 → 조건 확인" 과 짝을 이룬다.
            std::atomic_thread_fence( std::memory_order_seq_cst );
            if ( _waiterCount.load( std::memory_order_seq_cst ) == 0 )
                return;
            _epoch.fetch_add( 1, std::memory_order_release );
            Futex::wakeAll( _epoch );
        }

    private:
        atomic<uint32> _epoch;       ///< 깨울 때마다 오르는 세대이자 잠드는 워드
        atomic<uint32> _waiterCount; ///< 잠든(또는 잠들려는) 스레드 수
    };
} // namespace sw
