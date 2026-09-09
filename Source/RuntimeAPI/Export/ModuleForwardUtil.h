/**
 * @file ModuleForwardUtil.h
 * @brief 불투명 핸들 → 구현 인스턴스 전달 헬퍼.
 *
 * @details C-ABI 테이블의 항목은 하나같이 "핸들을 구현 타입으로 되돌리고, 널이면 아무것도
 *          하지 않고, 아니면 멤버를 부른다" 를 반복한다. 항목마다 그 세 줄을 복사하면 테이블에
 *          함수를 하나 더 붙일 때 복사할 것이 생긴다 — 널 검사를 한 군데서 빠뜨려도 아무도
 *          모른다. 그 반복을 여기로 모아 Export 매크로의 한 항목이 한 줄이 되게 한다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct ModuleForwardUtil
     * @brief Export 매크로가 쓰는 핸들 전달 헬퍼 모음.
     */
    struct ModuleForwardUtil
    {
        /**
         * @brief 핸들이 가리키는 인스턴스의 멤버를 부릅니다. 핸들이 비면 아무것도 하지 않습니다.
         * @param pHandle 호스트가 넘긴 불투명 핸들.
         * @param method 부를 멤버 함수 포인터.
         */
        template <typename Class, typename Method, typename... Args>
        static void callVoid( void* pHandle, Method method, Args&&... args )
        {
            Class* pInstance = static_cast<Class*>( pHandle );
            if ( pInstance == nullptr )
                return;

            ( pInstance->*method )( std::forward<Args>( args )... );
        }

        /**
         * @brief 멤버를 부르고 결과를 돌려줍니다. 핸들이 비면 fallback 을 돌려줍니다.
         * @param fallback 핸들이 비었을 때 돌려줄 값. `Result` 를 명시하면 멤버의 반환형이
         *        달라도 여기서 변환한다(예: `CameraComponent*` → `void*`).
         */
        template <typename Class, typename Result, typename Method, typename... Args>
        static Result callOr( void* pHandle, Result fallback, Method method, Args&&... args )
        {
            Class* pInstance = static_cast<Class*>( pHandle );
            if ( pInstance == nullptr )
                return fallback;

            return static_cast<Result>( ( pInstance->*method )( std::forward<Args>( args )... ) );
        }
    };
} // namespace sw
