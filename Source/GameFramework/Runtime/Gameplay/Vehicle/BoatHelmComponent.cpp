#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/BoatHelmComponent.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    BoatHelmComponent::BoatHelmComponent()
    {
        setCanEverTick( true );
        setTickGroup( TickGroup::PrePhysics );
    }

    BoatHelmInput BoatHelmComponent::toHelmInput( const ControlIntent& intent, const float3& boatForward )
    {
        const float2  localMove = intent.computeLocalMove( boatForward );
        BoatHelmInput input{};
        input._throttle = localMove._y;
        input._rudder   = localMove._x;
        return input;
    }

    void BoatHelmComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*           pOwner = getOwner();
        const PawnComponent*  pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        BoatComponent*        pBoat  = pOwner != nullptr ? pOwner->getComponent<BoatComponent>() : nullptr;
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pPawn == nullptr || pBoat == nullptr || pRoot == nullptr )
        {
            SW_LOG_ERROR_ONCE_ID( "BoatHelm.MissingParts", "'%#' has a BoatHelmComponent without a pawn, a Boat component or a scene component - the helm steers nothing",
                                  pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, pRoot->getWorldMatrix() );
        pBoat->setHelmInput( toHelmInput( pPawn->getIntent(), forward ) );
    }
} // namespace sw
