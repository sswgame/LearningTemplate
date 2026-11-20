/**
 * @file ModuleForwardUtil.h
 * @brief 불투명 핸들 → 구현 인스턴스 전달 도우미입니다.
 *
 * @details C-ABI 테이블의 항목은 하나같이 "핸들을 구현 타입으로 되돌리고, 널이면 아무것도
 *          하지 않고, 아니면 멤버를 부른다" 를 반복합니다. 항목마다 그 세 줄을 복사하면 테이블에
 *          함수를 하나 더 붙일 때 복사할 것이 생깁니다. 널 검사를 한 군데서 빠뜨려도 아무도
 *          모릅니다. 그 반복을 여기로 모아 Export 매크로의 한 항목이 한 줄이 되게 합니다.
 *
 *          모듈 경계의 할당 태그도 여기서 겁니다 — 호스트가 표를 거쳐 모듈 코드를 부르는 동안의 할당은 그 모듈 종류의 태그(`kModuleMemoryTag`)로 센다.
 *          모듈 종류마다 태그는 Export 헤더의 별칭 한 줄(`GameModuleForwardUtil` · `EditorModuleForwardUtil`)이 정하고, 표 항목도 호스트의 모듈 매니저도 태그를 적지 않는다
 *          (언리얼 LLM 이 모듈 경계에 태그 스코프를 두는 것과 같은 자리).
 */
#pragma once
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @struct ModuleForwardUtil
     * @brief Export 매크로가 쓰는 핸들 전달 도우미 모음입니다. 부르는 동안 @p kModuleMemoryTag 를 겁니다.
     */
    template <MemoryTag kModuleMemoryTag>
    struct ModuleForwardUtil
    {
        /** @brief 구현 인스턴스를 만듭니다(표의 `create`). */
        template <typename Class>
        static Class* create()
        {
            const ScopedMemoryTag moduleMemoryTag{ kModuleMemoryTag };
            return sw::make_unique<Class>().release();
        }

        /** @brief 구현 인스턴스를 부숩니다(표의 `destroy`). */
        template <typename Class>
        static void destroy( void* pHandle )
        {
            const ScopedMemoryTag moduleMemoryTag{ kModuleMemoryTag };
            sw_delete( static_cast<Class*>( pHandle ) );
        }

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

            const ScopedMemoryTag moduleMemoryTag{ kModuleMemoryTag };
            ( pInstance->*method )( std::forward<Args>( args )... );
        }

        /**
         * @brief 멤버를 부르고 결과를 반환합니다. 핸들이 비면 fallback 을 반환합니다.
         * @param fallback 핸들이 비었을 때 반환할 값. `Result` 를 명시하면 멤버의 반환형이
         *        달라도 여기서 변환합니다(예: `CameraComponent*` → `void*`).
         */
        template <typename Class, typename Result, typename Method, typename... Args>
        static Result callOr( void* pHandle, Result fallback, Method method, Args&&... args )
        {
            Class* pInstance = static_cast<Class*>( pHandle );
            if ( pInstance == nullptr )
                return fallback;

            const ScopedMemoryTag moduleMemoryTag{ kModuleMemoryTag };
            return static_cast<Result>( ( pInstance->*method )( std::forward<Args>( args )... ) );
        }
    };
} // namespace sw
