#include "pch.h"

#include "ModuleManager/Host/HostServiceUtil.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    bool HostServiceUtil::startAll( SubsystemCollection& outParts, vector_reference<const HostServiceID> listPart, const HostServiceStartDelegate& start,
                                    const HostServiceStopDelegate& stop, const CommandLineManager& commandLine )
    {
        if ( startHostServices( outParts, listPart, start, stop ) == false )
        {
            SW_LOG_ERROR_ID( "HostServices.HostServicesFailedStart", "Host services failed to start: %#", outParts.getError().c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "HostServices.HostServicesUp", "Host services up: %#", describeHostServiceOrder( outParts ).c_str() );
        warnUnknownGlobalOverrides( commandLine );
        return true;
    }

    void HostServiceUtil::warnUnknownGlobalOverrides( const CommandLineManager& commandLine )
    {
        const vector<string> listPendingName = commandLine.collectPendingGlobalNames();
        for ( const string& pendingName : listPendingName )
        {
            if ( engine::getSubsystem<GlobalVariableManager>().findVariable( pendingName ) == nullptr )
                SW_LOG_WARNING_ID( "HostServices.NoSuchGlobalVariable", "%#: no such global variable. Ignored", pendingName.c_str() );
        }
    }
} // namespace sw
