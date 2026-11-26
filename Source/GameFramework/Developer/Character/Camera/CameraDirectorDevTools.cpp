/**
 * @file CameraDirectorDevTools.cpp
 * @brief 카메라 디렉터(`CameraDirectorComponent`)의 프리셋 개발 명령입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDesc.h"

#include "GameFramework/Runtime/Character/Camera/CameraCollisionProbe.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `camera.preset <id>` — 활성 씬의 카메라 디렉터(캡처 카메라 제외) 중 그 프리셋이 카탈로그에 있는 것을 그 프리셋으로 자릅니다(`startWithPreset`).
         * @details 프리셋마다 스크린샷을 찍으려면(시나리오 골든) 키를 누르지 않고 시점을 골라야 한다. 예전에는 테스트용 변수(`-gv_cameraPreset`)를 시작 때 컴포넌트가 읽었다 —
         *          묻는 쪽이 부르는 명령으로 바꿔 런타임 시작 경로에서 개발 분기를 뺐다(배포본에는 등록되지 않는다). 받은 디렉터가 하나도 없으면 오류다 — 틀린 화면으로 골든을 뜨지 않는다.
         */
        struct CameraPresetDevCommandInternal
        {
            static bool runPreset( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                SceneManager* const pSceneManager = game::findSubsystem<SceneManager>();
                Scene* const        pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
                GameObjectManager*  pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                if ( pManager == nullptr )
                {
                    outReply = "camera.preset: no active scene";
                    return true;
                }
                const hashed_string id( listArgument[0] );
                uint32              applied = 0;
                for ( CameraDirectorComponent* pDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
                {
                    const GameObject*      pOwner   = pDirector->getOwner();
                    const CameraComponent* pCamera  = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
                    const bool             bCapture = pCamera != nullptr && pCamera->getRole() == CameraRole::Capture;
                    if ( bCapture || pDirector->getCatalog().find( id ) == nullptr )
                        continue;
                    pDirector->startWithPreset( id );
                    ++applied;
                }
                if ( applied == 0 )
                    SW_LOG_ERROR_ID( "Camera.UnknownPreset", "camera.preset '%#': no camera director in the active scene has that preset", id.c_str() );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "camera.preset '%#': %# director(s)", id.c_str(), applied );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CameraPreset, "camera.preset", "camera.preset <id>", "Cut every camera director of the active scene (capture cameras excluded) that has this preset to it",
                    &CameraPresetDevCommandInternal::runPreset );
} // namespace sw
