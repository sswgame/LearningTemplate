#include "pch.h"

#include "Games/ThemeParkTycoon/CoasterCarComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Utility/OrientationUtil.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

namespace sw
{
    CoasterCarComponent::CoasterCarComponent()
        : _director{}
        , _coasterIndex{ -1 }
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

    void CoasterCarComponent::assignCar( GameObjectHandle director, int32 coasterIndex, int32 carIndex )
    {
        _director     = director;
        _coasterIndex = coasterIndex;
        _carIndex     = carIndex;
    }

    void CoasterCarComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr || _carIndex < 0 )
            return;
        const ParkDirectorComponent* pDirector = GameDirectorComponent::resolve<ParkDirectorComponent>( *pManager, _director );
        const CoasterTrain*          pTrain    = pDirector != nullptr ? pDirector->findCoasterTrain( _coasterIndex ) : nullptr;
        if ( pTrain == nullptr )
            return;
        const CoasterTrackFrame frame = pTrain->getCarFrame( static_cast<uint32>( _carIndex ), _carSpacing );
        pMesh->setLocalPosition( frame._position );
        pMesh->setLocalRotation( OrientationUtil::computeEulerFromForwardUp( frame._forward, frame._up ) );
    }
} // namespace sw
