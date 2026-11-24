#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/CoasterCarComponent.h"

#include "Core/Math/OrientationUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"

namespace sw
{
    CoasterCarComponent::CoasterCarComponent()
        : _coasterIndex{ -1 }
        , _carIndex{ 0 }
        , _carSpacing{ 2.4f }
    {
        setCanEverTick( true );
    }

    void CoasterCarComponent::assignCar( GameObjectHandle gameMode, int32 coasterIndex, int32 carIndex )
    {
        bindGameMode( gameMode );
        _coasterIndex = coasterIndex;
        _carIndex     = carIndex;
    }

    void CoasterCarComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner             = getOwner();
        MeshComponent* pMesh              = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool     bMissingCarContext = pMesh == nullptr || _carIndex < 0;
        if ( bMissingCarContext )
            return;
        const ParkGameModeComponent* pGameMode = findGameMode<ParkGameModeComponent>();
        const CoasterTrain*          pTrain    = pGameMode != nullptr ? pGameMode->findCoasterTrain( _coasterIndex ) : nullptr;
        if ( pTrain == nullptr )
            return;
        const CoasterTrackFrame frame = pTrain->getCarFrame( static_cast<uint32>( _carIndex ), _carSpacing );
        pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( frame._position ) );
        pMesh->setLocalRotation( OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ) );
    }
} // namespace sw
