#include "pch.h"

#include "Engine/Foundation/HeadlessTask.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        struct HeadlessTaskInternal
        {
            /** @brief 등록된 작업(우선순위, 이름 순). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다(`AutomationStageResetRegistry` 와 같다). */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const HeadlessTaskRegistration, "HeadlessTaskRegistry", RegistrationOrder::ByOrderThenName, true )

                /** @brief @p host 가 @p thisHost 를 포함하면 true 입니다. */
                static constexpr bool includesHost( HeadlessTaskHost host, HeadlessTaskHost thisHost )
            {
                return ( static_cast<uint8>( host ) & static_cast<uint8>( thisHost ) ) != 0;
            }
        };
    } // namespace

    HeadlessTaskState::HeadlessTaskState()
        : _bFailed{ SW_FALSE }
        , _failureExitCode{ kNoExitCode }
        , _bHeadless{ SW_FALSE }
    {
    }
} // namespace sw

namespace sw
{
    bool HeadlessTaskRegistration::registerTask( const HeadlessTaskRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pIsRequested == nullptr || pRegistration->_pRun == nullptr )
            return false;
        return HeadlessTaskInternal::getRegistrations().addLogged( pRegistration, "Headless task", pRegistration->_pName, pRegistration->_priority ) == RegistrationResult::Added;
    }

    void HeadlessTaskRegistration::unregisterTask( const HeadlessTaskRegistration* pRegistration )
    {
        (void)HeadlessTaskInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 항목을 빼는 것은 할 일이 없다
    }

    HeadlessTaskResult HeadlessTaskRegistration::selectAndRun( span<const HeadlessTaskRegistration* const> listRegistration, HeadlessTaskContext& context, HeadlessLeakBaseline leakBaseline )
    {
        const auto enterHeadless = [&context, leakBaseline]
        {
            if ( leakBaseline == HeadlessLeakBaseline::Capture )
                context._state.enterHeadless();
            else
                context._state.markHeadless();
        };
        const HeadlessTaskHost          thisHost = context._bDedicatedServer ? HeadlessTaskHost::DedicatedServer : HeadlessTaskHost::Client;
        const HeadlessTaskRegistration* pChosen  = nullptr;
        for ( const HeadlessTaskRegistration* pTask : listRegistration )
        {
            if ( pTask->_pIsRequested( context._commandLine ) == false )
                continue;
            // 이 호스트에서 돌지 않는 작업이 하나라도 청해졌으면 다른 작업도 돌리지 않는다 — 우선순위가 앞선 작업만 돌고 나머지가 조용히 버려지지 않게.
            if ( HeadlessTaskInternal::includesHost( pTask->_host, thisHost ) == false )
            {
                SW_LOG_ERROR_ID( "Headless.HeadlessTaskWrongHost", "Headless task '%#' runs in the %#, not in this host", pTask->_pName,
                                 thisHost == HeadlessTaskHost::DedicatedServer ? "client build (App)" : "dedicated server" );
                enterHeadless();
                context._state._bFailed = SW_TRUE;
                return HeadlessTaskResult::Rejected;
            }
            if ( pChosen == nullptr )
                pChosen = pTask;
        }
        if ( pChosen == nullptr )
            return HeadlessTaskResult::NotRequested;
        enterHeadless();
        if ( pChosen->_pRun( context ) == false )
            context._state._bFailed = SW_TRUE;
        return HeadlessTaskResult::Ran;
    }

    bool HeadlessTaskRegistration::isAnyRequested( const CommandLineManager& commandLine )
    {
        const RegistrationList<const HeadlessTaskRegistration>& registrations = HeadlessTaskInternal::getRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            if ( registrations.getAt( index )->_pIsRequested( commandLine ) )
                return true;
        }
        return false;
    }

    HeadlessTaskResult HeadlessTaskRegistration::runRequested( HeadlessTaskContext& context )
    {
        const RegistrationList<const HeadlessTaskRegistration>& registrations = HeadlessTaskInternal::getRegistrations();
        vector<const HeadlessTaskRegistration*>                 listTask;
        listTask.reserve( registrations.getCount() );
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            listTask.push_back( registrations.getAt( index ) );
        }
        return selectAndRun( listTask, context, HeadlessLeakBaseline::Capture );
    }
} // namespace sw
