/**
 * @file ScopedAssertCapture.h
 * @brief 케이스가 단언(`SW_ASSERT` · 멈추는 `SW_LOG_ASSERT`)이 걸리는 경로를 일부러 지날 때 멈추지 않고 세게 하는 RAII 가드입니다.
 * @details Debug 의 단언은 디버거에서 멈추므로 단언이 걸리는 입력을 테스트하면 프로세스가 죽는다. 가드 안에서는 단언이 세어지기만 하고 코드는 그 뒤로
 *          이어진다(배포본과 같은 길). 창구는 `Core/Foundation/Macros.h` 의 단언 가로채기(검사 구성(SW_DO_CHECKS)에만 있다)다 — 유니티 `LogAssert.Expect` 와 같은 역할.
 *          단언은 검사 구성(SW_DO_CHECKS)에만 있으므로 다른 구성에서 `getCount()` 는 늘 0 이다. 수를 보는 단언은 `kAssertsAreActive` 로 가른다.
 *          `SW_LOG_ASSERT` · `SW_LOW_LEVEL_ASSERT` 는 모든 구성에서 Error 로그(id `SW_ASSERT_LOG_ID`)도 남기므로 케이스는 `SW_EXPECT_LOG( SW_ASSERT_LOG_ID )` 로 적는다
 *          (`SW_ASSERT` 는 로그를 남기지 않는다).
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
#if defined( SW_DO_CHECKS )
        static constexpr bool kAssertsAreActive = true;
#else
        static constexpr bool kAssertsAreActive = false;
#endif

        ScopedAssertCapture()
#if defined( SW_DO_CHECKS )
            : _startCount{ internal::getCapturedAssertCount() }
#endif
        {
#if defined( SW_DO_CHECKS )
            internal::beginAssertCapture();
#endif
        }
        ~ScopedAssertCapture() // 사용자 정의 소멸자 — 단언이 없는 구성에서도 이름만 둔 가드가 -Wunused-variable 에 걸리지 않는다
        {
#if defined( SW_DO_CHECKS )
            internal::endAssertCapture();
#endif
        }
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 이 스코프가 시작한 뒤 가로챈 단언 수입니다(다른 스레드의 단언도 센다). 단언이 없는 구성에서는 늘 0 입니다. */
        [[nodiscard]] uint32 getCount() const
        {
#if defined( SW_DO_CHECKS )
            return internal::getCapturedAssertCount() - _startCount;
#else
            return 0;
#endif
        }

#if defined( SW_DO_CHECKS )
    private:
        uint32 _startCount;
#endif
    };
} // namespace sw::test
