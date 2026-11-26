/**
 * @file RTSCommandInputComponentDevTools.cpp
 * @brief `RTSCommandInputComponent` 의 자동화 감시 값입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Component/SkirmishGameModeComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Input/InputActionCheckUtil.h"
#include "GameFramework/Runtime/Foundation/Presentation/GameSound.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSCommandInputComponentDevInternal
        {
            /** @brief 감시 값 `Skirmish.SelectedCount` 입니다. */
            [[nodiscard]] static bool readSelectedCount( const RTSCommandInputComponent& commandInput, float64& outValue )
            {
                outValue = static_cast<float64>( commandInput.getSelection().getSelected().size() );
                return true;
            }

            /** @brief 감시 값 `Skirmish.SelectedMovingCount` 입니다. 고른 유닛 가운데 앞 명령이 이동인 수입니다. */
            [[nodiscard]] static bool readSelectedMovingCount( const RTSCommandInputComponent& commandInput, float64& outValue )
            {
                const SkirmishGameModeComponent* const pDirector = commandInput.findGameMode();
                if ( pDirector == nullptr )
                    return false;
                int32 count = 0;
                for ( const RTSUnitID unitID : commandInput.getSelection().getSelected() )
                {
                    const RTSUnit* const  pUnit  = pDirector->getWorld().findUnit( unitID );
                    const RTSOrder* const pOrder = pUnit != nullptr ? pUnit->findOrder() : nullptr;
                    if ( pOrder != nullptr && pOrder->_type == RTSOrderKind::Move )
                        ++count;
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedCount, "Skirmish.SelectedCount", "Units in the human selection", RTSCommandInputComponent, &RTSCommandInputComponentDevInternal::readSelectedCount );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishSelectedMovingCount, "Skirmish.SelectedMovingCount", "Selected units whose current order is a move", RTSCommandInputComponent,
                                   &RTSCommandInputComponentDevInternal::readSelectedMovingCount );
} // namespace sw
