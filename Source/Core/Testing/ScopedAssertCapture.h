/**
 * @file ScopedAssertCapture.h
 * @brief 케이스가 단언(`SW_ASSERT` · 멈추는 `SW_LOG_ASSERT`)이 걸리는 경로를 일부러 지날 때 멈추지 않고 세게 하는 RAII 가드입니다.
 * @details Debug 의 단언은 디버거에서 멈추므로 단언이 걸리는 입력을 테스트하면 프로세스가 죽는다. 가드 안에서는 단언이 세어지기만 하고 코드는 그 뒤로
 *          이어진다(배포본과 같은 길). 창구는 `Core/Foundation/Macros.h` 의 단언 가로채기(Debug 에만 있다)다 — 유니티 `LogAssert.Expect` 와 같은 역할.
 *          단언은 Debug 에만 있으므로 다른 구성에서 `getCount()` 는 늘 0 이다. 수를 보는 단언은 `kAssertsAreActive` 로 가른다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::test
{
    /**
     * @class ScopedAssertCapture
     * @brief 스코프 동안 단언을 멈추지 않고 셉니다(겹쳐 걸 수 있다).
     */
    class ScopedAssertCapture
    {
    public:
#if defined( SW_DEBUG )
        static constexpr bool kAssertsAreActive = true;

        ScopedAssertCapture()
            : _startCount{ internal::getCapturedAssertCount() }
        {
            internal::beginAssertCapture();
        }
        ~ScopedAssertCapture() { internal::endAssertCapture(); }
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 이 스코프가 시작한 뒤 가로챈 단언 수입니다(다른 스레드의 단언도 센다). */
        [[nodiscard]] uint32 getCount() const { return internal::getCapturedAssertCount() - _startCount; }

    private:
        uint32 _startCount;
#else
        static constexpr bool kAssertsAreActive = false;

        ScopedAssertCapture() = default;
        ~ScopedAssertCapture() {} // 사용자 정의 소멸자 — 이름만 둔 가드가 -Wunused-variable 에 걸리지 않는다
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 단언이 없는 구성이라 늘 0 입니다. */
        [[nodiscard]] uint32 getCount() const { return 0; }
#endif
    };
} // namespace sw::test
