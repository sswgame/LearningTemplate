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
                static RegistrationList<const AutomationStepRegistration> s_registration{ "AutomationStepRegistry", RegistrationOrder::Insertion, true };
                return s_registration;
            }

            /** @brief 등록된 차단 조건(등록 순서)입니다. */
            static RegistrationList<const AutomationBlocker>& getBlockers()
            {
                static RegistrationList<const AutomationBlocker> s_blocker{ "AutomationBlocker", RegistrationOrder::Insertion, true };
                return s_blocker;
            }

            /** @brief 범위가 @p minimumScope 이상인 조건 중 지금 참인 첫 조건의 이름입니다. 없으면 nullptr 입니다. */
            static const utf8* findBlocking( AutomationBlockScope minimumScope )
            {
                for ( const AutomationBlocker* pBlocker : getBlockers().getItems() )
                {
                    if ( pBlocker->_scope >= minimumScope && pBlocker->_pIsBlocking() )
                        return pBlocker->_pName;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationStepRegistry::registerStep( const AutomationStepRegistration* pRegistration )
    {
        const bool bInvalidStepRegistration = pRegistration == nullptr || pRegistration->_pKind == nullptr || ( pRegistration->_pRun == nullptr && pRegistration->_pPrepare == nullptr );
        if ( bInvalidStepRegistration )
            return false;
        return AutomationStepRegistryInternal::getRegistrations().addLogged( pRegistration, "Automation step", pRegistration->_pKind ) == RegistrationResult::Added;
    }

    void AutomationStepRegistry::unregisterStep( const AutomationStepRegistration* pRegistration )
    {
        (void)AutomationStepRegistryInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 단계를 빼는 것은 할 일이 없다
    }

    const AutomationStepRegistration* AutomationStepRegistry::find( string_view kind )
    {
        return AutomationStepRegistryInternal::getRegistrations().findByName( kind );
    }

    bool AutomationStepRegistry::registerBlocker( const AutomationBlocker* pBlocker )
    {
        const bool bInvalidBlockerRegistration = pBlocker == nullptr || pBlocker->_pName == nullptr || pBlocker->_pIsBlocking == nullptr;
        if ( bInvalidBlockerRegistration )
            return false;
        return AutomationStepRegistryInternal::getBlockers().addLogged( pBlocker, "Automation blocker", pBlocker->_pName ) == RegistrationResult::Added;
    }

    void AutomationStepRegistry::unregisterBlocker( const AutomationBlocker* pBlocker )
    {
        (void)AutomationStepRegistryInternal::getBlockers().remove( pBlocker ); // 등록되어 있지 않은 조건을 빼는 것은 할 일이 없다
    }

    bool AutomationStepRegistry::isStartBlocked()
    {
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Start ) != nullptr;
    }

    const utf8* AutomationStepRegistry::findCaptureBlocker()
    {
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Capture );
    }

    const utf8* AutomationStepRegistry::findClockHolder()
    {
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Clock );
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

    AutomationBlockerRegistrar::AutomationBlockerRegistrar( const AutomationBlocker* pBlocker )
        : _pBlocker{ AutomationStepRegistry::registerBlocker( pBlocker ) ? pBlocker : nullptr }
    {
    }

    AutomationBlockerRegistrar::~AutomationBlockerRegistrar()
    {
        if ( _pBlocker != nullptr )
            AutomationStepRegistry::unregisterBlocker( _pBlocker );
    }
} // namespace sw
