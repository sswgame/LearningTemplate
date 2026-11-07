#include "pch.h"

#include "Engine/Foundation/EngineServices.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

// 서비스 **타입의 정의는 하나도 필요 없다.** 칸은 타입마다 그 객체를 범위에 넣는 .cpp 의 등록 줄이 정의하고 엔진의 등록 목록에 단다 —
// 이 파일은 목록을 걸으며 타입 해시로 찾아 칸에 둘 뿐이다(매니저 헤더를 끌어오면 이 접착 파일이 엔진 전체에 의존하게 된다).

namespace sw
{
    namespace
    {
        /** @brief 바인딩한 범위입니다(칸이 없는 타입의 조회 창구). */
        const ISubsystemLocator* s_pSubsystems{ nullptr };
        /**
         * @brief `areEngineServicesBound()` 의 답입니다. bind/unbind 때만 바뀝니다.
         * @details `Component::getTypeInfo()` 가 캐스트마다 이것을 부르므로 부를 때마다 필수 칸을 훑으면 캐스트 비용의 절반이 됩니다.
         */
        atomic<bool> s_bServicesBound{ false };
    } // namespace

    namespace engine
    {
        EngineServiceBindResult bindEngineServices( const ISubsystemLocator& subsystems )
        {
            const EngineServiceBindResult result = EngineRegistrationList::getGlobal().bindServices( subsystems );
            if ( result._kind == EngineServiceBindKind::AlreadyBound )
                return result; // 앞 엔진의 범위 · 플래그를 그대로 둔다
            s_pSubsystems = &subsystems;
            s_bServicesBound.store( result.isBound(), std::memory_order_release );
            return result;
        }

        void unbindEngineServices( const ISubsystemLocator& subsystems )
        {
            if ( s_pSubsystems != &subsystems )
                return; // 묶지 못한 엔진의 종료가 앞 엔진의 칸을 비우지 않는다
            EngineRegistrationList::getGlobal().unbindServices( subsystems );
            s_pSubsystems = nullptr;
            s_bServicesBound.store( false, std::memory_order_release );
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
            EngineRegistrationList::getGlobal().bindModuleServices( outScope, bGameModuleOnly, bSkipAlreadyBound );
        }
    } // namespace engine
} // namespace sw
