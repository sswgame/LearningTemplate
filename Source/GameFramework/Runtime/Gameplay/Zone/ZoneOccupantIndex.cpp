#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneOccupantIndex.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneSpatialGrid.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw
{
    void ZoneOccupantIndex::addUser( GameObjectManager& manager )
    {
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        grid.addUser( PhysicsComponent::getCharacterSpatialLayer() );
        grid.addUser( PhysicsComponent::getBodySpatialLayer() );
    }

    void ZoneOccupantIndex::removeUser( GameObjectManager& manager )
    {
        SceneSpatialGrid& grid = manager.getSubsystem<SceneSpatialGrid>();
        grid.removeUser( PhysicsComponent::getCharacterSpatialLayer() );
        grid.removeUser( PhysicsComponent::getBodySpatialLayer() );
    }

    void ZoneOccupantIndex::queryCandidates( GameObjectManager& manager, float32 cellSize, float32 minX, float32 minZ, float32 maxX, float32 maxZ, vector<Component*>& inoutListKey,
                                             vector<ZoneOccupantCandidate>& outListCandidate )
    {
        outListCandidate.clear();
        SceneSpatialGrid&           grid           = manager.getSubsystem<SceneSpatialGrid>();
        const SpatialGridLayerDesc& characterLayer = PhysicsComponent::getCharacterSpatialLayer();
        const SpatialGridLayerDesc& bodyLayer      = PhysicsComponent::getBodySpatialLayer();
        grid.requireCellSize( characterLayer, cellSize );
        grid.requireCellSize( bodyLayer, cellSize );
        // 볼륨이 사용자로 셌고 셀이 0 보다 크니 맞추는 층이다. 아니면 볼륨이 사용자를 세지 않은 것이다.
        SW_ASSERT( grid.isActive( characterLayer ) && grid.isActive( bodyLayer ) );
        if ( grid.isActive( characterLayer ) == false || grid.isActive( bodyLayer ) == false )
            return;

        // 이 층들의 열쇠는 물리 컴포넌트 자신이다(`PhysicsComponent::onRegister`).
        grid.query( characterLayer, minX, minZ, maxX, maxZ, inoutListKey );
        for ( Component* pKey : inoutListKey )
        {
            const CharacterControllerComponent* pController = static_cast<const CharacterControllerComponent*>( pKey );
            GameObject*                         pObject     = pController->getOwner();
            if ( pObject != nullptr )
                outListCandidate.push_back( ZoneOccupantCandidate{ pObject, pController, nullptr } );
        }
        grid.query( bodyLayer, minX, minZ, maxX, maxZ, inoutListKey );
        for ( Component* pKey : inoutListKey )
        {
            const RigidBodyComponent* pBody    = static_cast<const RigidBodyComponent*>( pKey );
            GameObject*               pObject  = pBody->getOwner();
            const bool                bDynamic = pBody->getBodyType() == PhysicsBodyType::Dynamic && pBody->isTrigger() == false;
            if ( bDynamic == false || pObject == nullptr || pObject->getComponent<CharacterControllerComponent>() != nullptr )
                continue;
            outListCandidate.push_back( ZoneOccupantCandidate{ pObject, nullptr, pBody } );
        }
    }
} // namespace sw
