#include "pch.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"

#include "Core/Console/DevCommandRegistry.h"
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

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        /** @brief `scenario.play <경로> [loop] [demo] [fixed]` — demo 는 사람이 손대면 멈춤, fixed 는 고정 프레임 시간. */
        bool runPlay( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() )
                return false;
            ScenarioPlayOptions options;
            for ( size_t index = 1; index < listArgument.size(); ++index )
            {
                const string& flag = listArgument[index];
                if ( flag == "loop" )
                    options._bLoop = true;
                else if ( flag == "demo" )
                    options._bStopOnHumanInput = true;
                else if ( flag == "fixed" )
                    options._bFixedDelta = true;
                else
                    return false;
            }
            const bool bStarted = ScenarioPlayback::play( AssetID{ listArgument[0] }, options );
            outReply            = bStarted ? "playing " + listArgument[0] : "could not play " + listArgument[0] + " (see the log)";
            return true;
        }

        bool runStop( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            const bool bPlaying = ScenarioPlayback::isPlaying();
            ScenarioPlayback::stop();
            outReply = bPlaying ? "stopped" : "nothing is playing";
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( ScenarioPlay, "scenario.play", "scenario.play <path> [loop] [demo] [fixed]",
                    "Play a scenario in the running game (loop: repeat, demo: stop when a person touches the keyboard or mouse, fixed: fixed frame time)", &runPlay );
    SW_DEV_COMMAND( ScenarioStop, "scenario.stop", "scenario.stop", "Stop the scenario that scenario.play started", &runStop );
} // namespace sw
#endif
