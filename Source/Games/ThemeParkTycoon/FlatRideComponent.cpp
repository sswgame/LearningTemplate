#include "pch.h"

#include "Games/ThemeParkTycoon/FlatRideComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

namespace sw
{
    FlatRideComponent::FlatRideComponent()
        : _director{}
        , _rideIndex{ -1 }
        , _spin{ 0.0f }
        , _angle{ 0.0f }
    {
        setCanEverTick( true );
    }

    void FlatRideComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 탑승을 정한 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void FlatRideComponent::assignRide( GameObjectHandle director, int32 rideIndex, float32 spin )
    {
        _director  = director;
        _rideIndex = rideIndex;
        _spin      = spin;
    }

    void FlatRideComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _spin == 0.0f )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const ParkDirectorComponent* pDirector = GameDirectorComponent::resolve<ParkDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        const vector<ParkRide>& listRide = pDirector->getSimulation().getRides();
        const bool              bInRange = 0 <= _rideIndex && _rideIndex < static_cast<int32>( listRide.size() );
        const ParkRide*         pRide    = bInRange ? listRide.data() + _rideIndex : nullptr;
        if ( pRide == nullptr || pRide->_listRider.empty() )
            return;
        _angle += _spin * deltaTime;
        pMesh->setLocalRotation( float3{ 0.0f, _angle, 0.0f } );
    }
} // namespace sw
