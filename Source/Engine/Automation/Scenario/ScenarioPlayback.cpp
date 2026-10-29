#include "pch.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"

#include "Core/Log/Logger.h"

SW_LOG_CALLER( "ScenarioPlayback" );

namespace sw
{
    namespace
    {
        /** @brief 걸린 재생기입니다. 호스트가 세울 때 걸고 내리기 전에 푼다 — 게임 스레드에서만 만진다. */
        IScenarioPlayer* s_pPlayer = nullptr;
    } // namespace

    void ScenarioPlayback::bindPlayer( IScenarioPlayer* pPlayer )
    {
        s_pPlayer = pPlayer;
    }

    bool ScenarioPlayback::play( const AssetID& scenarioPath, const ScenarioPlayOptions& options )
    {
        if ( s_pPlayer == nullptr )
        {
            SW_LOG_WARNING( "[Scenario] cannot play '%#' - this host has no scenario player (dedicated server or automation left out)", scenarioPath.c_str() );
            return false;
        }
        return s_pPlayer->play( scenarioPath, options );
    }

    void ScenarioPlayback::stop()
    {
        if ( s_pPlayer != nullptr )
            s_pPlayer->stopPlayback();
    }

    bool ScenarioPlayback::isPlaying()
    {
        return s_pPlayer != nullptr && s_pPlayer->isPlaying();
    }
} // namespace sw
