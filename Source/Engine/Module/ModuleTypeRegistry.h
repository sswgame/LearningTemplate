/**
 * @file ModuleTypeRegistry.h
 * @brief 동적으로 로드한 모듈의 리플렉션 타입(컴포넌트 생성 함수 포함)과 전역 변수를 등록 · 정리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    struct EnumRegistrar;
    struct GlobalVariableRegistrar;
    struct TypeRegistrar;

    namespace engine
    {
        /** @brief DLL 로드 직후: 전역 헤드에 매달린 모듈의 리플렉션 타입 · 전역 변수를 떼어 등록합니다. 컴포넌트 생성 함수는 타입(`TypeInfo::_addComponent`)과 함께 오릅니다. */
        SW_API void registerModuleTypes( string_view moduleName );

        /**
         * @brief DLL 로드 직후: 지정된 헤드 포인터들로부터 모듈의 타입 · 전역 변수를 등록합니다.
         * @param pVariableHead 이 로드에서 새로 매달린 전역 변수 등록자입니다. 타입과 달리 캐시해 두었다 다시 쓰지 않습니다 — 같은 이름을
         *                      두 번 올리면 매니저가 경고하고 무시하므로, 새로 뗀 것이 있을 때만 올립니다.
         */
        SW_API void registerModuleTypes(
            string_view              moduleName,
            TypeRegistrar*           pTypeHead,
            EnumRegistrar*           pEnumHead,
            GlobalVariableRegistrar* pVariableHead );

        /**
         * @brief enum 전역 변수가 열거자 이름(`-gv_rhiBackend=Vulkan`)을 받도록 리플렉션 enum 표를 파서로 겁니다. 엔진 타입을 등록한 뒤 부릅니다.
         * @return 명령줄에서 받아 둔 이름 가운데 모르는 열거자가 있으면 false 입니다 — 기동은 멈춥니다(모르는 값으로 기본값을 쓰며 돌지 않게).
         */
        [[nodiscard]] SW_API bool bindGlobalVariableEnumNames();
        /** @brief `bindGlobalVariableEnumNames` 가 건 파서를 뗍니다. TypeRegistry 를 놓기 전에 부릅니다. */
        SW_API void unbindGlobalVariableEnumNames();

#if !defined( SW_SHIPPING )
        // 모듈을 **내리는** 쪽은 Dev 에만 있다. Shipping 은 모듈을 정적 링크해 프로세스가 끝날 때까지 그대로 있으므로
        // 등록 해제가 할 일이 없다. 그래서 코드도 두지 않는다. 등록(registerModuleTypes)은 Shipping 도 쓴다.
        /** @brief DLL 언로드 직전: 해당 모듈 타입의 살아 있는 컴포넌트를 지우고, 리플렉션 타입(생성 함수 포함) · 전역 변수를 정리합니다. */
        SW_API void unregisterModuleTypes( string_view moduleName );
#endif
    } // namespace engine
} // namespace sw
