#include "pch.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"

#include "Core/Log/Logger.h"

#include "Engine/Foundation/EngineServices.h"

SW_LOG_CALLER( "ScenarioPlayback" );

namespace sw
{
    bool ScenarioPlayback::play( const AssetID& scenarioPath, const ScenarioPlayOptions& options )
    {
        IScenarioPlayer* const pPlayer = engine::findSubsystem<IScenarioPlayer>();
        if ( pPlayer == nullptr )
        {
            SW_LOG_WARNING( "[Scenario] cannot play '%#' - this host has no scenario player (dedicated server or automation left out)", scenarioPath.c_str() );
            return false;
        }
        return pPlayer->play( scenarioPath, options );
    }

    void ScenarioPlayback::stop()
    {
        if ( IScenarioPlayer* const pPlayer = engine::findSubsystem<IScenarioPlayer>() )
            pPlayer->stopPlayback();
    }

    bool ScenarioPlayback::isPlaying()
    {
        const IScenarioPlayer* const pPlayer = engine::findSubsystem<IScenarioPlayer>();
        return pPlayer != nullptr && pPlayer->isPlaying();
    }
} // namespace sw
