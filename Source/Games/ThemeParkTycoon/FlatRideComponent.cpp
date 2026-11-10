#include "pch.h"

#include "Games/ThemeParkTycoon/FlatRideComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/ThemeParkTycoon/ParkGameModeComponent.h"

namespace sw
{
    FlatRideComponent::FlatRideComponent()
        : _rideIndex{ -1 }
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

    void FlatRideComponent::assignRide( GameObjectHandle gameMode, int32 rideIndex, float32 spin )
    {
        bindGameMode( gameMode );
        _rideIndex = rideIndex;
        _spin      = spin;
    }

    void FlatRideComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _spin == 0.0f )
            return;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const ParkGameModeComponent* pGameMode = findGameMode<ParkGameModeComponent>();
        if ( pGameMode == nullptr )
            return;
        const vector<ParkRide>& listRide = pGameMode->getSimulation().getRides();
        const bool              bInRange = 0 <= _rideIndex && _rideIndex < static_cast<int32>( listRide.size() );
        const ParkRide*         pRide    = bInRange ? listRide.data() + _rideIndex : nullptr;
        if ( pRide == nullptr || pRide->_listRider.empty() )
            return;
        _angle += _spin * context.getDeltaTime();
        pMesh->setLocalRotation( float3{ 0.0f, _angle, 0.0f } );
    }
} // namespace sw
