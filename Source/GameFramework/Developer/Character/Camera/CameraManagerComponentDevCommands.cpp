/**
 * @file CameraManagerComponentDevCommands.cpp
 * @brief 카메라 매니저(`CameraManagerComponent`)의 개발 명령 `camera.shake` 입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/MeshLODSystem.h"

#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"
#include "GameFramework/Runtime/Character/Camera/CameraUserSettings.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct CameraManagerComponentDevCommandsInternal
        {
            /** @brief `camera.shake <x> <y> <z> <amplitude> <duration> [radius]` — 활성 씬의 흔들림 채널(알림의 `CameraShake` 처리기와 같은 길)에 충격 하나를 냅니다. */
            static bool runShake( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 5 || listArgument.size() > 6 )
                    return false;
                CameraShakeRequest request;
                bool               bParsed = StringUtil::parseFloat( listArgument[0], request._origin._x ) && StringUtil::parseFloat( listArgument[1], request._origin._y ) &&
                               StringUtil::parseFloat( listArgument[2], request._origin._z ) && StringUtil::parseFloat( listArgument[3], request._amplitude ) &&
                               StringUtil::parseFloat( listArgument[4], request._duration );
                if ( listArgument.size() == 6 )
                    bParsed = bParsed && StringUtil::parseFloat( listArgument[5], request._falloffRadius );
                if ( bParsed == false )
                    return false;
                GameObjectManager* pManager = engine::findActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no active scene";
                    return true;
                }
                pManager->getCameraRegistry().getShakeChannel().broadcast( request );
                outReply = "camera shake requested";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( CameraShakeCommand, "camera.shake", "camera.shake <x> <y> <z> <amplitude> <duration> [radius]", "Request a camera shake at a world point of the active scene",
                    &CameraManagerComponentDevCommandsInternal::runShake );
} // namespace sw
