#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/ParkGuestComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Client/ParkViewComponent.h"
#include "GameFramework/Kits/Genre/Simulation/ThemePark/Component/ParkGameModeComponent.h"

namespace sw
{
    ParkGuestComponent::ParkGuestComponent()
        : _guestIndex{ -1 }
        , _heightOffset{ 0.45f }
    {
        setCanEverTick( true );
    }

    void ParkGuestComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시뮬레이션을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void ParkGuestComponent::assignGuest( GameObjectHandle gameMode, int32 guestIndex )
    {
        bindGameMode( gameMode );
        _guestIndex = guestIndex;
    }

    void ParkGuestComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const ParkGameModeComponent* pGameMode = findGameMode<ParkGameModeComponent>();
        // 모습을 나눠 주는 공원 뷰는 디렉터와 같은 오브젝트다(공원 뷰가 손님을 세우며 그 오브젝트를 적는다).
        const ParkViewComponent* pParkView = findGameMode<ParkViewComponent>();
        if ( pGameMode == nullptr || pParkView == nullptr )
            return;

        // 같은 그룹의 다른 손님과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<ParkGuest>& listGuest = pGameMode->getSimulation().getGuests();
        const bool               bInRange  = 0 <= _guestIndex && _guestIndex < static_cast<int32>( listGuest.size() );
        const ParkGuest*         pGuest    = bInRange ? listGuest.data() + _guestIndex : nullptr;
        const bool               bShown    = pGuest != nullptr && pGuest->_state != ParkGuestState::Riding && pGuest->_state != ParkGuestState::Left;
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( pGuest->_position + float3{ 0.0f, _heightOffset, 0.0f } ) );
        const shared_ptr<MaterialInstance>& look = pParkView->getGuestLook( ParkViewComponent::computeHappinessBucket( pGuest->_happiness ) );
        if ( look != nullptr )
            pMesh->setMaterialInstance( look ); // 같은 모습이면 메시가 거른다
    }
} // namespace sw
