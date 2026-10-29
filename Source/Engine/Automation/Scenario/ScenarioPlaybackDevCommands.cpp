/**
 * @file ScenarioPlaybackDevCommands.cpp
 * @brief 시나리오 재생 명령 `scenario.play` · `scenario.stop`(배포 구성은 `*DevCommands.cpp` 를 짓지 않는다).
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"

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
