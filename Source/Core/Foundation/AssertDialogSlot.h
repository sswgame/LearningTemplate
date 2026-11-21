/**
 * @file AssertDialogSlot.h
 * @brief 단언 대화상자를 거는 칸입니다(검사 구성만). 거는 쪽은 등록 토큰을 받고, 토큰이 사라지면 대화상자가 떨어진다.
 * @details 대화상자는 하나다 — 이미 걸려 있으면 이중 등록 단언이고 빈 토큰이다(예전 `setAssertDialog` 는 말없이 덮어써 앞 주인이 떼면서 뒤 주인의 대화상자를 지웠다).
 *          `Macros.h` 가 이 칸 형식(`ObserverSlot`)을 include 하면 `ObserverSlot.h` 가 다시 `Macros.h` 를 읽는 고리가 생겨 따로 둔다. 정의는 `Macros.cpp` 다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/ObserverSlot.h"
#include "Core/Foundation/Types.h"

#include <type_traits>

#if defined( SW_DO_CHECKS )
namespace sw::internal
{
    /** @brief 대화상자 함수의 형식입니다(`AssertDialogFunc` 가 가리키는 것). */
    using AssertDialogSignature = std::remove_pointer_t<AssertDialogFunc>;
    /** @brief 대화상자 등록 토큰입니다. 토큰이 사라지면 대화상자를 떼고, 이후 단언은 늘 멈춘다. */
    using AssertDialogRegistration = ObserverSlot<AssertDialogSignature>::Registration;

    /**
     * @brief 대화상자를 겁니다. 걸 때 "항상 무시" 로 기억한 자리를 잊습니다. 대화상자는 하나라 이미 걸려 있으면 이중 등록 단언이고 빈 토큰입니다.
     * @param pfnDialog nullptr 이면 빈 토큰입니다.
     */
    [[nodiscard]] SW_API AssertDialogRegistration bindAssertDialog( AssertDialogFunc pfnDialog ) noexcept;
} // namespace sw::internal
#endif
