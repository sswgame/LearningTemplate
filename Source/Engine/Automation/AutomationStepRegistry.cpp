#include "pch.h"

#include "Engine/Automation/AutomationStepRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationStepRegistry" );

    namespace
    {
        struct AutomationStepRegistryInternal
        {
            /** @brief 등록된 단계 종류(등록 순서). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다. */
            static RegistrationList<const AutomationStepRegistration>& getRegistrations()
            {
                static RegistrationList<const AutomationStepRegistration> s_registration{ RegistrationOrder::Insertion, true };
                return s_registration;
            }

            /** @brief 등록된 시작 차단 조건(등록 순서)입니다. */
            static RegistrationList<const AutomationStartBlocker>& getStartBlockers()
            {
                static RegistrationList<const AutomationStartBlocker> s_blocker{ RegistrationOrder::Insertion, true };
                return s_blocker;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationStepRegistry::registerStep( const AutomationStepRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pKind == nullptr || pRegistration->_pRun == nullptr )
            return false;
        const bool bEngineKind = isEngineStepKind( pRegistration->_pKind );
        if ( bEngineKind || AutomationStepRegistryInternal::getRegistrations().add( pRegistration, pRegistration->_pKind ) != RegistrationResult::Added )
        {
            SW_LOG_ERROR( "Automation step <%#> is already defined - the second one is ignored", pRegistration->_pKind );
            return false;
        }
        return true;
    }

    void AutomationStepRegistry::unregisterStep( const AutomationStepRegistration* pRegistration )
    {
        (void)AutomationStepRegistryInternal::getRegistrations().remove( pRegistration ); // 올라 있지 않은 단계를 빼는 것은 할 일이 없는 것이다
    }

    const AutomationStepRegistration* AutomationStepRegistry::find( string_view kind )
    {
        return AutomationStepRegistryInternal::getRegistrations().findByName( kind );
    }

    bool AutomationStepRegistry::isEngineStepKind( string_view kind )
    {
        static constexpr string_view kArrEngineStepKind[] = { "Press", "Release", "Tap", "MouseDelta", "MousePosition", "GamepadAxis", "Text",
                                                              "Variable", "Expect", "ExpectLog", "Screenshot", "ExpectImage", "CloseWindow",
                                                              "Pass", "Fail", "Skip", "ExpectExitWithin" };
        for ( const string_view engineKind : kArrEngineStepKind )
        {
            if ( engineKind == kind )
                return true;
        }
        return false;
    }

    bool AutomationStepRegistry::registerStartBlocker( const AutomationStartBlocker* pBlocker )
    {
        if ( pBlocker == nullptr || pBlocker->_pName == nullptr || pBlocker->_pIsBlocking == nullptr )
            return false;
        if ( AutomationStepRegistryInternal::getStartBlockers().add( pBlocker, pBlocker->_pName ) != RegistrationResult::Added )
        {
            SW_LOG_ERROR( "Automation start blocker <%#> is already defined - the second one is ignored", pBlocker->_pName );
            return false;
        }
        return true;
    }

    void AutomationStepRegistry::unregisterStartBlocker( const AutomationStartBlocker* pBlocker )
    {
        (void)AutomationStepRegistryInternal::getStartBlockers().remove( pBlocker ); // 올라 있지 않은 조건을 빼는 것은 할 일이 없는 것이다
    }

    bool AutomationStepRegistry::isStartBlocked()
    {
        for ( const AutomationStartBlocker* pBlocker : AutomationStepRegistryInternal::getStartBlockers().getItems() )
        {
            if ( pBlocker->_pIsBlocking() )
                return true;
        }
        return false;
    }

    AutomationStepRegistrar::AutomationStepRegistrar( const AutomationStepRegistration* pRegistration )
        : _pRegistration{ AutomationStepRegistry::registerStep( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationStepRegistrar::~AutomationStepRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationStepRegistry::unregisterStep( _pRegistration );
    }

    AutomationStartBlockerRegistrar::AutomationStartBlockerRegistrar( const AutomationStartBlocker* pBlocker )
        : _pBlocker{ AutomationStepRegistry::registerStartBlocker( pBlocker ) ? pBlocker : nullptr }
    {
    }

    AutomationStartBlockerRegistrar::~AutomationStartBlockerRegistrar()
    {
        if ( _pBlocker != nullptr )
            AutomationStepRegistry::unregisterStartBlocker( _pBlocker );
    }
} // namespace sw
