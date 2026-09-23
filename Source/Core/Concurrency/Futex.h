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
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

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
