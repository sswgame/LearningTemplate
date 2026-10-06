#include "pch.h"

#include "Engine/Automation/AutomationProbe.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationProbe" );

    namespace
    {
        struct AutomationProbeInternal
        {
            /** @brief 등록된 탐침(등록 순서). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. */
            static vector<const AutomationProbeRegistration*>& getRegistrations()
            {
                static vector<const AutomationProbeRegistration*> s_listRegistration;
                return s_listRegistration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationProbes::registerProbe( const AutomationProbeRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pFunction == nullptr )
            return false;
        if ( find( pRegistration->_pName ) != nullptr )
        {
            SW_LOG_ERROR( "Automation probe '%#' is registered twice - the second one is ignored", pRegistration->_pName );
            return false;
        }
        AutomationProbeInternal::getRegistrations().push_back( pRegistration );
        return true;
    }

    void AutomationProbes::unregisterProbe( const AutomationProbeRegistration* pRegistration )
    {
        vector<const AutomationProbeRegistration*>& listRegistration = AutomationProbeInternal::getRegistrations();
        for ( size_t index = 0; index < listRegistration.size(); ++index )
        {
            if ( listRegistration[index] != pRegistration )
                continue;
            listRegistration.erase( listRegistration.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    const AutomationProbeRegistration* AutomationProbes::find( string_view name )
    {
        for ( const AutomationProbeRegistration* pRegistration : AutomationProbeInternal::getRegistrations() )
        {
            if ( string_view{ pRegistration->_pName } == name )
                return pRegistration;
        }
        return nullptr;
    }

    AutomationProbeRegistrar::AutomationProbeRegistrar( const AutomationProbeRegistration* pRegistration )
        : _pRegistration{ AutomationProbes::registerProbe( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationProbeRegistrar::~AutomationProbeRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationProbes::unregisterProbe( _pRegistration );
    }
} // namespace sw
