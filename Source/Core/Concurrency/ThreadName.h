/**
 * @file ThreadName.h
 * @brief 지금 스레드에 OS 이름을 붙입니다(디버거 · 프로파일러가 읽는 이름).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct ThreadName
     * @brief 스레드 이름을 OS 에 적습니다. 이름표는 OS 하나에만 둡니다.
     * @details 디버거(Visual Studio · gdb)와 외부 프로파일러(Tracy 는 Windows 에서 `GetThreadDescription`, 리눅스에서
     *          `/proc/self/task/<tid>/comm` 을 읽는다)가 같은 이름을 봅니다. 그래서 프로파일러를 켜기 **전에** 만든 스레드도
     *          이름이 붙어 있고, 엔진이 따로 이름표를 들 필요가 없습니다.
     */
    struct SW_API ThreadName
    {
        /** @brief OS 가 받는 이름 길이 상한입니다(리눅스 `comm` 은 종료 문자 포함 16 바이트). 넘으면 자릅니다. */
        static constexpr uint32 kMaxPosixLength = 15;

        /**
         * @brief 이 함수를 부른 스레드의 이름을 @p pName 으로 둡니다. nullptr · 빈 문자열이면 아무것도 하지 않습니다.
         * @details 스레드 진입 함수의 첫 줄에서 부릅니다. 다른 스레드의 이름은 바꾸지 않습니다(그 스레드가 스스로 붙입니다).
         */
        static void setCurrentThreadName( const utf8* pName );
    };
} // namespace sw
