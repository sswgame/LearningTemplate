#include "pch.h"

#include "GameFramework/Kits/Rpg/OpenWorldWestern/HorseFollowAiController.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

namespace sw
{
    HorseFollowAiController::HorseFollowAiController()
        : _ownerObject{}
        , _followDistance{ 8.0f }
        , _stopDistance{ 3.0f }
        , _bCalled{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void HorseFollowAiController::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        (void)context;
        const GameObject*        pHorse     = pawn.getOwner();
        const GameObjectManager* pManager   = pHorse != nullptr ? pHorse->getManager() : nullptr;
        const GameObject*        pOwner     = pManager != nullptr && _ownerObject.isValid() ? pManager->resolveGameObject( _ownerObject ) : nullptr;
        const SceneComponent*    pHorseRoot = pHorse != nullptr ? pHorse->getPrimarySceneComponent() : nullptr;
        const SceneComponent*    pOwnerRoot = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pHorseRoot == nullptr || pOwnerRoot == nullptr )
        {
            stopMoving();
            return;
        }
        const float3  ownerPosition = pOwnerRoot->getWorldPosition();
        const float3  horsePosition = pHorseRoot->getWorldPosition();
        const float32 deltaX        = ownerPosition._x - horsePosition._x;
        const float32 deltaZ        = ownerPosition._z - horsePosition._z;
        const float32 distance      = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( distance <= _stopDistance )
        {
            _bCalled = SW_FALSE;
            if ( hasDestination() )
                stopMoving();
            return;
        }
        if ( _bCalled == SW_TRUE || distance > _followDistance || hasDestination() )
            moveTo( float3{ ownerPosition._x, horsePosition._y, ownerPosition._z } );
    }
} // namespace sw
