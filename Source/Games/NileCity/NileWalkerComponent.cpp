#include "pch.h"

#include "Games/NileCity/NileWalkerComponent.h"

#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/NileCity/NileDirectorComponent.h"

namespace sw
{
    NileWalkerComponent::NileWalkerComponent()
        : _director{}
        , _walkerIndex{ -1 }
        , _height{ 0.35f }
        , _pShownLook{ nullptr }
    {
        setCanEverTick( true );
    }

    void NileWalkerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 시뮬레이션을 끝낸 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void NileWalkerComponent::assignWalker( GameObjectHandle director, int32 walkerIndex )
    {
        _director    = director;
        _walkerIndex = walkerIndex;
        _pShownLook  = nullptr;
    }

    void NileWalkerComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const NileDirectorComponent* pDirector = NileDirectorComponent::resolveDirector( *pManager, _director );
        if ( pDirector == nullptr )
            return;
        // 같은 그룹의 다른 일꾼과 함께 읽는다 — 첨자 대신 포인터로(쓰기로 잡히지 않게).
        const vector<CityWalker>& listWalker = pDirector->getCity().getWalkers();
        const bool                bShown     = 0 <= _walkerIndex && _walkerIndex < static_cast<int32>( listWalker.size() );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        const CityWalker& walker   = listWalker.data()[_walkerIndex];
        const float2      position = CitySimulation::computeWalkerPosition( walker );
        pMesh->setLocalPosition( float3{ position._x, _height, position._y } );
        const shared_ptr<MaterialInstance>& look = pDirector->findWalkerLook( walker );
        if ( look == nullptr || look.get() == _pShownLook )
            return;
        pMesh->setMaterialInstance( look );
        _pShownLook = look.get();
    }
} // namespace sw
