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
    class ModuleObjectRegistrar;
    class ModuleSubsystemRegistrar;
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
            TypeRegistrar*            _pType{ nullptr };
            EnumRegistrar*            _pEnum{ nullptr };
            GlobalVariableRegistrar*  _pVariable{ nullptr };  ///< 타입과 달리 캐시해 두었다 다시 쓰지 않는다 — 같은 이름을 두 번 올리면 매니저가 경고하고 무시하므로, 새로 뗀 것만 올린다.
            AssetDataKindRegistrar*   _pDataKind{ nullptr };  ///< 변수와 같다 — 새로 뗀 것만 올린다(모듈 이름으로 올리고 내리기 전에 뺀다).
            ModuleObjectRegistrar*    _pObject{ nullptr };    ///< 변수와 같다 — 새로 뗀 것만 붙이고(`attach`), 모듈을 내리기 전에 뗀다(`detach`).
            ModuleSubsystemRegistrar* _pSubsystem{ nullptr }; ///< 변수와 같다 — 새로 뗀 것만 모듈 이름의 자식 범위에 세우고(`ModuleRegistrationHost::mountModule`), 모듈을 내리기 전에 걷는다.
        };

        /** @brief 전역 머리들을 그대로 읽습니다(비우지 않습니다). */
        [[nodiscard]] SW_API ModuleRegistrarHeads peekGlobalRegistrarHeads();
        /** @brief 전역 머리들을 @p heads 로 놓습니다. 기본값을 넘기면 모두 비웁니다(올린 쪽이 걷어 간 뒤, 또는 중단한 로드를 되돌릴 때). */
        SW_API void setGlobalRegistrarHeads( const ModuleRegistrarHeads& heads );

        /**
         * @brief 동적 모듈 하나의 이름 찍힌 등록자(전역 변수 · 데이터 파일 종류)가 모두 @p moduleName 의 것인지 봅니다.
         * @details 등록자는 정의한 대상의 이름(`SW_REGISTRAR_MODULE`)으로 오르고 그 이름으로 내린다. 올리는 쪽 이름과 다르면 다른 이미지(먼저 올려야 할 공용 모듈)의
         *          등록자가 섞인 것이다 — 오류를 남기고 false 입니다.
         */
        [[nodiscard]] SW_API bool verifyModuleRegistrars( string_view moduleName, const ModuleRegistrarHeads& heads );

        /**
         * @brief DLL 로드 직후: 전역 헤드에 매달린 모듈의 리플렉션 타입 · 전역 변수 · 데이터 파일 종류를 떼어 등록합니다. 컴포넌트 생성 함수는 타입(`TypeInfo::_addComponent`)과 함께 오릅니다.
         * @return 등록자 이름이 @p moduleName 과 다르면(`verifyModuleRegistrars`) 아무것도 올리지 않고 false 입니다 — 부르는 쪽은 모듈 로드를 멈춘다.
         */
        [[nodiscard]] SW_API bool registerModuleTypes( string_view moduleName );

        /** @brief DLL 로드 직후: 지정된 헤드들로부터 모듈의 타입 · 전역 변수 · 데이터 파일 종류를 등록합니다(`ModuleRegistrarHeads` 참고). @p heads 는 `verifyModuleRegistrars` 를 지난 것이다. */
        SW_API void registerModuleTypes( string_view moduleName, const ModuleRegistrarHeads& heads );

        /**
         * @brief 기동 때 호스트 이미지(Engine · App · Server · DevTools, 배포는 정적 링크한 모든 모듈)의 등록자를 걷어 올립니다(`ReflectionSubsystem`).
         * @details 여러 대상의 등록자가 한 이미지에 있어 이름을 대조하지 않는다 — 각자 찍힌 이름으로 오르고, 호스트 이미지는 내리지 않는다.
         */
        SW_API void registerHostImageTypes();

        /**
         * @brief enum 전역 변수가 열거자 이름(`-gv_rhiBackend=Vulkan`)을 받도록 리플렉션 enum 표를 파서로 겁니다. 엔진 타입을 등록한 뒤 부릅니다.
         * @return 명령줄에서 받아 둔 이름 가운데 모르는 열거자가 있으면 false 입니다 — 기동은 멈춥니다(모르는 값으로 기본값을 쓰며 돌지 않게).
         */
        [[nodiscard]] SW_API bool bindGlobalVariableEnumNames();
        /** @brief `bindGlobalVariableEnumNames` 가 건 파서를 뗍니다. TypeRegistry 를 놓기 전에 부릅니다. */
        SW_API void unbindGlobalVariableEnumNames();

        /**
         * @brief DLL 언로드 직전: 구독자에게 알리고(`TypeRegistry::subscribeModuleUnloading`), 모듈의 등록자를 떼고(`ModuleObjectRegistrar::detach`),
         *        해당 모듈 타입의 살아 있는 컴포넌트를 지우고, 모듈 서브시스템의 자식 범위를 걷고(`ModuleRegistrationHost::unmountModule`),
         *        리플렉션 타입(생성 함수 포함) · 전역 변수를 정리합니다.
         * @details 레지스트리 연산이라 구성과 무관하게 있다(배포도 게임 인스턴스를 내릴 때 같은 길로 부른다 — 정적 링크라 그 이름으로 오른 것이 없으면 할 일이 없다).
         */
        SW_API void unregisterModuleTypes( string_view moduleName );
    } // namespace engine
} // namespace sw
