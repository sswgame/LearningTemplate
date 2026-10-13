#include "pch.h"

#include "Engine/Common/EngineServices.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "RuntimeAPI/Service/ModuleServiceTable.h"
#include "RuntimeAPI/Service/ServiceListColumns.h"

// 서비스 **타입의 정의는 하나도 필요 없다.** 행마다 `locateSubsystem<Type>()` 이 타입 해시(함수 서명 글)로 찾으므로 전방 선언(EngineServices.h 가
// 목록에서 생성한다)으로 충분하다 — 매니저 헤더를 끌어오면 이 접착 파일이 엔진 전체에 의존하게 된다. 서브시스템이 아닌 행(타입 레지스트리 ·
// 엔진 기본 에셋 · 렌더러 · 명령줄 · 전역 변수 · 메모리 프로파일러)은 호스트가 소유자의 객체를 범위에 `bind` 한다.

namespace sw
{
    SW_LOG_CALLER( "EngineServices" );

    namespace
    {
        /** @brief 찾아 둔 서비스 포인터 묶음입니다. */
        EngineServices s_services{};
        /** @brief 바인딩한 범위입니다(목록 밖의 이름 조회 창구). */
        const ISubsystemLocator* s_pSubsystems{ nullptr };
        /**
         * @brief `areEngineServicesBound()` 의 답입니다. bind/unbind 때만 바뀝니다.
         * @details `Component::getTypeInfo()` 가 캐스트마다 이것을 부르므로 부를 때마다 필수 서비스를 훑으면 캐스트 비용의 절반이 됩니다.
         */
        atomic<bool> s_bServicesBound{ false };

        struct EngineServicesInternal
        {
            /** @brief @p subsystems 에서 목록의 행을 모두 찾습니다. 없는 행은 nullptr 입니다. */
            static EngineServices resolve( const ISubsystemLocator& subsystems )
            {
                EngineServices services{};
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )       services.member = locateSubsystem<Type>( &subsystems );
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) services.member = locateSubsystem<Type>( &subsystems );
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                services.member = locateSubsystem<Type>( &subsystems );
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
                return services;
            }

            /** @brief @p services 에서 비어 있는 첫 필수 행의 타입 이름입니다. 행 순서가 곧 보고 순서다. */
            static const utf8* findMissing( const EngineServices& services )
            {
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility ) \
    if constexpr ( SW_SERVICE_IS_REQUIRED( requirement ) != 0 )                 \
    {                                                                           \
        if ( services.member == nullptr )                                       \
            return #Type;                                                       \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
                return nullptr;
            }
        };
    } // namespace

    namespace engine
    {
        const utf8* bindEngineServices( const ISubsystemLocator& subsystems )
        {
            s_services           = EngineServicesInternal::resolve( subsystems );
            s_pSubsystems        = &subsystems;
            const utf8* pMissing = EngineServicesInternal::findMissing( s_services );
            s_bServicesBound.store( pMissing == nullptr, std::memory_order_release );
            return pMissing;
        }

        void unbindEngineServices()
        {
            s_services    = {};
            s_pSubsystems = nullptr;
            s_bServicesBound.store( false, std::memory_order_release );
        }

        const utf8* findUnboundRequiredServiceName( const ISubsystemLocator& subsystems )
        {
            return EngineServicesInternal::findMissing( EngineServicesInternal::resolve( subsystems ) );
        }

        const utf8* findUnboundRequiredServiceName()
        {
            return EngineServicesInternal::findMissing( s_services );
        }

        const EngineServices& getBoundEngineServices()
        {
            return s_services;
        }

        const ISubsystemLocator* getEngineSubsystems()
        {
            return s_pSubsystems;
        }

        bool areEngineServicesBound()
        {
            return s_bServicesBound.load( std::memory_order_acquire );
        }

        void fillModuleServices( ModuleServiceTable& outService, bool bGameModuleOnly )
        {
            using namespace sw::internal;
            outService = {};

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )              \
    if ( bGameModuleOnly == false || ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 1 ) )     \
    {                                                                                        \
        outService.arrServices[toRawServiceID( ModuleServiceID::Type )] = s_services.member; \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                SW_ENGINE_SERVICE( member, Tag, Type, getter, Optional, visibility )
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility ) \
    Type& getter()                                                              \
    {                                                                           \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );   \
        return *s_services.member;                                              \
    }
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) \
    const Type& getter()                                                              \
    {                                                                                 \
        SW_LOG_ASSERT( s_services.member != nullptr, #Type " is not bound" );         \
        return *s_services.member;                                                    \
    }
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility ) \
    Type* getter()                                                     \
    {                                                                  \
        return s_services.member;                                      \
    }
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
    } // namespace engine
} // namespace sw
