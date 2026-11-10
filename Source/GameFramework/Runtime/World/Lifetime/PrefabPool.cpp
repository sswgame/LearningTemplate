#include "pch.h"

#include "GameFramework/Runtime/World/Lifetime/PrefabPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include <algorithm>

namespace sw
{
    PrefabPool::PrefabPool( string prefabPath, uint32 maxIdleCount )
        : _prefabPath{ std::move( prefabPath ) }
        , _spawner{}
        , _listIdle{}
        , _listBusy{}
        , _maxIdleCount{ maxIdleCount }
        , _createdCount{ 0 }
    {
    }

    GameObject* PrefabPool::findLive( GameObjectManager& manager, GameObjectHandle handle )
    {
        GameObject* pObject = manager.resolveGameObject( handle );
        return pObject != nullptr && pObject->isPendingDestroy() == false ? pObject : nullptr;
    }

    GameObject* PrefabPool::spawnInstance( GameObjectManager& manager, const utf8* pInstanceName )
    {
        GameObject* pObject = nullptr;
        if ( _spawner.isBound() )
        {
            pObject = _spawner( manager, pInstanceName );
        }
        else
        {
            AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
            if ( pAssetManager == nullptr || _prefabPath.empty() )
            {
                SW_LOG_ERROR_ID( "PrefabPool.PrefabPoolCannotSpawn", "Prefab pool '%#' cannot spawn - %#", _prefabPath, _prefabPath.empty() ? "no prefab path" : "no asset manager" );
                return nullptr;
            }
            pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, _prefabPath, pInstanceName );
        }
        if ( pObject != nullptr )
            ++_createdCount;
        return pObject;
    }

    GameObject* PrefabPool::acquire( GameObjectManager& manager, const utf8* pInstanceName )
    {
        // 지워진 핸들(씬이 지웠다)은 걷어 내며 살아 있는 것을 찾는다.
        GameObject* pObject = nullptr;
        while ( pObject == nullptr && _listIdle.empty() == false )
        {
            pObject = findLive( manager, _listIdle.back() );
            _listIdle.pop_back();
            if ( pObject != nullptr )
                pObject->setActive( true );
        }
        if ( pObject == nullptr )
            pObject = spawnInstance( manager, pInstanceName );
        if ( pObject != nullptr )
            _listBusy.push_back( pObject->getHandle() );
        return pObject;
    }

    GameObject* PrefabPool::acquireRecycling( GameObjectManager& manager, uint32 maxBusyCount, const utf8* pInstanceName )
    {
        // 앞이 가장 오래 꺼낸 것이다. 지워진 핸들은 수에 넣지 않는다.
        while ( _listBusy.empty() == false && _listBusy.size() >= maxBusyCount )
        {
            const GameObjectHandle oldest = _listBusy.front();
            _listBusy.erase( _listBusy.begin() );
            GameObject* pObject = findLive( manager, oldest );
            if ( pObject == nullptr )
                continue;
            _listBusy.push_back( oldest );
            return pObject;
        }
        return acquire( manager, pInstanceName );
    }

    void PrefabPool::release( GameObjectManager& manager, GameObjectHandle handle )
    {
        const auto itBusy = std::find( _listBusy.begin(), _listBusy.end(), handle );
        if ( itBusy != _listBusy.end() )
            _listBusy.erase( itBusy );
        GameObject* pObject = findLive( manager, handle );
        if ( pObject == nullptr )
            return;
        if ( _listIdle.size() >= _maxIdleCount )
        {
            manager.destroyGameObject( pObject );
            return;
        }
        pObject->setActive( false );
        _listIdle.push_back( handle );
    }

    uint32 PrefabPool::prewarm( GameObjectManager& manager, uint32 count, const utf8* pInstanceName )
    {
        const uint32 target  = count < _maxIdleCount ? count : _maxIdleCount;
        uint32       created = 0;
        while ( _listIdle.size() < target )
        {
            GameObject* pObject = spawnInstance( manager, pInstanceName );
            if ( pObject == nullptr )
                break; // 프리팹을 읽지 못했다 — 같은 실패를 되풀이하지 않는다
            pObject->setActive( false );
            _listIdle.push_back( pObject->getHandle() );
            ++created;
        }
        return created;
    }

    void PrefabPool::clear( GameObjectManager& manager )
    {
        for ( const GameObjectHandle& handle : _listIdle )
        {
            GameObject* pObject = findLive( manager, handle );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _listIdle.clear();
    }

    void PrefabPool::destroyAll( GameObjectManager& manager )
    {
        clear( manager );
        for ( const GameObjectHandle& handle : _listBusy )
        {
            GameObject* pObject = findLive( manager, handle );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _listBusy.clear();
    }
} // namespace sw
