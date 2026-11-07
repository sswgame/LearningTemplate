#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct GameplayDebugTargetInternal
        {
            /** @brief 정한 대상 이름입니다(게임 스레드만 — 개발 명령과 HUD 섹션이 같은 스레드다). */
            static string& getName()
            {
                static string s_name;
                return s_name;
            }

            /** @brief `debug.target [이름|clear]` — 인자가 없으면 지금 대상을 알린다. */
            static bool runTarget( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                if ( listArgument.empty() == false )
                    GameplayDebugTarget::setTargetName( listArgument[0] == "clear" ? string_view{} : string_view( listArgument[0] ) );
                outReply = GameplayDebugTarget::getTargetName().empty() ? string( "debug.target: none (each section shows the first object that has its component)" )
                                                                        : "debug.target: " + GameplayDebugTarget::getTargetName();
                return true;
            }
        };
    } // namespace

    GameObjectManager* GameplayDebugTarget::findActiveObjectManager()
    {
        SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
        return pSceneManager != nullptr ? pSceneManager->findActiveObjectManager() : nullptr;
    }

    const string& GameplayDebugTarget::getTargetName()
    {
        return GameplayDebugTargetInternal::getName();
    }

    void GameplayDebugTarget::setTargetName( string_view name )
    {
        GameplayDebugTargetInternal::getName() = string( name );
    }

    SW_DEV_COMMAND( DebugTarget, "debug.target", "debug.target [object|clear]",
                    "Pick the object the gameplay debug HUD sections (quest, progress, gimmick, npc, contacts, movement, ability, interaction) show", &GameplayDebugTargetInternal::runTarget );
} // namespace sw
