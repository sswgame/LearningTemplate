/**
 * @file ScopedAssertCapture.h
 * @brief 케이스가 단언(`SW_ASSERT` · 멈추는 `SW_LOG_ASSERT`)이 걸리는 경로를 일부러 지날 때, 멈추지 않고 개수만 세는 구간입니다.
 * @details Debug 의 단언은 디버거에서 멈추므로(`SW_DEBUG_BREAK`) 단언이 걸리는 입력을 테스트하면 프로세스가 죽는다. 이 구간 안에서는 단언이 세어지기만 하고
 *          코드는 그 뒤로 이어진다(배포본과 같은 길 — 유니티 `LogAssert.Expect`, 언리얼 자동화의 기대 오류). 가로채기는 프로세스 전체라 다른 스레드의 단언도 센다.
 *          단언은 Debug 에만 있으므로 그 밖의 구성에서 `getCount()` 는 늘 0 이다 — 개수를 보는 단언은 `if constexpr ( ScopedAssertCapture::kAssertsAreActive )` 로 가른다.
 *          `SW_LOG_ASSERT` 는 Error 로그도 남기므로 그 id 를 `SW_EXPECT_LOG` 에 함께 적는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::test
{
    /** @class ScopedAssertCapture @brief 만들 때 단언 가로채기를 걸고(`internal::beginAssertCapture`) 없앨 때 풉니다. */
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

        /** @brief 이 구간이 시작한 뒤 가로챈 단언 수입니다(다른 스레드의 단언도 센다). */
        uint32 getCount() const { return internal::getCapturedAssertCount() - _startCount; }

    private:
        uint32 _startCount;
#else
        // 단언이 멈추지 않는 구성에는 가로채기 창구(`internal::beginAssertCapture`)가 없다 — 이 구간은 아무것도 하지 않는다.
        static constexpr bool kAssertsAreActive = false;

        ScopedAssertCapture() = default;
        ~ScopedAssertCapture() {} // 사용자 정의 소멸자 — 이름만 둔 구간이 -Wunused-variable 에 걸리지 않는다(Debug 와 같은 RAII 가드로 보인다)
        ScopedAssertCapture( const ScopedAssertCapture& )            = delete;
        ScopedAssertCapture& operator=( const ScopedAssertCapture& ) = delete;

        /** @brief 단언이 없는 구성이라 늘 0 입니다. */
        uint32 getCount() const { return 0; }
#endif
    };
} // namespace sw::test
