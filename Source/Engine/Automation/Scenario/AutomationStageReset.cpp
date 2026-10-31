#include "pch.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationStageReset" );

    namespace
    {
        struct AutomationStageResetInternal
        {
            /** @brief 등록된 되돌리기(등록 순서). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다(`AutomationWatchRegistry` 와 같다). */
            static RegistrationList<const AutomationStageResetRegistration>& getRegistrations()
            {
                static RegistrationList<const AutomationStageResetRegistration> s_registration{ "AutomationStageResetRegistry", RegistrationOrder::Insertion, true };
                return s_registration;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationStageResetRegistry::registerReset( const AutomationStageResetRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pFunction == nullptr )
            return false;
        return AutomationStageResetInternal::getRegistrations().addLogged( pRegistration, "Automation stage reset", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void AutomationStageResetRegistry::unregisterReset( const AutomationStageResetRegistration* pRegistration )
    {
        (void)AutomationStageResetInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 항목을 빼는 것은 할 일이 없다
    }

    uint32 AutomationStageResetRegistry::resetAll()
    {
        const RegistrationList<const AutomationStageResetRegistration>& registrations = AutomationStageResetInternal::getRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            const AutomationStageResetRegistration* pRegistration = registrations.getAt( index );
            SW_LOG_INFO( "[Scenario] stage reset: %#", pRegistration->_pName );
            pRegistration->_pFunction();
        }
        return registrations.getCount();
    }

    AutomationStageResetRegistrar::AutomationStageResetRegistrar( const AutomationStageResetRegistration* pRegistration )
        : _pRegistration{ AutomationStageResetRegistry::registerReset( pRegistration ) ? pRegistration : nullptr }
    {
    }

    AutomationStageResetRegistrar::~AutomationStageResetRegistrar()
    {
        if ( _pRegistration != nullptr )
            AutomationStageResetRegistry::unregisterReset( _pRegistration );
    }
} // namespace sw
