#include "pch.h"

#include "Engine/Config/NetSendThrottleSettings.h"

#include "Core/Log/Logger.h"
#include "Core/Network/Connection/NetEndpoint.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    bool NetSendThrottleSettings::loadInto( NetEndpointConfig& inoutSettings )
    {
        const expected<NetSendThrottleSettings> settings = ConfigManager::readConfig( kConfigFile );
        if ( settings.has_value() == false )
        {
            SW_LOG_ERROR_ID( "NetSendThrottleSettings.ConfigInvalid", "%# (%#) - the net endpoint stays closed", settings.error().message(), settings.error().detail() );
            return false;
        }
        inoutSettings._maxBytesPerSecond = settings->_maxBytesPerSecond;
        inoutSettings._sendThrottle      = settings->makeThrottleConfig();
        return true;
    }

    expected<void> NetSendThrottleSettings::validate() const
    {
        return NetSendThrottle::validate( makeThrottleConfig(), _maxBytesPerSecond );
    }
} // namespace sw
