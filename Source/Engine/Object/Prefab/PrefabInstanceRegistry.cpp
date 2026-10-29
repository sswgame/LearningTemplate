#include "pch.h"

#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"

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
        pruneDestroyedParts();
    }

    void PrefabInstanceRegistry::pruneDestroyedParts()
    {
        for ( auto partIt = _mapNestedPart.begin(); partIt != _mapNestedPart.end(); )
        {
            if ( _pManager->findGameObjectByID( partIt->first ) == nullptr )
                partIt = _mapNestedPart.erase( partIt );
            else
                ++partIt;
        }
    }

    void PrefabInstanceRegistry::onSceneCleared()
    {
        _mapNestedPart.clear();
    }

    void PrefabInstanceRegistry::registerNestedPart( uint64 objectID, uint64 ownerObjectID, string_view prefabPath, string_view partKey )
    {
        PrefabNestedPart& part  = _mapNestedPart[objectID];
        part._ownerObjectID     = ownerObjectID;
        part._registrationOrder = _nextRegistration;
        part._prefabPath        = string{ prefabPath };
        part._partKey           = string{ partKey };
        ++_nextRegistration;
    }

    void PrefabInstanceRegistry::restoreNestedPart( uint64 objectID, const PrefabNestedPart& part )
    {
        _mapNestedPart[objectID] = part;
        // 복원한 순서보다 뒤에 기록하는 부품이 늘 뒤에 오게 한다.
        _nextRegistration = MathUtil::max( _nextRegistration, part._registrationOrder + 1 );
    }

    bool PrefabInstanceRegistry::makePartPath( uint64 objectID, uint64 rootObjectID, string& outPath ) const
    {
        outPath.clear();
        vector<const string*> listKey;
        uint64                currentID = objectID;
        // 기록 수보다 깊게 올라가면 고리다(기록을 손으로 고친 경우) — 멈춘다.
        for ( size_t depth = 0; depth <= _mapNestedPart.size(); ++depth )
        {
            if ( currentID == rootObjectID )
            {
                for ( auto keyIt = listKey.rbegin(); keyIt != listKey.rend(); ++keyIt )
                {
                    if ( outPath.empty() == false )
                        outPath += "/";
                    outPath += **keyIt;
                }
                return listKey.empty() == false;
            }
            const PrefabNestedPart* pPart = findNestedPart( currentID );
            if ( pPart == nullptr || pPart->_partKey.empty() )
                return false;
            listKey.push_back( &pPart->_partKey );
            currentID = pPart->_ownerObjectID;
        }
        return false;
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
        vector<pair<uint64, uint64>> listOrdered;
        for ( const auto& [objectID, part] : _mapNestedPart )
        {
            if ( part._ownerObjectID == ownerObjectID )
                listOrdered.emplace_back( part._registrationOrder, objectID );
        }
        VectorUtil::sort( listOrdered );
        outListObjectID.reserve( listOrdered.size() );
        for ( const pair<uint64, uint64>& entry : listOrdered )
        {
            outListObjectID.push_back( entry.second );
        }
    }
} // namespace sw
