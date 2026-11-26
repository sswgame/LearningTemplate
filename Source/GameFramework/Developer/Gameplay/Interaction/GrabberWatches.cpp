/**
 * @file GrabberWatches.cpp
 * @brief 잡기(`GrabberComponent`)의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Interaction/GrabberComponent.h"

namespace sw
{
    namespace
    {
        struct GrabberWatchesInternal
        {
            /** @brief 감시 값 `Grab.Held` 입니다. 대상의 집기 컴포넌트가 든 오브젝트 이름이고, 든 것이 없으면 `none` 입니다. */
            [[nodiscard]] static bool readHeld( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GrabberComponent* pGrabber = query.findTargetComponent<GrabberComponent>( "grabber", outValue );
                if ( pGrabber == nullptr )
                    return false;
                const GameObject* pHeld = query._pManager != nullptr ? query._pManager->resolveGameObject( pGrabber->getHeld() ) : nullptr;
                outValue.setText( pHeld != nullptr ? pHeld->getName().c_str() : "none" );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( grabHeld, "Grab.Held", "Name of the object the target's GrabberComponent holds; none when it holds nothing", &GrabberWatchesInternal::readHeld, true, false );
} // namespace sw
