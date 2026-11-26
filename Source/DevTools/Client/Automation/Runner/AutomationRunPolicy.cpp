#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationRunPolicy.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    expected<AutomationRunPolicy> AutomationRunPolicy::load()
    {
        expected<AutomationRunPolicy> policy = ConfigManager::readConfig( kConfigFile );
        if ( policy.has_value() == false )
            return policy;
        if ( expected<void> configured = AutomationStepRegistry::configureConditions( policy->_mapConditionThreshold ); configured.has_value() == false )
            return unexpected( configured.error() );
        return policy;
    }
} // namespace sw
