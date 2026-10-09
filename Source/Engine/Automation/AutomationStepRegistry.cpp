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

    AutomationStepRegistrar::AutomationStepRegistrar( const AutomationStepRegistration* pRegistration )
        : _pRegistration{ AutomationStepRegistry::registerStep( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationStepRegistrar::~AutomationStepRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationStepRegistry::unregisterStep( _pRegistration );
    }
} // namespace sw
