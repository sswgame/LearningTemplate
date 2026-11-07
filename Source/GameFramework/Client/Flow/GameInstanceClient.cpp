#include "pch.h"

#include "GameFramework/Client/Flow/GameInstanceClient.h"

#include "Core/String/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Flow/LoadingScreenController.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstanceClientHook.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameSceneFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    void GameSessionCommands::bind( GameSceneFlow& flow, UIGlobalCommandRegistry& commands )
    {
        // 타이틀 씬이 없는 게임은 "타이틀로" 를 걸지 않는다(단추가 접힌 채다).
        if ( flow.hasTitleScene() == false )
            return;
        flow.holdSessionCommand( commands.registerCommand( hashed_string( UIGlobalCommandRegistry::kReturnToTitle ),
                                                           SW_DELEGATE_METHOD( UICommandDelegate, &GameSceneFlow::onReturnToTitleCommand, &flow ) ) );
    }

    namespace
    {
        /** @brief 게임 인스턴스의 클라이언트 전용 부분입니다. 상태가 없다 — 세운 것은 게임 범위 · 씬 흐름이 든다. */
        class GameInstanceClient final : public IGameInstanceClientHook
        {
        public:
            ILoadingScreen* emplaceLoadingScreen( SubsystemScope& gameScope ) override { return &gameScope.emplaceSubsystem<LoadingScreenController>().getLoadingScreen(); }

            void bindSessionCommands( GameSceneFlow& flow ) override
            {
                // 창이 없는 클라이언트 호스트(헤드리스)에는 UI 가 없다 — UI 전역 명령 표도 쓰지 않는다.
                if ( game::findSubsystem<UISystem>() == nullptr )
                    return;
                GameSessionCommands::bind( flow, game::getSubsystem<UIGlobalCommandRegistry>() );
            }
        };

        GameInstanceClient                          s_gameInstanceClient;
        const IGameInstanceClientHook::Registration s_gameInstanceClientRegistration{ s_gameInstanceClient };
    } // namespace
} // namespace sw
