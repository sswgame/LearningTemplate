#include "pch.h"

#include "Engine/Common/EngineServices.h"

#include "RuntimeAPI/Service/ModuleService.h"

// 서비스 **타입의 정의는 하나도 필요 없다.** 이 파일이 하는 일은 포인터를 담아 두고 참조로
// 되돌려 주는 것뿐이라 전방 선언(EngineServices.h 가 목록에서 생성한다)으로 충분하다. 예전에는
// 매니저 헤더 열다섯 개를 끌어와서, 105줄짜리 접착 파일이 엔진 전체에 의존하는 것처럼 보였다 —
// ShaderCache 와 GameObjectManager 까지 들어와 있었다.

namespace sw
{
    SW_LOG_CALLER( "EngineServices" );

    namespace
    {
        /** @brief App 이 바인딩해 둔 서비스 포인터 묶음입니다. */
        EngineServices s_services{};
    } // namespace

    namespace engine
    {
        void bindEngineServices( const EngineServices& services )
        {
            s_services = services;
        }

        void unbindEngineServices()
        {
            s_services = {};
        }

        bool areEngineServicesBound()
        {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed ) \
    if constexpr ( ( required ) != 0 )                                        \
    {                                                                         \
        if ( s_services.member == nullptr )                                   \
            return false;                                                     \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
            return true;
        }

        void fillModuleServices( ModuleService& outService, bool bGameModuleOnly )
        {
            using namespace sw::internal;
            outService = {};

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )                \
    if ( bGameModuleOnly == false || ( ( gameAllowed ) == 1 ) )                              \
    {                                                                                        \
        outService.arrServices[toRawServiceId( ModuleServiceId::Type )] = s_services.member; \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) \
    SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed ) \
    SW_ENGINE_SERVICE( member, Tag, Type, getter, 0, gameAllowed )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed ) \
    Type& getter()                                                            \
    {                                                                         \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" ); \
        return *s_services.member;                                            \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) \
    const Type& getter()                                                            \
    {                                                                               \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );       \
        return *s_services.member;                                                  \
    }
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed ) \
    Type* getter()                                                      \
    {                                                                   \
        return s_services.member;                                       \
    }
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    } // namespace engine
} // namespace sw
