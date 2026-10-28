#include "pch.h"

#include "Engine/Foundation/EngineServices.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineServiceListColumns.h"

// 서비스 **타입의 정의는 하나도 필요 없다.** 행마다 `locateSubsystem<Type>()` 이 타입 해시(함수 서명 글)로 찾으므로 전방 선언(EngineServices.h 가
// 목록에서 생성한다)으로 충분하다 — 매니저 헤더를 끌어오면 이 접착 파일이 엔진 전체에 의존하게 된다. 서브시스템이 아닌 행(타입 레지스트리 ·
// 엔진 기본 에셋 · 렌더러 · 명령줄 · 전역 변수 · 메모리 프로파일러)은 호스트가 소유자의 객체를 범위에 `bind` 한다.

namespace sw
{
    namespace
    {
        constexpr uint32 kServiceCount = static_cast<uint32>( EngineServiceIndex::Count );

        /** @brief 찾아 둔 서비스 칸입니다(`EngineServiceIndex` 순서). */
        void* s_arrService[kServiceCount]{};
        /** @brief 바인딩한 범위입니다(목록 밖의 타입 조회 창구). */
        const ISubsystemLocator* s_pSubsystems{ nullptr };
        /**
         * @brief `areEngineServicesBound()` 의 답입니다. bind/unbind 때만 바뀝니다.
         * @details `Component::getTypeInfo()` 가 캐스트마다 이것을 부르므로 부를 때마다 필수 서비스를 훑으면 캐스트 비용의 절반이 됩니다.
         */
        atomic<bool> s_bServicesBound{ false };

        struct EngineServicesInternal
        {
            /** @brief @p subsystems 에서 목록의 행을 모두 찾아 @p outArrService 에 둡니다. 없는 행은 nullptr 입니다. */
            static void resolve( const ISubsystemLocator& subsystems, void* ( &outArrService )[kServiceCount] )
            {
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility ) \
    outArrService[EngineServiceSlot<Type>::kIndex] = locateSubsystem<Type>( &subsystems );
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )                SW_ENGINE_SERVICE( Tag, Type, Optional, visibility )
#include "Engine/Foundation/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
            }

            /** @brief @p arrService 에서 비어 있는 첫 필수 행의 타입 이름입니다. 행 순서가 곧 보고 순서다. */
            static const utf8* findMissing( void* const ( &arrService )[kServiceCount] )
            {
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )       \
    if constexpr ( SW_SERVICE_IS_REQUIRED( requirement ) != 0 )       \
    {                                                                 \
        if ( arrService[EngineServiceSlot<Type>::kIndex] == nullptr ) \
            return #Type;                                             \
    }
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )
#include "Engine/Foundation/EngineServiceList.xxx"
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
            EngineServicesInternal::resolve( subsystems, s_arrService );
            s_pSubsystems        = &subsystems;
            const utf8* pMissing = EngineServicesInternal::findMissing( s_arrService );
            s_bServicesBound.store( pMissing == nullptr, std::memory_order_release );
            return pMissing;
        }

        void unbindEngineServices()
        {
            for ( void*& pService : s_arrService )
            {
                pService = nullptr;
            }
            s_pSubsystems = nullptr;
            s_bServicesBound.store( false, std::memory_order_release );
        }

        const utf8* findUnboundRequiredServiceName( const ISubsystemLocator& subsystems )
        {
            void* arrService[kServiceCount]{};
            EngineServicesInternal::resolve( subsystems, arrService );
            return EngineServicesInternal::findMissing( arrService );
        }

        const utf8* findUnboundRequiredServiceName()
        {
            return EngineServicesInternal::findMissing( s_arrService );
        }

        const ISubsystemLocator* getEngineSubsystems()
        {
            return s_pSubsystems;
        }

        bool areEngineServicesBound()
        {
            return s_bServicesBound.load( std::memory_order_acquire );
        }

        void bindModuleServices( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound )
        {
#define SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )                                                                                                            \
    {                                                                                                                                                                      \
        Type* const pService  = static_cast<Type*>( s_arrService[EngineServiceSlot<Type>::kIndex] );                                                                       \
        const bool  bVisible  = bGameModuleOnly == false || ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 1 );                                                             \
        const bool  bFresh    = bSkipAlreadyBound == false || outScope.find<Type>() == nullptr;                                                                            \
        const bool  bBindable = pService != nullptr && bVisible;                                                                                                           \
        if ( bBindable && bFresh )                                                                                                                                         \
            SW_LOW_LEVEL_ASSERT( outScope.bind( pService ), "Engine service %s is bound twice - each row is a distinct type and the caller passes a fresh scope", #Type ); \
    }
#define SW_ENGINE_SERVICE_CONST( Tag, Type, requirement, visibility ) SW_ENGINE_SERVICE( Tag, Type, requirement, visibility )
#define SW_ENGINE_SERVICE_OPT( Tag, Type, visibility )                SW_ENGINE_SERVICE( Tag, Type, Optional, visibility )
#include "Engine/Foundation/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
        }

        namespace internal
        {
            void* const* getServiceSlots()
            {
                return s_arrService;
            }
        } // namespace internal
    } // namespace engine
} // namespace sw
