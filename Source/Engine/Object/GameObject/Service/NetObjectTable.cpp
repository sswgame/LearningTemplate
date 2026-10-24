#include "pch.h"

#include "Engine/Object/GameObject/Service/NetObjectTable.h"

#include "Core/Container/unordered_map.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "NetObjectTable" );

    SW_SCENE_SERVICE( NetObjectTable, FlushEnd )

    NetObjectTable::NetObjectTable( GameObjectManager& manager )
        : _pManager{ &manager }
        , _listScene{}
        , _listSpawned{}
        , _nextSpawned{ 1 }
        , _spawnedRevision{ 0 }
    {
    }

    void NetObjectTable::onSceneCleared()
    {
        _listScene.clear();
        _listSpawned.clear();
        _nextSpawned = 1;
        ++_spawnedRevision;
    }

    uint32 NetObjectTable::computeSceneID( const GameObject& object )
    {
        const uint32 sceneID = object.getName().getHash() & ~kSpawnedBit;
        return sceneID != 0 ? sceneID : 1u;
    }

    void NetObjectTable::captureSceneObjects()
    {
        vector<GameObject*> listObject;
        _pManager->getAllGameObjects( listObject );
        // 이미 번호가 있는 오브젝트(씬 · 생성)를 한 번 모아 정렬한다 — 오브젝트마다 두 목록을 선형으로 훑지 않는다(오브젝트 N 개에 O(N log N)).
        vector<GameObjectHandle> listKnown;
        listKnown.reserve( _listScene.size() + _listSpawned.size() );
        for ( const Entry& entry : _listScene )
        {
            listKnown.push_back( entry._object );
        }
        for ( const Entry& entry : _listSpawned )
        {
            listKnown.push_back( entry._object );
        }
        std::sort( listKnown.begin(), listKnown.end() );

        // 새 씬 번호는 모았다가 정렬해 한 번에 끼운다(하나씩 끼우면 목록 뒤쪽을 매번 옮겨 O(N²)). 이 호출 안에서 새로 잡은 번호끼리 겹치면 해시 표로 가린다.
        // 겹친 번호는 먼저 만난 오브젝트의 항목이 모호 표시를 달고 남는다(하나씩 끼울 때와 같다).
        vector<Entry>                 listNew;
        unordered_map<uint32, uint32> mapNewIndex;
        for ( GameObject* pObject : listObject )
        {
            const GameObjectHandle handle = pObject->getHandle();
            const bool             bKnown = std::binary_search( listKnown.begin(), listKnown.end(), handle );
            if ( bKnown || pObject->isPendingDestroy() )
                continue;
            const uint32 sceneID = computeSceneID( *pObject );
            const auto   it      = std::lower_bound( _listScene.begin(), _listScene.end(), sceneID, []( const Entry& entry, uint32 id )
                   { return entry._netID < id; } );
            if ( it != _listScene.end() && it->_netID == sceneID )
            {
                // 같은 번호(같은 이름 또는 해시 충돌) — 어느 쪽에도 주지 않는다. 다른 오브젝트에 조용히 묶이면 복제가 엉뚱한 오브젝트를 움직인다.
                if ( it->_bAmbiguous == SW_FALSE )
                    SW_LOG_ERROR( "Net object '%#' shares its scene id %# with another object - give replicated scene objects unique names", pObject->getName().c_str(), sceneID );
                it->_bAmbiguous = SW_TRUE;
                continue;
            }
            const auto itNew = mapNewIndex.find( sceneID );
            if ( itNew != mapNewIndex.end() )
            {
                Entry& earlier = listNew[itNew->second];
                if ( earlier._bAmbiguous == SW_FALSE )
                    SW_LOG_ERROR( "Net object '%#' shares its scene id %# with another object - give replicated scene objects unique names", pObject->getName().c_str(), sceneID );
                earlier._bAmbiguous = SW_TRUE;
                continue;
            }
            Entry entry{};
            entry._netID  = sceneID;
            entry._object = handle;
            mapNewIndex.emplace( sceneID, static_cast<uint32>( listNew.size() ) );
            listNew.push_back( entry );
        }
        if ( listNew.empty() )
            return;
        const auto byNetID = []( const Entry& left, const Entry& right )
        { return left._netID < right._netID; };
        std::sort( listNew.begin(), listNew.end(), byNetID );
        const size_t oldCount = _listScene.size();
        _listScene.reserve( oldCount + listNew.size() );
        for ( const Entry& entry : listNew )
        {
            _listScene.push_back( entry );
        }
        std::inplace_merge( _listScene.begin(), _listScene.begin() + oldCount, _listScene.end(), byNetID );
    }

    const NetObjectTable::Entry* NetObjectTable::findSceneEntry( uint32 netID ) const
    {
        const auto it = std::lower_bound( _listScene.begin(), _listScene.end(), netID, []( const Entry& entry, uint32 id )
        { return entry._netID < id; } );
        return ( it != _listScene.end() && it->_netID == netID ) ? &*it : nullptr;
    }

    const NetObjectTable::Entry* NetObjectTable::findSpawnedEntryByObject( GameObjectHandle object ) const
    {
        const auto it = std::find_if( _listSpawned.begin(), _listSpawned.end(), [&object]( const Entry& entry )
        { return entry._object == object; } );
        return it != _listSpawned.end() ? &*it : nullptr;
    }

    uint32 NetObjectTable::findID( const GameObject& object ) const
    {
        const Entry* pSpawned = findSpawnedEntryByObject( object.getHandle() );
        if ( pSpawned != nullptr )
            return pSpawned->_netID;
        const Entry* pScene  = findSceneEntry( computeSceneID( object ) );
        const bool   bIsThis = pScene != nullptr && pScene->_bAmbiguous == SW_FALSE && pScene->_object == object.getHandle();
        return bIsThis ? pScene->_netID : 0u;
    }

    GameObject* NetObjectTable::findObject( uint32 netID ) const
    {
        if ( netID == 0 )
            return nullptr;
        if ( ( netID & kSpawnedBit ) != 0 )
        {
            const auto it = std::lower_bound( _listSpawned.begin(), _listSpawned.end(), netID, []( const Entry& entry, uint32 id )
            { return entry._netID < id; } );
            return ( it != _listSpawned.end() && it->_netID == netID ) ? _pManager->resolveGameObject( it->_object ) : nullptr;
        }
        const Entry* pScene = findSceneEntry( netID );
        if ( pScene == nullptr || pScene->_bAmbiguous == SW_TRUE )
            return nullptr;
        return _pManager->resolveGameObject( pScene->_object );
    }

    uint32 NetObjectTable::registerSpawned( GameObject& object )
    {
        const Entry* pExisting = findSpawnedEntryByObject( object.getHandle() );
        if ( pExisting != nullptr )
            return pExisting->_netID;
        Entry entry{};
        entry._netID  = kSpawnedBit | _nextSpawned++;
        entry._object = object.getHandle();
        const auto it = std::lower_bound( _listSpawned.begin(), _listSpawned.end(), entry._netID, []( const Entry& other, uint32 id )
        { return other._netID < id; } );
        (void)_listSpawned.insert( it, entry );
        ++_spawnedRevision;
        return entry._netID;
    }

    bool NetObjectTable::bindSpawned( uint32 netID, GameObject& object )
    {
        if ( ( netID & kSpawnedBit ) == 0 || netID == kSpawnedBit )
        {
            SW_LOG_ERROR( "Net object '%#' cannot be bound to %# - it is not a spawned id", object.getName().c_str(), netID );
            return false;
        }
        const Entry* pOwn = findSpawnedEntryByObject( object.getHandle() );
        if ( pOwn != nullptr )
        {
            if ( pOwn->_netID == netID )
                return true;
            SW_LOG_ERROR( "Net object '%#' already has spawned id %# - cannot bind it to %#", object.getName().c_str(), pOwn->_netID, netID );
            return false;
        }
        const auto it = std::lower_bound( _listSpawned.begin(), _listSpawned.end(), netID, []( const Entry& entry, uint32 id )
        { return entry._netID < id; } );
        if ( it != _listSpawned.end() && it->_netID == netID )
        {
            // 묶였던 오브젝트가 지워졌으면 새 오브젝트로 바꾼다(서버가 같은 번호를 다시 알린다 — 늦은 참가 · 재접속).
            if ( _pManager->resolveGameObject( it->_object ) != nullptr )
            {
                SW_LOG_ERROR( "Spawned net id %# is already bound to another object - '%#' was not bound", netID, object.getName().c_str() );
                return false;
            }
            it->_object = object.getHandle();
            ++_spawnedRevision;
            return true;
        }
        Entry entry{};
        entry._netID  = netID;
        entry._object = object.getHandle();
        (void)_listSpawned.insert( it, entry );
        ++_spawnedRevision;
        return true;
    }

    void NetObjectTable::unregisterSpawned( const GameObject& object )
    {
        const GameObjectHandle handle = object.getHandle();
        const auto             it     = std::find_if( _listSpawned.begin(), _listSpawned.end(), [&handle]( const Entry& entry )
                        { return entry._object == handle; } );
        if ( it == _listSpawned.end() )
            return;
        _listSpawned.erase( it );
        ++_spawnedRevision;
    }

    void NetObjectTable::collectObjects( vector<GameObject*>& outListObject ) const
    {
        outListObject.clear();
        for ( const vector<Entry>* pList : { &_listScene, &_listSpawned } )
        {
            for ( const Entry& entry : *pList )
            {
                GameObject* pObject = entry._bAmbiguous == SW_TRUE ? nullptr : _pManager->resolveGameObject( entry._object );
                if ( pObject != nullptr )
                    outListObject.push_back( pObject );
            }
        }
    }
} // namespace sw
