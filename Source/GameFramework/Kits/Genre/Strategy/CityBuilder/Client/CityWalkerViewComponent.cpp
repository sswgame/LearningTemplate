#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityWalkerViewComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Client/CityViewComponent.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/Component/CityBuilderGameModeComponent.h"

namespace sw
{
    CityWalkerViewComponent::CityWalkerViewComponent()
        : _walkerIndex{ -1 }
        , _height{ 0.35f }
    {
        setCanEverTick( true );
    }

    void CityWalkerViewComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시뮬레이션을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void CityWalkerViewComponent::assignWalker( GameObjectHandle gameMode, int32 walkerIndex )
    {
        bindGameMode( gameMode );
        _walkerIndex = walkerIndex;
    }

    void CityWalkerViewComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const CityBuilderGameModeComponent* pGameMode = findGameMode<CityBuilderGameModeComponent>();
        const CityViewComponent*            pCityView = findGameMode<CityViewComponent>(); // 디렉터와 같은 오브젝트다
        if ( pGameMode == nullptr || pCityView == nullptr )
            return;
        // 같은 그룹의 다른 일꾼과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<CityWalker>& listWalker = pGameMode->getCity().getWalkers();
        const bool                bShown     = 0 <= _walkerIndex && _walkerIndex < static_cast<int32>( listWalker.size() );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        const CityWalker& walker   = listWalker.data()[_walkerIndex];
        const float2      position = CitySimulation::computeWalkerPosition( walker );
        pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( float3{ position._x, _height, position._y } ) );
        const shared_ptr<MaterialInstance>& look = pCityView->findWalkerLook( walker );
        if ( look != nullptr )
            pMesh->setMaterialInstance( look ); // 같은 모습이면 메시가 거른다
    }
} // namespace sw
