/**
 * @file Engine/Animation/Scene/AnimationDevCommands.cpp
 * @brief 활성 씬의 애니메이션 되감기 개발 명령(`anim.rewind` · `anim.rewind.seconds` · `anim.rewind.scrub`)입니다. Shipping 에는 없습니다.
 * @details 명령은 소유 코드 옆에 `SW_DEV_COMMAND` 로 둡니다(언리얼 `FAutoConsoleCommand`). 활성 씬은 씬 층(아래)에서 찾는다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"

    #include "Engine/Animation/Scene/AnimationRewind.h"
    #include "Engine/Animation/Scene/AnimationSystem.h"
    #include "Engine/Foundation/EngineServices.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct AnimationDevCommandsInternal
        {
            /** @brief 활성 씬의 애니메이션 시스템입니다(없으면 nullptr). */
            static AnimationSystem* findActiveAnimationSystem()
            {
                GameObjectManager* pManager = engine::findActiveObjectManager();
                return pManager != nullptr ? &pManager->getSubsystem<AnimationSystem>() : nullptr;
            }

            /** @brief 되감기 상태 한 줄입니다. */
            static string describeRewind( const AnimationRewindRecorder& rewind )
            {
                string line = string( "rewind " ) + ( AnimationRewindRecorder::isRecordingRequested() ? "on" : "off" ) + ", window " +
                              to_string( AnimationRewindRecorder::getRequestedWindowSeconds() ) + " s, " + to_string( static_cast<uint32>( rewind.getTracks().size() ) ) +
                              " tracks, " + to_string( rewind.getByteCount() / 1024u ) + " KB, history " +
                              to_string( static_cast<float32>( rewind.getLatestTime() - rewind.getEarliestTime() ) ) + " s";
                if ( rewind.isScrubbing() )
                    line += ", scrubbing " + to_string( static_cast<float32>( rewind.getLatestTime() - rewind.getScrubTime() ) ) + " s ago";
                return line;
            }

            static bool runAnimationRewind( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn = false;
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    AnimationRewindRecorder::setRecordingRequested( bOn );
                }
                const AnimationSystem* pSystem = findActiveAnimationSystem();
                outReply                       = pSystem != nullptr ? describeRewind( pSystem->getRewind() )
                                                                    : string( "rewind " ) + ( AnimationRewindRecorder::isRecordingRequested() ? "on" : "off" ) + " (no active scene)";
                return true;
            }

            static bool runAnimationRewindSeconds( const vector<string>& listArgument, string& outReply )
            {
                float32    seconds                 = 0.0f;
                const bool bInvalidWindowArguments = listArgument.size() != 1 || StringUtil::parseFloat( listArgument[0], seconds ) == false || seconds <= 0.0f;
                if ( bInvalidWindowArguments )
                    return false;
                AnimationRewindRecorder::setRequestedWindowSeconds( seconds );
                outReply = "rewind window = " + to_string( AnimationRewindRecorder::getRequestedWindowSeconds() ) + " s";
                return true;
            }

            static bool runAnimationRewindScrub( const vector<string>& listArgument, string& outReply )
            {
                AnimationSystem* pSystem = findActiveAnimationSystem();
                if ( pSystem == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                AnimationRewindRecorder& rewind = pSystem->getRewind();
                if ( listArgument.size() == 1 && StringUtil::equals( listArgument[0], "resume", true ) )
                {
                    rewind.clearScrub();
                    outReply = describeRewind( rewind );
                    return true;
                }
                float32    secondsAgo              = 0.0f;
                const bool bInvalidRewindArguments = listArgument.size() != 1 || StringUtil::parseFloat( listArgument[0], secondsAgo ) == false || secondsAgo < 0.0f;
                if ( bInvalidRewindArguments )
                    return false;
                if ( rewind.getTracks().empty() )
                {
                    outReply = "nothing recorded - turn on with 'anim.rewind on'";
                    return false;
                }
                rewind.setScrubTime( rewind.getLatestTime() - static_cast<float64>( secondsAgo ) );
                outReply = describeRewind( rewind );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( AnimationRewind, "anim.rewind", "anim.rewind [on|off]", "Show or switch animation rewind recording (gv_animationRewind)",
                    &AnimationDevCommandsInternal::runAnimationRewind );
    SW_DEV_COMMAND( AnimationRewindSeconds, "anim.rewind.seconds", "anim.rewind.seconds <seconds>", "Set how many seconds of animation history are kept",
                    &AnimationDevCommandsInternal::runAnimationRewindSeconds );
    SW_DEV_COMMAND( AnimationRewindScrub, "anim.rewind.scrub", "anim.rewind.scrub <seconds ago>|resume",
                    "Freeze animation and show the recorded poses of the active scene that many seconds ago, or resume",
                    &AnimationDevCommandsInternal::runAnimationRewindScrub );
} // namespace sw

#endif
