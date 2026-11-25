#include "pch.h"

#include "Engine/Foundation/Commandlet.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        struct CommandletInternal
        {
            /** @brief 등록된 작업(우선순위, 이름 순). Engine 이미지의 함수 정적이라 모듈이 바뀌어도 남는다(`AutomationStageResetRegistry` 와 같다). */
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const CommandletRegistration, "CommandletRegistry", RegistrationOrder::ByOrderThenName, true )

                /** @brief @p host 가 @p thisHost 를 포함하면 true 입니다. */
                static constexpr bool includesHost( CommandletHost host, CommandletHost thisHost )
            {
                return ( static_cast<uint8>( host ) & static_cast<uint8>( thisHost ) ) != 0;
            }
        };
    } // namespace

    CommandletState::CommandletState()
        : _bFailed{ SW_FALSE }
        , _failureExitCode{ kNoExitCode }
        , _bHeadless{ SW_FALSE }
    {
    }
} // namespace sw

namespace sw
{
    bool CommandletRegistration::registerTask( const CommandletRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pIsRequested == nullptr || pRegistration->_pRun == nullptr )
            return false;
        return CommandletInternal::getRegistrations().addLogged( pRegistration, "Headless task", pRegistration->_pName, pRegistration->_priority ) == RegistrationResult::Added;
    }

    void CommandletRegistration::unregisterTask( const CommandletRegistration* pRegistration )
    {
        (void)CommandletInternal::getRegistrations().remove( pRegistration ); // 등록되어 있지 않은 항목을 빼는 것은 할 일이 없다
    }

    CommandletResult CommandletRegistration::selectAndRun( span<const CommandletRegistration* const> listRegistration, CommandletContext& context, CommandletLeakBaseline leakBaseline )
    {
        const auto enterHeadless = [&context, leakBaseline]
        {
            if ( leakBaseline == CommandletLeakBaseline::Capture )
                context._state.enterHeadless();
            else
                context._state.markHeadless();
        };
        const CommandletHost          thisHost = context._bDedicatedServer ? CommandletHost::DedicatedServer : CommandletHost::Client;
        const CommandletRegistration* pChosen  = nullptr;
        for ( const CommandletRegistration* pTask : listRegistration )
        {
            if ( pTask->_pIsRequested( context._commandLine ) == false )
                continue;
            // 이 호스트에서 돌지 않는 작업이 하나라도 청해졌으면 다른 작업도 돌리지 않는다 — 우선순위가 앞선 작업만 돌고 나머지가 조용히 버려지지 않게.
            if ( CommandletInternal::includesHost( pTask->_host, thisHost ) == false )
            {
                SW_LOG_ERROR_ID( "Headless.CommandletWrongHost", "Headless task '%#' runs in the %#, not in this host", pTask->_pName,
                                 thisHost == CommandletHost::DedicatedServer ? "client build (App)" : "dedicated server" );
                enterHeadless();
                context._state._bFailed = SW_TRUE;
                return CommandletResult::Rejected;
            }
            if ( pChosen == nullptr )
                pChosen = pTask;
        }
        if ( pChosen == nullptr )
            return CommandletResult::NotRequested;
        enterHeadless();
        if ( pChosen->_pRun( context ) == false )
            context._state._bFailed = SW_TRUE;
        return CommandletResult::Ran;
    }

    bool CommandletRegistration::isAnyRequested( const CommandLineManager& commandLine )
    {
        const RegistrationList<const CommandletRegistration>& registrations = CommandletInternal::getRegistrations();
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            if ( registrations.getAt( index )->_pIsRequested( commandLine ) )
                return true;
        }
        return false;
    }

    CommandletResult CommandletRegistration::runRequested( CommandletContext& context )
    {
        const RegistrationList<const CommandletRegistration>& registrations = CommandletInternal::getRegistrations();
        vector<const CommandletRegistration*>                 listTask;
        listTask.reserve( registrations.getCount() );
        for ( uint32 index = 0; index < registrations.getCount(); ++index )
        {
            listTask.push_back( registrations.getAt( index ) );
        }
        return selectAndRun( listTask, context, CommandletLeakBaseline::Capture );
    }
} // namespace sw
