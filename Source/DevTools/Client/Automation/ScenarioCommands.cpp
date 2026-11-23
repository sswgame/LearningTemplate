#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Scenario/ScenarioPlayback.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"

namespace sw
{
    namespace
    {
        /** @brief 도는 게임에서 시나리오를 재생 · 정지하는 개발 명령(`scenario.*`)입니다. */
        struct ScenarioCommandsInternal
        {
            /** @brief `scenario.play <경로> [loop] [demo] [fixed]` — demo 는 사람이 손대면 멈춤, fixed 는 고정 프레임 시간. */
            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() )
                    return false;
                ScenarioPlayOptions options;
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    const string& flag = listArgument[index];
                    if ( flag == "loop" )
                        options._bLoop = SW_TRUE;
                    else if ( flag == "demo" )
                        options._bStopOnHumanInput = true;
                    else if ( flag == "fixed" )
                        options._bFixedDelta = true;
                    else
                        return false;
                }
                const bool                               bStarted = ScenarioPlayback::play( AssetID{ listArgument[0] }, options );
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( bStarted ? "playing %#" : "could not play %# (see the log)", listArgument[0] );
                outReply.assign( reply.view() );
                return true;
            }

            static bool runStop( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const bool bPlaying = ScenarioPlayback::isPlaying();
                ScenarioPlayback::stop();
                outReply = bPlaying ? "stopped" : "nothing is playing";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ScenarioPlay, "scenario.play", "scenario.play <path> [loop] [demo] [fixed]",
                    "Play a scenario in the running game (loop: repeat, demo: stop when a person touches the keyboard or mouse, fixed: fixed frame time)", &ScenarioCommandsInternal::runPlay );
    SW_DEV_COMMAND( ScenarioStop, "scenario.stop", "scenario.stop", "Stop the scenario that scenario.play started", &ScenarioCommandsInternal::runStop );
} // namespace sw
