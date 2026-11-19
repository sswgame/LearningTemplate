#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/WorldItemComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct WorldItemComponentInternal
        {
            static constexpr const utf8* kMeshID    = "Cube"; ///< 떨어진 조각의 모습(엔진 기본 메시)
            static constexpr float32     kMeshScale = 0.3f;   ///< 상자 한 변(m)
        };
    } // namespace

    WorldItemComponent::WorldItemComponent()
        : Component{}
        , _item{}
        , _itemID{}
        , _count{ 1 }
    {
    }

    WorldItemComponent::~WorldItemComponent() = default;

    void WorldItemComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _item.isEmpty() && _itemID.empty() == false )
        {
            _item._itemID = _itemID;
            _item._count  = _count;
        }
    }

    GameObject* WorldItemComponent::spawn( GameObjectManager& manager, const InventorySlot& item, const float3& position )
    {
        if ( item.isEmpty() )
            return nullptr;
        using Internal      = WorldItemComponentInternal;
        GameObject* pObject = manager.createGameObject( hashed_string( kSpawnedName ) );
        if ( pObject == nullptr )
            return nullptr;
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        if ( pMesh != nullptr )
        {
            pMesh->setMeshID( Internal::kMeshID );
            pMesh->setLocalPosition( position );
            pMesh->setLocalScale( float3{ Internal::kMeshScale, Internal::kMeshScale, Internal::kMeshScale } );
        }
        WorldItemComponent* pItem = pObject->addComponent<WorldItemComponent>();
        if ( pItem != nullptr )
        {
            pItem->_item   = item;
            pItem->_itemID = item._itemID; // 씬 저장 · 불러오기는 id · 수를 싣는다(개체 상태는 이 실행 동안만)
            pItem->_count  = item._count;
        }
        return pObject;
    }

    bool WorldItemComponent::pickUp( Inventory& bag )
    {
        if ( _item.isEmpty() || bag.addStack( _item ) == false )
            return false;
        SW_LOG_INFO_ID( "WorldItem.PickedUp", "Picked up %# x%#", _item._itemID.c_str(), _item._count );
        _item                       = InventorySlot{};
        GameObjectManager* pManager = engine::findActiveObjectManager();
        if ( pManager != nullptr && getOwner() != nullptr )
            pManager->destroyGameObject( getOwner() );
        return true;
    }
} // namespace sw
