#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Actor/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `photo.on` · `photo.off` · `photo.shot` — 활성 씬의 게임 카메라 디렉터에 포토 모드를 켜고 끄며 화면을 찍습니다.
         * @details 찍기는 엔진의 `screenshot` 명령을 레지스트리로 불러 같은 경로 규칙(`Saved/Screenshots/<시각>.png`)을 쓴다 — 렌더 스레드를 직접 알지 않는다.
         */
        struct PhotoModeDevCommandsInternal
        {
            /** @brief 활성 씬의 오브젝트 매니저입니다. 없으면 nullptr 입니다. */
            static GameObjectManager* findActiveManager()
            {
                if ( game::findSubsystem<SceneManager>() == nullptr )
                    return nullptr;
                SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
                if ( pSceneManager == nullptr )
                    return nullptr;
                Scene* pScene = pSceneManager->getActiveScene();
                return pScene != nullptr ? pScene->getObjectManager() : nullptr;
            }

            /** @brief 게임 시점 카메라(`CameraRole::Game`)를 모는 첫 디렉터입니다. @p bInPhotoMode 이면 포토 모드가 켜진 것만 봅니다. */
            static CameraDirectorComponent* findDirector( bool bInPhotoMode )
            {
                GameObjectManager* pManager = findActiveManager();
                if ( pManager == nullptr )
                    return nullptr;
                for ( CameraDirectorComponent* pDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
                {
                    const GameObject*      pOwner  = pDirector->getOwner();
                    const CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
                    if ( pCamera == nullptr || pCamera->getRole() != CameraRole::Game )
                        continue;
                    if ( bInPhotoMode && pDirector->isInPhotoMode() == false )
                        continue;
                    return pDirector;
                }
                return nullptr;
            }

            static bool runOn( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                CameraDirectorComponent* pDirector = findDirector( false );
                if ( pDirector == nullptr )
                {
                    outReply = "photo mode: no game camera director in the active scene";
                    return true;
                }
                if ( pDirector->isInPhotoMode() )
                {
                    outReply = "photo mode: already on";
                    return true;
                }
                outReply = pDirector->enterPhotoMode() ? "photo mode: on (game paused)" : "photo mode: not entered - no pose yet or the photo mode input map did not load (see the log)";
                return true;
            }

            static bool runOff( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                CameraDirectorComponent* pDirector = findDirector( true );
                if ( pDirector == nullptr )
                {
                    outReply = "photo mode: already off";
                    return true;
                }
                pDirector->exitPhotoMode();
                outReply = "photo mode: off";
                return true;
            }

            static bool runShot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                const DevCommandRegistration* pScreenshot = DevCommandRegistry::get().findCommand( "screenshot" );
                if ( pScreenshot == nullptr )
                {
                    outReply = "photo mode: the screenshot command is not registered";
                    return true;
                }
                return pScreenshot->_pFunc( listArgument, outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( PhotoOn, "photo.on", "photo.on", "Pause the game and fly a free camera from the current view (photo mode)", &PhotoModeDevCommandsInternal::runOn );
    SW_DEV_COMMAND( PhotoOff, "photo.off", "photo.off", "Leave photo mode and restore the game camera and time", &PhotoModeDevCommandsInternal::runOff );
    SW_DEV_COMMAND( PhotoShot, "photo.shot", "photo.shot [file]", "Save the next frame as PNG (same as screenshot)", &PhotoModeDevCommandsInternal::runShot );
} // namespace sw
#endif
