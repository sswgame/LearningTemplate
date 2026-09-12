/**
 * @file ModuleTypeRegistry.h
 * @brief 동적 로드 모듈의 리플렉션 타입 및 컴포넌트 팩토리 등록/정리 인터페이스.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    struct ComponentFactoryRegistrar;
    struct EnumRegistrar;
    struct TypeRegistrar;

    namespace engine
    {
        /** @brief DLL 로드 직후: 해당 모듈의 리플렉션 타입, 컴포넌트 팩토리를 등록합니다. */
        SW_API void registerModuleTypes( string_view moduleName );

        /** @brief DLL 로드 직후: 지정된 헤드 포인터들로부터 모듈의 타입들을 등록합니다. */
        SW_API void registerModuleTypes(
            string_view                    moduleName,
            TypeRegistrar*                 pTypeHead,
            EnumRegistrar*                 pEnumHead,
            sw::ComponentFactoryRegistrar* pFactoryHead );

#if !defined( SW_SHIPPING )
        // 모듈을 **내리는** 쪽은 Dev 에만 있다. Shipping 은 모듈을 정적 링크해 프로세스가 끝날 때까지 그대로 있으므로
        // 등록 해제가 할 일이 없다 — 코드도 두지 않는다. 등록(registerModuleTypes)은 Shipping 도 쓴다.
        /** @brief DLL 언로드 직전: 해당 모듈의 리플렉션 타입, 컴포넌트 팩토리를 정리합니다. */
        SW_API void unregisterModuleTypes( string_view moduleName );
#endif
    } // namespace engine
} // namespace sw
