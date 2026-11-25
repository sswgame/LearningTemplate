/**
 * @file LowLevelReporterSlot.h
 * @brief 로그보다 아래 티어의 보고(`SW_LOW_LEVEL_*`)를 받는 보고자 칸입니다. 로그(`Logger.cpp`)가 기동 때 걸고 프로세스 끝까지 쥔다.
 * @details 보고자는 하나다 — 이미 걸려 있으면 이중 등록 단언이고 빈 토큰이다. 예전 `setLowLevelReporter` 는 앞 보고자를 돌려주는 교환이라, 부르는 쪽이 짝을 잊거나
 *          순서가 엇갈리면 로그의 보고자를 말없이 지웠다. 테스트가 잠깐 자기 보고자를 걸 때는 `suspendLowLevelReporter` 로 로그의 것을 떼어 둔다.
 *          `Macros.h` 가 칸 형식(`ObserverSlot`)을 include 하면 고리가 생겨 따로 둔다(`AssertDialogSlot.h` 와 같은 까닭). 정의는 `Macros.cpp` 다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/ObserverSlot.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

namespace sw::internal
{
    /** @brief 보고자 함수의 형식입니다(`LowLevelReporterFunc` 가 가리키는 것). */
    using LowLevelReporterSignature = std::remove_pointer_t<LowLevelReporterFunc>;
    /** @brief 보고자 등록 토큰입니다. 토큰이 사라지면 보고자를 떼고, 이후 보고는 stderr 로 간다. */
    using LowLevelReporterSubscription = ObserverSlot<LowLevelReporterSignature>::Subscription;
    /** @brief 걸린 보고자를 범위 동안 떼어 두는 범위입니다(테스트만). */
    using LowLevelReporterSuspension = ObserverSlot<LowLevelReporterSignature>::Suspension;

    /** @brief 보고자를 겁니다. 보고자는 하나라 이미 걸려 있으면 이중 등록 단언이고 빈 토큰입니다. nullptr 이면 빈 토큰입니다. */
    [[nodiscard]] SW_API LowLevelReporterSubscription bindLowLevelReporter( LowLevelReporterFunc pfnReporter ) noexcept;
    /** @brief 걸린 보고자를 범위 동안 떼어 둡니다 — 테스트만 쓴다. 범위가 끝날 때 칸이 비어 있어야 되돌린다. */
    [[nodiscard]] SW_API LowLevelReporterSuspension suspendLowLevelReporter() noexcept;
} // namespace sw::internal
