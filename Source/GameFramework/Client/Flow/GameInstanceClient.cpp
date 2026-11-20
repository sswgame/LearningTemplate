#include "pch.h"

#include "GameFramework/Client/Flow/GameInstanceClient.h"

#include "Core/Log/Logger.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"
#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Flow/LoadingScreenController.h"
#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceScope.h"
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

            bool openExperienceUI( ExperienceScope& scope ) override
            {
                const ExperienceDefinition& definition = scope.getDefinition();
                UISystem* const             pUI        = game::findSubsystem<UISystem>();
                if ( pUI == nullptr || definition._listUI.empty() ) // 창이 없는 클라이언트 호스트(헤드리스)에는 열 화면이 없다
                    return true;
                for ( const string& document : definition._listUI )
                {
                    const UIScreenHandle handle = pUI->openScreen( document );
                    if ( handle == kInvalidUIScreenHandle )
                    {
                        SW_LOG_ERROR_ID( "GameInstanceClient.ExperienceUINotOpened", "Experience '%#': UI document '%#' could not be opened (see the messages above)", definition._name.c_str(),
                                         document.c_str() );
                        return false;
                    }
                    // UI 서비스는 엔진 수명이라 Experience 보다 오래 산다. 이미 닫힌 화면을 닫는 것은 할 일이 없다.
                    scope.addCleanup( Delegate<void()>::create( [pUI, handle]()
                    { pUI->closeScreen( handle ); } ) );
                }
                return true;
            }
        };

        GameInstanceClient                          s_gameInstanceClient;
        const IGameInstanceClientHook::Registration s_gameInstanceClientRegistration{ s_gameInstanceClient };
    } // namespace
} // namespace sw
