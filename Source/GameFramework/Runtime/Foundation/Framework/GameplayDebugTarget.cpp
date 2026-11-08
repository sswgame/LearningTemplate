#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    /**
     * @struct GameplayDebugTargetName
     * @brief 개발 명령 `debug.target` 이 정한 대상 이름입니다(게임 스레드만 — 개발 명령과 HUD 섹션이 같은 스레드다).
     * @details GameFramework 의 모듈 부분이라 엔진 범위와 함께 생기고 사라진다(언리얼 게임플레이 디버거의 선택 대상이 `UGameplayDebuggerLocalController` 멤버인 자리).
     */
    struct GameplayDebugTargetName
    {
        static constexpr const utf8* kModulePartName = "GameplayDebugTargetName";

        string _name;
    };

    SW_MODULE_PART( Engine, GameplayDebugTargetName, GameplayDebugTargetName );

    namespace
    {
        struct GameplayDebugTargetInternal
        {

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
        static const string            s_none;
        const GameplayDebugTargetName* pTarget = game::findSubsystem<GameplayDebugTargetName>();
        return pTarget != nullptr ? pTarget->_name : s_none; // 게임 서비스가 없는 실행(테스트)은 대상이 없다
    }

    void GameplayDebugTarget::setTargetName( string_view name )
    {
        if ( GameplayDebugTargetName* const pTarget = game::findSubsystem<GameplayDebugTargetName>() )
            pTarget->_name = string( name );
    }

    SW_DEV_COMMAND( DebugTarget, "debug.target", "debug.target [object|clear]",
                    "Pick the object the gameplay debug HUD sections (quest, progress, gimmick, npc, contacts, movement, ability, interaction) show", &GameplayDebugTargetInternal::runTarget );
} // namespace sw
