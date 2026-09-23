/**
 * @file Futex.h
 * @brief 32비트 원자 워드 하나에 잠들고 그 주소로 깨우는 대기 원시 연산 (Windows `WaitOnAddress` · Linux `futex`).
 * @details 뮤텍스 + 조건 변수는 깨우는 쪽이 뮤텍스를 잡고, 깨어난 쪽이 그 뮤텍스를 **다시** 잡아야 돌아온다 —
 *          워커 넷을 한 번에 깨우면 넷이 한 뮤텍스에 줄을 서서 차례로 일어난다(호송). 여기서는 잠드는 쪽이
 *          자기 워드 하나에 잠들고 깨우는 쪽은 그 주소만 두드리므로 잠금이 없고, 여러 워커를 깨우는 것이
 *          서로 독립이다. `TaskManager` 의 워커 잠들기 · 대기자 파킹이 이것 위에 있다.
 *
 *          약속: `wait( word, expected )` 는 `word == expected` 인 동안만 잠들고 **허위로 깨어날 수 있다** —
 *          부르는 쪽이 조건을 다시 확인하는 루프 안에서 쓴다. 깨우는 쪽은 워드를 **먼저 바꾸고** 깨운다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

namespace sw
{
    /**
     * @struct Futex
     * @brief 원자 워드의 주소로 잠들고 깨웁니다. 워드는 `sw::atomic<uint32>` 하나다.
     */
    struct SW_API Futex
    {
        /** @brief @p word 가 @p expected 인 동안 잠듭니다. 값이 다르면 바로 돌아온다. 허위로 깨어날 수 있다. */
        static void wait( atomic<uint32>& word, uint32 expected );

        /**
         * @brief `wait` 과 같되 @p timeoutMilli 뒤에는 값이 그대로여도 돌아옵니다.
         * @return 값이 바뀌었거나 깨워졌으면 true, 시간이 다 되었으면 false.
         */
        static bool waitFor( atomic<uint32>& word, uint32 expected, uint32 timeoutMilli );

        /** @brief @p word 에 잠든 스레드 하나를 깨웁니다. 아무도 없으면 아무 일도 없다. */
        static void wakeOne( atomic<uint32>& word );

        /** @brief @p word 에 잠든 스레드 전부를 깨웁니다. */
        static void wakeAll( atomic<uint32>& word );
    };
} // namespace sw
