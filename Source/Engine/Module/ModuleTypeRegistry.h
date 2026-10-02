/**
 * @file ModuleTypeRegistry.h
 * @brief 동적으로 로드한 모듈의 리플렉션 타입과 컴포넌트 팩토리를 등록 · 정리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    struct ComponentFactoryRegistrar;
    struct EnumRegistrar;
    struct GlobalVariableRegistrar;
    struct TypeRegistrar;

    namespace engine
    {
        /** @brief DLL 로드 직후: 전역 헤드에 매달린 모듈의 리플렉션 타입 · 컴포넌트 팩토리 · 전역 변수를 떼어 등록합니다. */
        SW_API void registerModuleTypes( string_view moduleName );

        /**
         * @brief DLL 로드 직후: 지정된 헤드 포인터들로부터 모듈의 타입 · 팩토리 · 전역 변수를 등록합니다.
         * @param pVariableHead 이 로드에서 새로 매달린 전역 변수 등록자입니다. 타입과 달리 캐시해 두었다 다시 쓰지 않습니다 — 같은 이름을
         *                      두 번 올리면 매니저가 경고하고 무시하므로, 새로 뗀 것이 있을 때만 올립니다.
         */
        SW_API void registerModuleTypes(
            string_view                    moduleName,
            TypeRegistrar*                 pTypeHead,
            EnumRegistrar*                 pEnumHead,
            sw::ComponentFactoryRegistrar* pFactoryHead,
            GlobalVariableRegistrar*       pVariableHead );

#if !defined( SW_SHIPPING )
        // 모듈을 **내리는** 쪽은 Dev 에만 있다. Shipping 은 모듈을 정적 링크해 프로세스가 끝날 때까지 그대로 있으므로
        // 등록 해제가 할 일이 없다. 그래서 코드도 두지 않는다. 등록(registerModuleTypes)은 Shipping 도 쓴다.
        /** @brief DLL 언로드 직전: 해당 모듈의 리플렉션 타입, 컴포넌트 팩토리를 정리합니다. */
        SW_API void unregisterModuleTypes( string_view moduleName );

        /**
         * @brief 모듈 이미지 [@p pBegin, @p pEnd) 의 코드를 가리키는 엔진 쪽 등록을 뗍니다. 이미지를 내리거나 언로드를 미루기 **전에** 부릅니다.
         * @details 모듈보다 오래 사는 등록부는 `IModuleCodeHolder` 를 상속해 만들어질 때 스스로 목록에 오르고, 여기서는 그 목록을 훑습니다
         *          — 이벤트 버스(구독과 그 모듈이 만든 채널 항목) · 전역 로그 리스너 · Undo 스택(들어 있으면 통째로 비운다) · 창의 처리기 ·
         *          에셋 캐시 등록부. 등록부를 하나 더하는 자리는 그 등록부의 상속 한 줄이고 이 함수는 고치지 않습니다. 뗀 것은 보유자 이름과
         *          함께 경고로 남깁니다 — 모듈이 스스로 떼지 않고 남긴 것이라 모듈 쪽 버그의 실마리입니다.
         * @param pOutKeepImageMapped 주면, 이 이미지를 **내리면 안 되는지** 받습니다. 이미지가 만든 이벤트 채널을 다른 코드가 아직 구독하면 true —
         *                            채널의 브로드캐스트 함수와 멀티캐스트의 해제자(`shared_ptr` 제어 블록)가 그 이미지의 코드라, 내리면 다음
         *                            발행 · 디스패처 소멸이 내려간 코드로 뛴다. 그런 이미지는 프로세스 끝까지 올려 둔다(떼어 낼 방법이 없다).
         * @return 모든 보유자에서 뗀 것의 수입니다. 모듈이 제대로 정리했으면 0 입니다.
         */
        SW_API uint32 releaseModuleCode( string_view moduleName, const void* pBegin, const void* pEnd, bool* pOutKeepImageMapped = nullptr );
#endif
    } // namespace engine
} // namespace sw
