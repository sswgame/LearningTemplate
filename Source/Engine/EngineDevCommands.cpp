/**
 * @file EngineDevCommands.cpp
 * @brief 엔진이 내주는 개발 명령(시간 배율 · 순간이동 · 디버그 드로우 카테고리)입니다. Shipping 에는 없습니다.
 * @details 게임 · 키트의 치트(무적 · 아이템 주기 …)는 그 게임 · 키트의 .cpp 에 `SW_DEV_COMMAND` 로 둡니다 — 여기에는 어느 게임에나 뜻이 있는 것만.
 */
#include "pch.h"

#include "Engine/Utility/Console/DevCommandRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/String/StringUtil.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
    #include "Engine/Object/Animation/AnimationSystem.h"
    #include "Engine/Object/Component/SceneComponent.h"
    #include "Engine/Object/GameObject/GameObject.h"
    #include "Engine/Object/GameObject/GameObjectManager.h"
    #include "Engine/Scene/Scene.h"
    #include "Engine/Scene/SceneManager.h"
    #include "Engine/Utility/GameAutoplay.h"
    #include "Engine/Utility/GameTimeScale.h"

namespace sw
{
    namespace
    {
        struct EngineDevCommandsInternal
        {
            /** @brief 낱말을 float 으로 읽습니다. 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool parseFloat( const string& text, float32& outValue )
            {
                utf8*         pEnd  = nullptr;
                const float64 value = std::strtod( text.c_str(), &pEnd );
                if ( pEnd == text.c_str() || ( pEnd != nullptr && *pEnd != '\0' ) )
                    return false;
                outValue = static_cast<float32>( value );
                return true;
            }

            static bool runTimeScale( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    float32 scale = 1.0f;
                    if ( parseFloat( listArgument[0], scale ) == false )
                        return false;
                    GameTimeScale::set( scale );
                }
                outReply = "time scale = " + to_string( GameTimeScale::get() );
                return true;
            }

            static bool runTeleport( const vector<string>& listArgument, string& outReply )
            {
                float3 position{};
                if ( listArgument.size() != 4 || parseFloat( listArgument[1], position._x ) == false || parseFloat( listArgument[2], position._y ) == false ||
                     parseFloat( listArgument[3], position._z ) == false )
                    return false;
                Scene*             pScene    = engine::areEngineServicesBound() ? engine::getSceneManager().getActiveScene() : nullptr;
                GameObjectManager* pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                GameObject*        pObject   = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( listArgument[0] ) ) : nullptr;
                SceneComponent*    pRootNode = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRootNode == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' with a transform in the active scene";
                    return false;
                }
                pRootNode->teleportTo( position );
                outReply = "teleported " + listArgument[0];
                return true;
            }

            static bool runAutoplay( const vector<string>& listArgument, string& outReply )
            {
                const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
                if ( pActive == nullptr )
                {
                    outReply = "this game has no autoplay (SW_GAME_AUTOPLAY)";
                    return false;
                }
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.size() == 1 )
                {
                    bool bOn{ false };
                    if ( StringUtil::tryParseBool( listArgument[0], bOn ) == false )
                        return false;
                    (void)GameAutoplay::setOn( bOn ); // 위에서 등록을 확인했다
                }
                outReply = string( pActive->_pGameName ) + " autoplay " + ( GameAutoplay::isOn() ? "on" : "off" ) + " (" + pActive->_pVariableName + ")";
                return true;
            }

            /** @brief 활성 씬의 애니메이션 시스템입니다(없으면 nullptr). */
            static AnimationSystem* findActiveAnimationSystem()
            {
                Scene*             pScene   = engine::areEngineServicesBound() ? engine::getSceneManager().getActiveScene() : nullptr;
                GameObjectManager* pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                return pManager != nullptr ? &pManager->getAnimationSystem() : nullptr;
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
                float32 seconds = 0.0f;
                if ( listArgument.size() != 1 || parseFloat( listArgument[0], seconds ) == false || seconds <= 0.0f )
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
                float32 secondsAgo = 0.0f;
                if ( listArgument.size() != 1 || parseFloat( listArgument[0], secondsAgo ) == false || secondsAgo < 0.0f )
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

            static bool runDebugDrawCategory( const vector<string>& listArgument, string& outReply )
            {
                if ( engine::areEngineServicesBound() == false )
                    return false;
                DebugDrawQueue& queue = engine::getDebugDrawQueue();
                if ( listArgument.empty() )
                {
                    vector<hashed_string> listCategory;
                    queue.collectCategories( listCategory );
                    outReply = "categories:";
                    for ( const hashed_string& category : listCategory )
                    {
                        outReply += ' ';
                        outReply += category.c_str();
                        outReply += queue.isCategoryEnabled( category ) ? "(on)" : "(off)";
                    }
                    return true;
                }
                if ( listArgument.size() != 2 )
                    return false;
                bool bOn{ false };
                if ( StringUtil::tryParseBool( listArgument[1], bOn ) == false )
                    return false;
                queue.setCategoryEnabled( hashed_string( listArgument[0] ), bOn );
                outReply = listArgument[0] + ( bOn ? " on" : " off" );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( TimeScale, "timescale", "timescale [scale]", "Show or set the game time scale (gv_timeScale, 0 = frozen)",
                    &EngineDevCommandsInternal::runTimeScale );
    SW_DEV_COMMAND( Teleport, "teleport", "teleport <object> <x> <y> <z>", "Teleport a named object of the active scene",
                    &EngineDevCommandsInternal::runTeleport );
    SW_DEV_COMMAND( Autoplay, "autoplay", "autoplay [on|off]", "Show or switch the game's autoplay (AI drives the player - SW_GAME_AUTOPLAY)",
                    &EngineDevCommandsInternal::runAutoplay );
    SW_DEV_COMMAND( AnimationRewind, "anim.rewind", "anim.rewind [on|off]", "Show or switch animation rewind recording (gv_animationRewind)",
                    &EngineDevCommandsInternal::runAnimationRewind );
    SW_DEV_COMMAND( AnimationRewindSeconds, "anim.rewind.seconds", "anim.rewind.seconds <seconds>", "Set how many seconds of animation history are kept",
                    &EngineDevCommandsInternal::runAnimationRewindSeconds );
    SW_DEV_COMMAND( AnimationRewindScrub, "anim.rewind.scrub", "anim.rewind.scrub <seconds ago>|resume",
                    "Freeze animation and show the recorded poses of the active scene that many seconds ago, or resume",
                    &EngineDevCommandsInternal::runAnimationRewindScrub );
    SW_DEV_COMMAND( DebugDrawCategory, "debugdraw.category", "debugdraw.category [<name> <on|off>]", "List or toggle DebugDrawQueue categories",
                    &EngineDevCommandsInternal::runDebugDrawCategory );
} // namespace sw

#endif
