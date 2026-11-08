#include "pch.h"

#include "Engine/Automation/Scenario/AutomationStageReset.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct AutomationStageResetInternal
        {
            /** @brief 등록된 되돌리기(등록 순서). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다(`AutomationWatchRegistry` 와 같다). */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const AutomationStageResetRegistration, "AutomationStageResetRegistry", RegistrationOrder::Insertion, true )
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
            SW_LOG_INFO_ID( "AutomationStageReset.StageReset", "[Scenario] stage reset: %#", pRegistration->_pName );
            pRegistration->_pFunction();
        }
        return registrations.getCount();
    }
} // namespace sw
