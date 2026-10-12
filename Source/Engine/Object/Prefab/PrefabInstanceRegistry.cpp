#include "pch.h"

#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    SW_SCENE_SERVICE( PrefabInstanceRegistry, DestroyPost )

    PrefabInstanceRegistry::PrefabInstanceRegistry( GameObjectManager& manager )
        : _pManager{ &manager }
        , _mapNestedPart{}
        , _nextRegistration{ 0 }
    {
    }

    void PrefabInstanceRegistry::update( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void PrefabInstanceRegistry::shutdown()
    {
        _mapNestedPart.clear();
    }

    void PrefabInstanceRegistry::registerNestedPart( uint64 objectID, uint64 ownerObjectID, string_view prefabPath )
    {
        PrefabNestedPart& part  = _mapNestedPart[objectID];
        part._ownerObjectID     = ownerObjectID;
        part._registrationOrder = _nextRegistration;
        part._prefabPath        = string{ prefabPath };
        ++_nextRegistration;
    }

    void PrefabInstanceRegistry::unregisterNestedPart( uint64 objectID )
    {
        _mapNestedPart.erase( objectID );
    }

    const PrefabNestedPart* PrefabInstanceRegistry::findNestedPart( uint64 objectID ) const
    {
        const auto partIt = _mapNestedPart.find( objectID );
        return ( partIt != _mapNestedPart.end() ) ? &partIt->second : nullptr;
    }

    bool PrefabInstanceRegistry::isAttachedNestedPart( const GameObject* pObject ) const
    {
        if ( pObject == nullptr )
            return false;
        const PrefabNestedPart* pPart = findNestedPart( pObject->getObjectID() );
        if ( pPart == nullptr )
            return false;
        const GameObject* pOwner = _pManager->findGameObjectByID( pPart->_ownerObjectID );
        if ( pOwner == nullptr || pOwner->isPendingDestroy() )
            return false;
        return pObject->getPrimarySceneComponent() == nullptr || pObject->isDescendantOf( pOwner );
    }

    void PrefabInstanceRegistry::collectNestedPartsOf( uint64 ownerObjectID, vector<uint64>& outListObjectID ) const
    {
        outListObjectID.clear();
        // (기록 순서, 오브젝트 id) 로 모아 정렬한다 — 해시 표의 순회 순서는 정해져 있지 않다.
        vector<std::pair<uint64, uint64>> listOrdered;
        for ( const auto& [objectID, part] : _mapNestedPart )
        {
            if ( part._ownerObjectID == ownerObjectID )
                listOrdered.emplace_back( part._registrationOrder, objectID );
        }
        std::sort( listOrdered.begin(), listOrdered.end() );
        outListObjectID.reserve( listOrdered.size() );
        for ( const std::pair<uint64, uint64>& entry : listOrdered )
        {
            outListObjectID.push_back( entry.second );
        }
    }
} // namespace sw
