#include "pch.h"

#include "Engine/Config/NetSendThrottleSettings.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Connection/NetEndpoint.h"

#include "Engine/Config/ConfigManager.h"

namespace sw
{
    bool NetSendThrottleSettings::loadInto( NetEndpointConfig& inoutSettings )
    {
        NetSendThrottleSettings settings;
        if ( ConfigManager::readConfigFileInto( kConfigFile, settings ) != ConfigReadResult::Loaded )
        {
            SW_LOG_ERROR_ID( "NetSendThrottleSettings.ConfigMissing", "%# is missing or unreadable (the log above names the key) - the net endpoint stays closed", kConfigFile );
            return false;
        }
        const NetSendThrottleConfig throttle = settings.makeThrottleConfig();
        const expected<void>        valid    = NetSendThrottle::validate( throttle, settings._maxBytesPerSecond );
        if ( valid.has_value() == false )
        {
            SW_LOG_ERROR_ID( "NetSendThrottleSettings.ConfigInvalid", "%# %#: %# - the net endpoint stays closed", kConfigFile, valid.error().detail(), valid.error().message() );
            return false;
        }
        inoutSettings._maxBytesPerSecond = settings._maxBytesPerSecond;
        inoutSettings._sendThrottle      = throttle;
        return true;
    }
} // namespace sw
