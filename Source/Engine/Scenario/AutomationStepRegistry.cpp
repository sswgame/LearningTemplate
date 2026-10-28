#include "pch.h"

#include "Engine/Scenario/AutomationStepRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/String/RegistrationList.h"

namespace sw
{
    SW_LOG_CALLER( "AutomationStepRegistry" );

    namespace
    {
        struct AutomationStepRegistryInternal
        {
            /**
             * @brief 이 레지스트리의 목록 셋입니다. Engine 이미지의 함수 정적 하나가 쥐고, 모듈이 바뀌어도 남는다.
             * @details 단계 종류(등록 순서) · 차단 조건(등록 순서) · 검사 함수만 올린 등록(`SW_AUTOMATION_STEP_VALIDATOR` — 단계를 등록하는 모듈이 없는 프로세스가 속성을 검사하게 한다)이다.
             */
            struct Registries
            {
                RegistrationList<const AutomationStepRegistration>          _step{ "AutomationStepRegistry", RegistrationOrder::Insertion, true };
                RegistrationList<const AutomationBlocker>                   _blocker{ "AutomationBlocker", RegistrationOrder::Insertion, true };
                RegistrationList<const AutomationStepValidatorRegistration> _validator{ "AutomationStepValidator", RegistrationOrder::Insertion, true };
            };

            static Registries& getRegistries()
            {
                static Registries s_registries;
                return s_registries;
            }

            static RegistrationList<const AutomationStepRegistration>& getRegistrations() { return getRegistries()._step; }

            static RegistrationList<const AutomationBlocker>& getBlockers() { return getRegistries()._blocker; }

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

    bool AutomationStepRegistry::registerValidator( const AutomationStepValidatorRegistration* pRegistration )
    {
        const bool bInvalidValidatorRegistration = pRegistration == nullptr || pRegistration->_pKind == nullptr || pRegistration->_pValidate == nullptr;
        if ( bInvalidValidatorRegistration )
            return false;
        return AutomationStepRegistryInternal::getRegistries()._validator.addLogged( pRegistration, "Automation step validator", pRegistration->_pKind ) == RegistrationResult::Added;
    }

    void AutomationStepRegistry::unregisterValidator( const AutomationStepValidatorRegistration* pRegistration )
    {
        (void)AutomationStepRegistryInternal::getRegistries()._validator.remove( pRegistration ); // 올라와 있지 않은 검사 함수를 빼는 것은 할 일이 없다
    }

    const AutomationStepValidatorRegistration* AutomationStepRegistry::findValidator( string_view kind )
    {
        return AutomationStepRegistryInternal::getRegistries()._validator.findByName( kind );
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
