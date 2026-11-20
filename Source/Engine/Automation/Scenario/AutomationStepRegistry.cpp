#include "pch.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct AutomationStepRegistryInternal
        {
            /** @brief 단계 종류(등록 순서). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다. */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const AutomationStepRegistration, "AutomationStepRegistry", RegistrationOrder::Insertion, true )

                /** @brief 차단 조건(등록 순서)입니다. */
                static SW_DEFINE_REGISTRATION_LIST( getBlockers, const AutomationBlocker, "AutomationBlocker", RegistrationOrder::Insertion, true )

                /** @brief 검사 함수만 올린 등록(`SW_AUTOMATION_STEP_VALIDATOR` — 단계를 등록하는 모듈이 없는 프로세스가 속성을 검사하게 한다)입니다. */
                static SW_DEFINE_REGISTRATION_LIST( getValidators, const AutomationStepValidatorRegistration, "AutomationStepValidator", RegistrationOrder::Insertion, true )

                /**
                 * @brief @p isInScope 가 고른 조건 가운데 지금 진행 중인 첫 조건입니다. 해당 없음 · 준비됨은 막지 않는다.
                 */
                template <typename TScopeFilter>
                static AutomationWaitingCondition findWaiting( TScopeFilter isInScope )
            {
                for ( const AutomationBlocker* pBlocker : getBlockers().getItems() )
                {
                    if ( isInScope( pBlocker->_scope ) == false )
                        continue;
                    const AutomationReadiness readiness = pBlocker->_pReadiness();
                    if ( readiness.isWaiting() )
                        return AutomationWaitingCondition{ pBlocker->_pName, readiness.getWhat(), readiness.getProgress() };
                }
                return AutomationWaitingCondition{};
            }

            /** @brief @p minimumScope 이상(넓은 범위가 좁은 범위를 포함)인 조건 가운데 지금 막는 것입니다. `Finish` · `RenderStable` 은 어느 범위에도 들지 않는다(enum 순서가 그 아래다). */
            static AutomationWaitingCondition findBlocking( AutomationBlockScope minimumScope )
            {
                return findWaiting( [minimumScope]( AutomationBlockScope scope )
                { return scope >= minimumScope; } );
            }

            /** @brief 범위가 정확히 @p scope 인 조건 가운데 지금 막는 것입니다(`Finish` · `RenderStable`). */
            static AutomationWaitingCondition findExactly( AutomationBlockScope exactScope )
            {
                return findWaiting( [exactScope]( AutomationBlockScope scope )
                { return scope == exactScope; } );
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
        return AutomationStepRegistryInternal::getValidators().addLogged( pRegistration, "Automation step validator", pRegistration->_pKind ) == RegistrationResult::Added;
    }

    void AutomationStepRegistry::unregisterValidator( const AutomationStepValidatorRegistration* pRegistration )
    {
        (void)AutomationStepRegistryInternal::getValidators().remove( pRegistration ); // 올라와 있지 않은 검사 함수를 빼는 것은 할 일이 없다
    }

    const AutomationStepValidatorRegistration* AutomationStepRegistry::findValidator( string_view kind )
    {
        return AutomationStepRegistryInternal::getValidators().findByName( kind );
    }

    bool AutomationStepRegistry::registerBlocker( const AutomationBlocker* pBlocker )
    {
        const bool bInvalidBlockerRegistration = pBlocker == nullptr || pBlocker->_pName == nullptr || pBlocker->_pReadiness == nullptr;
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
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Start ).isWaiting();
    }

    AutomationWaitingCondition AutomationStepRegistry::findCaptureBlocker()
    {
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Capture );
    }

    AutomationWaitingCondition AutomationStepRegistry::findClockHolder()
    {
        return AutomationStepRegistryInternal::findBlocking( AutomationBlockScope::Clock );
    }

    AutomationWaitingCondition AutomationStepRegistry::findFinishBlocker()
    {
        return AutomationStepRegistryInternal::findExactly( AutomationBlockScope::Finish );
    }

    AutomationWaitingCondition AutomationStepRegistry::findStabilityCondition()
    {
        return AutomationStepRegistryInternal::findExactly( AutomationBlockScope::RenderStable );
    }

    string AutomationStepRegistry::describeActiveBlockers()
    {
        StringBuilder<constant::buffer::kMax256> text;
        for ( const AutomationBlocker* pBlocker : AutomationStepRegistryInternal::getBlockers().getItems() )
        {
            if ( pBlocker->_scope == AutomationBlockScope::RenderStable )
                continue; // 정착 조건은 그것을 기다리는 사진 단계만 막는다 — 그 단계의 대기 까닭(`holdForRenderStable`)이 이름 · 진척을 찍는다
            const AutomationReadiness readiness = pBlocker->_pReadiness();
            if ( readiness.isWaiting() == false )
                continue;
            if ( text.size() != 0 )
                text.append( ", " );
            text.appendFormat( "%# (%#, progress %#)", pBlocker->_pName, readiness.getWhat(), readiness.getProgress() );
        }
        return text.size() == 0 ? string( "none" ) : string( text.view() );
    }

    expected<void> AutomationStepRegistry::configureConditions( const JSONValue& conditions )
    {
        for ( const AutomationBlocker* pBlocker : AutomationStepRegistryInternal::getBlockers().getItems() )
        {
            if ( pBlocker->_pConfigure == nullptr )
                continue;
            if ( conditions.isValid() == false || conditions.has( pBlocker->_pName ) == false )
                return unexpected( Error{ ErrorKind::NotFound, "ScenarioRun.json _conditions needs an entry for the registered condition", pBlocker->_pName } );
            if ( expected<void> configured = pBlocker->_pConfigure( conditions.get( pBlocker->_pName ) ); configured.has_value() == false )
                return configured;
        }
        return {};
    }
} // namespace sw
