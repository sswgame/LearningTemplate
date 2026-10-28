/**
 * @file ModuleTypeRegistry.h
 * @brief 동적으로 로드한 모듈의 리플렉션 타입(컴포넌트 생성 함수 포함)과 전역 변수를 등록 · 정리합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct EnumRegistrar;
    struct GlobalVariableRegistrar;
    struct AssetDataKindRegistrar;
    struct TypeRegistrar;

    namespace engine
    {
        /**
         * @struct ModuleRegistrarHeads
         * @brief 라이브러리 하나의 정적 등록자 목록 머리 모음입니다. 모듈 라이브러리가 링크 때 목록에 다는 등록자 종류를 늘릴 때 여기 한 줄만 더하면, 올리는 길
         *        (`registerModuleTypes`)과 핫 리로드(머리를 떼어 두었다 커밋 때 올리고, 중단하면 되돌린다)가 같이 따라온다.
         */
        struct ModuleRegistrarHeads
        {
            TypeRegistrar*           _pType{ nullptr };
            EnumRegistrar*           _pEnum{ nullptr };
            GlobalVariableRegistrar* _pVariable{ nullptr }; ///< 타입과 달리 캐시해 두었다 다시 쓰지 않는다 — 같은 이름을 두 번 올리면 매니저가 경고하고 무시하므로, 새로 뗀 것만 올린다.
            AssetDataKindRegistrar*  _pDataKind{ nullptr }; ///< 변수와 같다 — 새로 뗀 것만 올린다(모듈 이름으로 올리고 내리기 전에 뺀다).
        };

        /** @brief 전역 머리들을 그대로 읽습니다(비우지 않습니다). */
        [[nodiscard]] SW_API ModuleRegistrarHeads peekGlobalRegistrarHeads();
        /** @brief 전역 머리들을 @p heads 로 놓습니다. 기본값을 넘기면 모두 비웁니다(올린 쪽이 걷어 간 뒤, 또는 중단한 로드를 되돌릴 때). */
        SW_API void setGlobalRegistrarHeads( const ModuleRegistrarHeads& heads );

        /** @brief DLL 로드 직후: 전역 헤드에 매달린 모듈의 리플렉션 타입 · 전역 변수 · 데이터 파일 종류를 떼어 등록합니다. 컴포넌트 생성 함수는 타입(`TypeInfo::_addComponent`)과 함께 오릅니다. */
        SW_API void registerModuleTypes( string_view moduleName );

        /** @brief DLL 로드 직후: 지정된 헤드들로부터 모듈의 타입 · 전역 변수 · 데이터 파일 종류를 등록합니다(`ModuleRegistrarHeads` 참고). */
        SW_API void registerModuleTypes( string_view moduleName, const ModuleRegistrarHeads& heads );

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
