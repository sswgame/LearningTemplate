/**
 * @file PhotoModeCameraDevTools.cpp
 * @brief `PhotoModeCamera` 의 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/PhotoModeCamera.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct PhotoModeDevCommandsInternalDev
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

            /**
             * @brief 화면 시점을 모는 디렉터(`CameraDirectorComponent::findViewDirector` — 씬에 하나)입니다. @p bInPhotoMode 이면 포토 모드가 켜져 있을 때만 냅니다.
             * @details 매니저가 있는 게임(사격장 · 서부 평원)은 디렉터가 뷰 타깃에 있고 매니저가 그 포즈를 화면 카메라로 옮긴다(언리얼 PlayerCameraManager 의 뷰 타깃).
             */
            static CameraDirectorComponent* findDirector( bool bInPhotoMode )
            {
                const GameObjectManager* pManager  = findActiveManager();
                CameraDirectorComponent* pDirector = pManager != nullptr ? CameraDirectorComponent::findViewDirector( *pManager ) : nullptr;
                if ( pDirector == nullptr || ( bInPhotoMode && pDirector->isInPhotoMode() == false ) )
                    return nullptr;
                return pDirector;
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
                const DevCommandRegistration* pScreenshot = game::getSubsystem<DevCommandRegistry>().findCommand( "screenshot" );
                if ( pScreenshot == nullptr )
                {
                    outReply = "photo mode: the screenshot command is not registered";
                    return true;
                }
                return DevCommandRegistry::run( *pScreenshot, listArgument, outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( PhotoOn, "photo.on", "photo.on", "Pause the game and fly a free camera from the current view (photo mode)", &PhotoModeDevCommandsInternalDev::runOn );
    SW_DEV_COMMAND( PhotoOff, "photo.off", "photo.off", "Leave photo mode and restore the game camera and time", &PhotoModeDevCommandsInternalDev::runOff );
    SW_DEV_COMMAND( PhotoShot, "photo.shot", "photo.shot [file]", "Save the next frame as PNG (same as screenshot)", &PhotoModeDevCommandsInternalDev::runShot );
} // namespace sw
