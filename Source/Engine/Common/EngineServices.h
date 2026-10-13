/**
 * @file EngineServices.h
 * @brief 엔진 서비스의 조회 캐시입니다 — 호스트의 서브시스템 컬렉션(엔진 범위와 그 부모 범위)에서 행마다 한 번 찾아 둔 타입 포인터입니다.
 *
 * @details **서비스를 추가하려면 그 서비스를 서브시스템으로 만들어 호스트의 범위에 넣고 `EngineServiceList.xxx` 에 한 줄을 더하면 됩니다.**
 *          소유는 범위(`SubsystemScope`)이고 이 표는 아무것도 소유하지 않습니다. `bindEngineServices( 엔진 범위 )` 가 행마다 `locateSubsystem<Type>()` 으로 찾아 두고,
 *          `engine::getXxx()` 는 그 포인터를 돌려줄 뿐입니다 — 캐스트마다 타입 레지스트리를 묻는 자리처럼 프레임마다 도는 경로가 이름으로 찾지 않게 하려는
 *          것이다(언리얼 `GEngine->GetEngineSubsystem<T>()` 은 부를 때마다 맵을 찾는 것으로 알고 있다 — 우리는 바인딩 때 한 번 찾는다).
 *          소유 · 소멸 순서는 범위의 것이다. 목록에 없는 것을 타입으로 찾으려면 `locateSubsystem<T>( getEngineSubsystems() )` 를 쓴다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineServices.h can only be included internally by the Engine, App, or Tests."
#endif

namespace sw
{
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )       Tag Type;
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) Tag Type;
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                Tag Type;
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

    class ISubsystemLocator;

    // ------------------------------------------------------------------------------
    // 1) EngineServices: 찾아 둔 서비스 포인터 묶음(소유하지 않는다)
    // ------------------------------------------------------------------------------
    struct EngineServices
    {
// 인자가 **타입 이름과 선언자 이름**이라 괄호를 씌울 수 없다. `(Type)* (member)` 는 올바른 문법이 아니다.
// NOLINTBEGIN(bugprone-macro-parentheses)
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )       Type* member{ nullptr };
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) Type* member{ nullptr };
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                Type* member{ nullptr };
// NOLINTEND(bugprone-macro-parentheses)
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    };

    struct ModuleServiceTable;

    namespace engine
    {
        // ------------------------------------------------------------------------------
        // 2) 바인딩 — 호스트가 범위를 세운 뒤 묶고, 그 범위를 내리기 전에 끊는다
        // ------------------------------------------------------------------------------
        /**
         * @brief @p subsystems(엔진 범위 — 부모 범위까지 찾는다)에서 목록의 행을 모두 찾아 둡니다.
         * @return 비어 있는 **첫 필수 서비스의 타입 이름**입니다. 모두 찾았으면 nullptr 입니다. 호스트는 이것이 nullptr 가 아니면 서지 않는다 —
         *         그 함수로 게이팅되는 스무 곳이 조용히 폴백으로 가는 것(배포본에서 셰이더 캐시를 건너뛰고 DXC 를 부르다 죽은 적이 있다)을 기동에서 막는다.
         * @note 로케이터는 끊을 때까지 살아 있어야 합니다(`getEngineSubsystems` 가 돌려준다).
         */
        SW_API const utf8* bindEngineServices( const ISubsystemLocator& subsystems );
        /** @brief 바인딩을 끊습니다. 찾아 둔 서비스가 소멸하기 전에 부릅니다. */
        SW_API void unbindEngineServices();
        /**
         * @brief `Required` 인 서비스를 모두 찾았는지 반환합니다. bind/unbind 때 한 번 센 플래그입니다.
         * @details 선택(`Optional`) 항목의 원본은 `EngineServiceList.xxx` 입니다. 목록이 바뀌면 이 설명이 아니라 목록을 믿으십시오.
         */
        SW_API bool areEngineServicesBound();
        /** @brief @p subsystems 에서 찾을 수 없는 첫 필수 서비스의 타입 이름입니다. 바인딩을 흔들지 않고 물어볼 수 있다. 모두 있으면 nullptr 입니다. */
        SW_API const utf8* findUnboundRequiredServiceName( const ISubsystemLocator& subsystems );
        /** @brief 지금 찾아 둔 표에서 비어 있는 첫 필수 서비스의 타입 이름입니다. 모두 있으면 nullptr 입니다. */
        SW_API const utf8* findUnboundRequiredServiceName();
        /**
         * @brief 지금 찾아 둔 표입니다. 비어 있을 수 있는 칸(선택 서비스 · 바인딩 전)을 널로 읽어야 하는 자리가 씁니다.
         * @note 개별 서비스는 생성된 `getXxx()` 로 가져오십시오.
         */
        SW_API const EngineServices& getBoundEngineServices();
        /** @brief 바인딩한 범위의 로케이터입니다. 목록에 없는 서브시스템을 이름으로 찾을 때 씁니다. 바인딩 전이면 nullptr 입니다. */
        SW_API const ISubsystemLocator* getEngineSubsystems();
        /**
         * @brief ModuleServiceTable 테이블에 현재 바인딩된 Engine 서비스를 채웁니다.
         * @param bGameModuleOnly true 면 `GameVisible` 인 것만 채웁니다. 나머지 자리는 nullptr 입니다.
         * @note 표를 먼저 통째로 비우므로, 호스트 전용 서비스는 **이 호출 뒤에** 채워야 합니다.
         */
        SW_API void fillModuleServices( ModuleServiceTable& outService, bool bGameModuleOnly = false );

        // ------------------------------------------------------------------------------
        // 3) 서비스 조회 — bind 이후에만 호출 (EngineServiceList.xxx 에서 생성)
        // ------------------------------------------------------------------------------
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )       SW_API Type& getter();
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) SW_API const Type& getter();
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                SW_API Type* getter();
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    } // namespace engine
} // namespace sw
