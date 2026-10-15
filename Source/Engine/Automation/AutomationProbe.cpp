#include "pch.h"

#include "Engine/Automation/AutomationProbe.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationProbe" );

    namespace
    {
        struct AutomationProbeInternal
        {
            /** @brief 등록된 탐침(등록 순서 · 이름은 대소문자를 가린다). Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다. 핫 리로드가 올리는 교체 이미지의 같은 이름은 대체한다(`RegistrationReloadScope`). */
            static RegistrationList<const AutomationProbeRegistration>& getRegistrations()
            {
                static RegistrationList<const AutomationProbeRegistration> s_registration{ "AutomationProbeRegistry", RegistrationOrder::Insertion, true };
                return s_registration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationProbes::registerProbe( const AutomationProbeRegistration* pRegistration )
    {
        const bool bInvalidProbeRegistration =
            pRegistration == nullptr || pRegistration->_pName == nullptr || ( pRegistration->_pFunction == nullptr ) == ( pRegistration->_pQueryFunction == nullptr );
        if ( bInvalidProbeRegistration )
            return false;
        if ( AutomationProbeInternal::getRegistrations().add( pRegistration, pRegistration->_pName ) != RegistrationResult::Added )
        {
            SW_LOG_ERROR( "Automation probe '%#' is registered twice - the second one is ignored", pRegistration->_pName );
            return false;
        }
        return true;
    }

    void AutomationProbes::releaseUnusedStorage()
    {
        AutomationProbeInternal::getRegistrations().shrinkToFit();
    }

    void AutomationProbes::unregisterProbe( const AutomationProbeRegistration* pRegistration )
    {
        (void)AutomationProbeInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 탐침을 빼는 것은 할 일이 없다
    }

    const AutomationProbeRegistration* AutomationProbes::find( string_view name )
    {
        return AutomationProbeInternal::getRegistrations().findByName( name );
    }

    bool AutomationProbes::read( const AutomationProbeRegistration& registration, const AutomationProbeQuery& query, AutomationProbeValue& outValue )
    {
        if ( registration._pQueryFunction != nullptr )
            return registration._pQueryFunction( query, outValue );
        float64 number = 0.0;
        if ( registration._pFunction( query._pManager, number ) == false )
            return false;
        outValue.setNumber( number );
        return true;
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
