#include "pch.h"

#include "Games/ThemeParkTycoon/ParkGuestComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/ThemeParkTycoon/ParkDirectorComponent.h"

namespace sw
{
    ParkGuestComponent::ParkGuestComponent()
        : _director{}
        , _guestIndex{ -1 }
        , _heightOffset{ 0.45f }
        , _colorBucket{ -1 }
    {
        setCanEverTick( true );
    }

    void ParkGuestComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시뮬레이션을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void ParkGuestComponent::assignGuest( GameObjectHandle director, int32 guestIndex )
    {
        _director    = director;
        _guestIndex  = guestIndex;
        _colorBucket = -1;
    }

    void ParkGuestComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const ParkDirectorComponent* pDirector = ParkDirectorComponent::resolveDirector( *pManager, _director );
        if ( pDirector == nullptr )
            return;

        // 같은 그룹의 다른 손님과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<ParkGuest>& listGuest = pDirector->getSimulation().getGuests();
        const bool               bInRange  = 0 <= _guestIndex && _guestIndex < static_cast<int32>( listGuest.size() );
        const ParkGuest*         pGuest    = bInRange ? listGuest.data() + _guestIndex : nullptr;
        const bool               bShown    = pGuest != nullptr && pGuest->_state != ParkGuestState::Riding && pGuest->_state != ParkGuestState::Left;
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( pGuest->_position + float3{ 0.0f, _heightOffset, 0.0f } );
        const int32 bucket = ParkDirectorComponent::computeHappinessBucket( pGuest->_happiness );
        if ( bucket == _colorBucket )
            return;
        const shared_ptr<MaterialInstance>& look = pDirector->getGuestLook( bucket );
        if ( look == nullptr )
            return;
        pMesh->setMaterialInstance( look );
        _colorBucket = bucket;
    }
} // namespace sw
