/**
 * @file EngineServices.h
 * @brief App이 소유한 코어 매니저 포인터를 Engine.dll에 바인딩하는 서비스 테이블
 *
 * @details 서비스를 추가하려면 EngineServiceList.xxx 에 한 줄 추가하고, 바인딩 지점
 *          (EngineLoop::initialize, TestFramework/main.cpp)에서 포인터를 채우면 됩니다.
 *          구조체 멤버 · getter 선언/정의 · areEngineServicesBound() 는 그 목록에서 생성됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#if !defined( SW_ENGINE_INTERNAL ) && !defined( SW_APP_INTERNAL ) && !defined( SW_TEST_INTERNAL ) && !defined( SW_TOOL_INTERNAL )
    #error "EngineServices.h can only be included internally by the Engine, App, or Tests."
#endif

namespace sw
{
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       Tag Type;
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) Tag Type;
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             Tag Type;
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT

    // ------------------------------------------------------------------------------
    // 1) EngineServices — App이 소유한 매니저 포인터 묶음
    //    Engine.dll은 이 테이블만 들고, 생성/파괴는 App
    // ------------------------------------------------------------------------------
    struct EngineServices
    {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       Type* member{ nullptr };
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) Type* member{ nullptr };
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             Type* member{ nullptr };
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    };

    struct ModuleService;

    namespace engine
    {
        // ------------------------------------------------------------------------------
        // 2) 바인딩 — initialize 때 연결, shutdown 때 해제
        // ------------------------------------------------------------------------------
        /** @brief Engine.dll 전역 조회가 사용할 매니저 포인터를 바인딩합니다. */
        SW_API void bindEngineServices( const EngineServices& services );
        /** @brief 바인딩을 해제합니다 (앱 종료 시). */
        SW_API void unbindEngineServices();
        /** @brief 필수 매니저가 모두 바인딩되었는지 (MemoryProfiler / GameData 는 선택). */
        SW_API bool areEngineServicesBound();
        /** @brief ModuleService 테이블에 현재 바인딩된 Engine 서비스를 채웁니다. */
        SW_API void fillModuleServices( ModuleService& outService, bool bGameModuleOnly = false );

        // ------------------------------------------------------------------------------
        // 3) 코어 매니저 조회 — bind 이후에만 호출 (EngineServiceList.xxx 에서 생성)
        // ------------------------------------------------------------------------------
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       SW_API Type& getter();
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) SW_API const Type& getter();
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             SW_API Type* getter();
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    } // namespace engine
} // namespace sw
