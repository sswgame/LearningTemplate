#include "pch.h"

#include "Games/ThemeParkTycoon/CoasterCarComponent.h"

#include "Core/Math/OrientationUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/ThemeParkTycoon/ParkGameModeComponent.h"

namespace sw
{
    CoasterCarComponent::CoasterCarComponent()
        : _coasterIndex{ -1 }
        , _carIndex{ 0 }
        , _carSpacing{ 2.4f }
    {
        setCanEverTick( true );
    }

    void CoasterCarComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 열차를 나아가게 한 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
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
        GameObject*        pOwner             = getOwner();
        GameObjectManager* pManager           = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh              = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        const bool         bMissingCarContext = pManager == nullptr || pMesh == nullptr || _carIndex < 0;
        if ( bMissingCarContext )
            return;
        const ParkGameModeComponent* pGameMode = findGameMode<ParkGameModeComponent>();
        const CoasterTrain*          pTrain    = pGameMode != nullptr ? pGameMode->findCoasterTrain( _coasterIndex ) : nullptr;
        if ( pTrain == nullptr )
            return;
        const CoasterTrackFrame frame = pTrain->getCarFrame( static_cast<uint32>( _carIndex ), _carSpacing );
        pMesh->setLocalPosition( frame._position );
        pMesh->setLocalRotation( OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ) );
    }
} // namespace sw
