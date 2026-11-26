/**
 * @file CinematicCameraComponentDevTools.cpp
 * @brief `CinematicCameraComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CinematicCameraComponent.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct CinematicCameraComponentDevInternal
        {
            /** @brief 감시 값 `Cinematic.CutCamera` 입니다. 대상의 시네마틱 컴포넌트가 지금 화면을 덮는다고 아는 컷 카메라의 오브젝트 이름이고, 덮지 않으면 `none` 입니다. */
            [[nodiscard]] static bool readCutCamera( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CinematicCameraComponent* pCinematic = query.findTargetComponent<CinematicCameraComponent>( "cinematic camera", outValue );
                if ( pCinematic == nullptr )
                    return false;
                const GameObject* pCut = pCinematic->isCinematicActive() ? query._pManager->resolveGameObject( pCinematic->getCurrentCutCamera() ) : nullptr;
                outValue.setText( pCut != nullptr ? pCut->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( cinematicCutCamera, "Cinematic.CutCamera", "Name of the cut camera the target's cinematic component knows covers the screen; none between cinematics",
                               &CinematicCameraComponentDevInternal::readCutCamera, true, false );
} // namespace sw
