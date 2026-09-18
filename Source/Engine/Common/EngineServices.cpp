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

            // 빠뜨린 것을 **여기서 한 번** 크게 말한다. 이것이 없으면 증상은 바인딩이 아니라 한참 뒤
            // 엉뚱한 자리에서 나타난다 — `areEngineServicesBound()` 로 게이팅되는 스무 곳이 전부
            // 조용히 폴백으로 가기 때문이다(배포본에서 셰이더 캐시를 건너뛰고 DXC 를 부르다 죽었다).
            const utf8* pMissing = findUnboundRequiredServiceName( s_services );
            if ( pMissing != nullptr )
            {
                SW_LOG_WARNING( "필수 엔진 서비스 '%#' 가 비어 있습니다 — areEngineServicesBound() 가 false 가 되어 "
                                "그것으로 게이팅되는 경로가 전부 폴백으로 갑니다.",
                                pMissing );
            }
        }

        void unbindEngineServices()
        {
            s_services = {};
        }

        const utf8* findUnboundRequiredServiceName( const EngineServices& services )
        {
            // 행 순서가 곧 보고 순서다 — 같은 표를 두 번 물으면 같은 이름이 나온다.
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned ) \
    if constexpr ( ( required ) != 0 )                                               \
    {                                                                                \
        if ( services.member == nullptr )                                            \
            return #Type;                                                            \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
            return nullptr;
        }

        const utf8* findUnboundRequiredServiceName()
        {
            return findUnboundRequiredServiceName( s_services );
        }

        const EngineServices& getBoundEngineServices()
        {
            return s_services;
        }

        bool areEngineServicesBound()
        {
            return findUnboundRequiredServiceName( s_services ) == nullptr;
        }

        void fillModuleServices( ModuleService& outService, bool bGameModuleOnly )
        {
            using namespace sw::internal;
            outService = {};

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )         \
    if ( bGameModuleOnly == false || ( ( gameAllowed ) == 1 ) )                              \
    {                                                                                        \
        outService.arrServices[toRawServiceId( ModuleServiceId::Type )] = s_services.member; \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) \
    SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned ) \
    SW_ENGINE_SERVICE( member, Tag, Type, getter, 0, gameAllowed, owned )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned ) \
    Type& getter()                                                                   \
    {                                                                                \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );        \
        return *s_services.member;                                                   \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) \
    const Type& getter()                                                                   \
    {                                                                                      \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );              \
        return *s_services.member;                                                         \
    }
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned ) \
    Type* getter()                                                             \
    {                                                                          \
        return s_services.member;                                              \
    }
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    } // namespace engine
} // namespace sw
