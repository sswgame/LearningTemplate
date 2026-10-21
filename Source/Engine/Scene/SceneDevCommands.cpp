/**
 * @file SceneDevCommands.cpp
 * @brief 활성 씬을 다루는 개발 명령(순간이동 · 태그 · 애니메이션 되감기)입니다. Shipping 에는 없습니다.
 * @details 명령은 소유 코드 옆에 `SW_DEV_COMMAND` 로 둡니다(언리얼 `FAutoConsoleCommand`). 활성 씬을 찾는 명령은 씬(티어 7)이 아니면 둘 곳이 없어
 *          애니메이션 되감기(`Object/SceneAnimation/AnimationRewind`)도 여기 둡니다 — 컴포넌트 모델(6)은 씬을 모릅니다.
 */
#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/String/TagID.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Object/SceneAnimation/AnimationSystem.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Scene/Scene.h"
    #include "Engine/Scene/SceneCooker.h"
    #include "Engine/Scene/SceneManager.h"
    #include "Engine/Scene/WorldStreamingSubsystem.h"

namespace sw
{
    namespace
    {
        struct SceneDevCommandsInternal
        {
            static bool runTeleport( const vector<string>& listArgument, string& outReply )
            {
                float3     position{};
                const bool bInvalidMoveArguments = listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], position._x ) == false ||
                                                   StringUtil::parseFloat( listArgument[2], position._y ) == false || StringUtil::parseFloat( listArgument[3], position._z ) == false;
                if ( bInvalidMoveArguments )
                    return false;
                GameObject*     pObject   = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SceneComponent* pRootNode = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRootNode == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' with a transform in the active scene";
                    return false;
                }
                pRootNode->teleportTo( position );
                outReply = "teleported " + listArgument[0];
                return true;
            }

            static bool runTagAdd( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 || listArgument[1].empty() )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' in the active scene";
                    return false;
                }
                pObject->addTag( TagID::request( listArgument[1] ) );
                outReply = "tagged " + listArgument[0] + " with " + listArgument[1];
                return true;
            }

            /**
             * @brief `world.cells` — 스트리밍 중이면 올라 있는 셀 수를, 아니면 활성 씬을 쿠커처럼 셀로 나눠 셀 수 · 늘 올라 있는 엔티티 수를 알립니다.
             * @details 나누지 못하면(셀을 넘는 참조) 쿠커와 같은 오류가 엔티티마다 로그에 남는다 — 쿠킹 전에 에디터에서 위반을 본다.
             */
            static bool runWorldCells( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                const WorldStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<WorldStreamingSubsystem>();
                if ( streaming.isStreaming() )
                {
                    outReply = "streaming: " + to_string( streaming.getLoadedCellCount() ) + " of " + to_string( streaming.getCellCount() ) + " cells loaded";
                    return true;
                }
                if ( pScene->getWorldSettings()._bStreaming == false )
                {
                    outReply = "the active scene does not stream";
                    return true;
                }
                StreamingCellPreview preview{};
                if ( SceneCooker::previewStreamingCells( *pScene, preview ) == false )
                {
                    outReply = "the active scene could not be split into cells - see the log";
                    return false;
                }
                if ( preview._listViolation.empty() == false )
                {
                    outReply = "the cook would fail - " + to_string( static_cast<uint32>( preview._listViolation.size() ) ) +
                               " cross-cell references (see the log or the World Cells window)";
                    return false;
                }
                outReply = to_string( static_cast<uint32>( preview._listCell.size() ) ) + " cells, " +
                           to_string( static_cast<uint32>( preview._listAlwaysLoadedID.size() ) ) + " entities always loaded";
                return true;
            }

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
                    const bool bOn  = StringUtil::equals( listArgument[0], "on", true ) || listArgument[0] == "1";
                    const bool bOff = StringUtil::equals( listArgument[0], "off", true ) || listArgument[0] == "0";
                    if ( bOn == bOff )
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

    SW_DEV_COMMAND( Teleport, "teleport", "teleport <object> <x> <y> <z>", "Teleport a named object of the active scene", &SceneDevCommandsInternal::runTeleport );
    SW_DEV_COMMAND( WorldCells, "world.cells", "world.cells", "Show loaded streaming cells, or split the active scene into cells as the cooker would",
                    &SceneDevCommandsInternal::runWorldCells );
    SW_DEV_COMMAND( TagAdd, "tag.add", "tag.add <object> <tag>", "Add a gameplay tag to a named object of the active scene", &SceneDevCommandsInternal::runTagAdd );
    SW_DEV_COMMAND( AnimationRewind, "anim.rewind", "anim.rewind [on|off]", "Show or switch animation rewind recording (gv_animationRewind)",
                    &SceneDevCommandsInternal::runAnimationRewind );
    SW_DEV_COMMAND( AnimationRewindSeconds, "anim.rewind.seconds", "anim.rewind.seconds <seconds>", "Set how many seconds of animation history are kept",
                    &SceneDevCommandsInternal::runAnimationRewindSeconds );
    SW_DEV_COMMAND( AnimationRewindScrub, "anim.rewind.scrub", "anim.rewind.scrub <seconds ago>|resume",
                    "Freeze animation and show the recorded poses of the active scene that many seconds ago, or resume",
                    &SceneDevCommandsInternal::runAnimationRewindScrub );
} // namespace sw

#endif
