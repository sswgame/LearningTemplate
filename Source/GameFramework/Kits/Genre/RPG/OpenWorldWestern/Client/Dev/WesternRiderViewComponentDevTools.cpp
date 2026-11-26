/**
 * @file WesternRiderViewComponentDevTools.cpp
 * @brief `WesternRiderViewComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Client/WesternHUDViewModel.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Client/WesternRiderViewComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/MountMovementComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct WesternRiderViewComponentDevInternal
        {
            /** @brief 감시 값 `Western.OutlawStatus` 입니다. 대상 디렉터 오브젝트의 뷰가 HUD 에 쓰는 무법자 글입니다. 아무 일도 없으면 빈 글입니다. */
            [[nodiscard]] static bool readOutlawStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const WesternRiderViewComponent* const pView     = query.findTargetComponent<WesternRiderViewComponent>( "Western Rider View", outValue );
                const WesternGameModeComponent* const  pDirector = pView != nullptr ? query.findTargetComponent<WesternGameModeComponent>( "Western Game Mode", outValue ) : nullptr;
                if ( pDirector == nullptr )
                    return false;
                outValue.setText( LocalizedText::resolveNow( pView->formatOutlawStatus( *pDirector ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernOutlawStatus, "Western.OutlawStatus", "Outlaw line the rider view on target writes to the HUD (empty when nothing happened)",
                               &WesternRiderViewComponentDevInternal::readOutlawStatus, true, false );
} // namespace sw
