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
    SW_DEV_COMMAND( DebugDrawCategory, "debugdraw.category", "debugdraw.category [<name> <on|off>]", "List or toggle DebugDrawQueue categories",
                    &EngineDevCommandsInternal::runDebugDrawCategory );
} // namespace sw

#endif
