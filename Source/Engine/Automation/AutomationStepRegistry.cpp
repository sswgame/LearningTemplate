#include "pch.h"

#include "Engine/Automation/AutomationStepRegistry.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationStepRegistry" );

    namespace
    {
        struct AutomationStepRegistryInternal
        {
            /** @brief 등록된 단계 종류(등록 순서). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다. */
            static vector<const AutomationStepRegistration*>& getRegistrations()
            {
                static vector<const AutomationStepRegistration*> s_listRegistration;
                return s_listRegistration;
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
        if ( isEngineStepKind( pRegistration->_pKind ) || find( pRegistration->_pKind ) != nullptr )
        {
            SW_LOG_ERROR( "Automation step <%#> is already defined - the second one is ignored", pRegistration->_pKind );
            return false;
        }
        AutomationStepRegistryInternal::getRegistrations().push_back( pRegistration );
        return true;
    }

    void AutomationStepRegistry::unregisterStep( const AutomationStepRegistration* pRegistration )
    {
        vector<const AutomationStepRegistration*>& listRegistration = AutomationStepRegistryInternal::getRegistrations();
        for ( size_t index = 0; index < listRegistration.size(); ++index )
        {
            if ( listRegistration[index] != pRegistration )
                continue;
            listRegistration.erase( listRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    const AutomationStepRegistration* AutomationStepRegistry::find( string_view kind )
    {
        for ( const AutomationStepRegistration* pRegistration : AutomationStepRegistryInternal::getRegistrations() )
        {
            if ( string_view{ pRegistration->_pKind } == kind )
                return pRegistration;
        }
        return nullptr;
    }

    bool AutomationStepRegistry::isEngineStepKind( string_view kind )
    {
        static constexpr string_view kArrEngineStepKind[] = { "Press", "Release", "Tap", "MouseDelta", "GamepadAxis", "Text",
                                                              "Variable", "Expect", "ExpectLog", "CloseWindow",
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
