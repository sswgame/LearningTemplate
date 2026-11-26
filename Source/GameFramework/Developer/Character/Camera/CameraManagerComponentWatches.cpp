/**
 * @file CameraManagerComponentWatches.cpp
 * @brief 카메라 매니저(`CameraManagerComponent`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
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
        struct CameraManagerComponentWatchesInternal
        {
            /** @brief 질의 대상의 카메라 매니저입니다. 없으면 까닭을 적고 nullptr 입니다. */
            static const CameraManagerComponent* findTargetManager( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CameraManagerComponent>( "camera manager", outValue );
            }

            /** @brief 감시 값 `CameraManager.Blending` 입니다. 대상 매니저가 블렌드 중이면 1 입니다. */
            [[nodiscard]] static bool readBlending( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraManagerComponent* pManager = findTargetManager( query, outValue );
                if ( pManager == nullptr )
                    return false;
                outValue.setNumber( pManager->isBlending() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `CameraManager.PendingCamera` 입니다. 대기 뷰의 오브젝트 이름이고, 대기 뷰가 없으면 `none` 입니다. */
            [[nodiscard]] static bool readPendingCamera( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraManagerComponent* pManager = findTargetManager( query, outValue );
                if ( pManager == nullptr )
                    return false;
                const GameObject* pPending = pManager->hasPendingView() ? query._pManager->resolveGameObject( pManager->getPendingView()._camera ) : nullptr;
                outValue.setText( pPending != nullptr ? pPending->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( cameraManagerBlending, "CameraManager.Blending", "1 while the target's camera manager blends between views", &CameraManagerComponentWatchesInternal::readBlending, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraManagerPendingCamera, "CameraManager.PendingCamera", "Name of the object the target's camera manager is about to look through; none without a pending view",
                               &CameraManagerComponentWatchesInternal::readPendingCamera, true, false );
} // namespace sw
