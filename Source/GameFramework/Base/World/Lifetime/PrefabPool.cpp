#include "pch.h"

#include "GameFramework/Base/World/Lifetime/PrefabPool.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/AssetManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "PrefabPool" );

    PrefabPool::PrefabPool( string prefabPath, uint32 maxIdleCount )
        : _prefabPath{ std::move( prefabPath ) }
        , _listIdle{}
        , _maxIdleCount{ maxIdleCount }
        , _createdCount{ 0 }
    {
    }

    GameObject* PrefabPool::spawnInstance( GameObjectManager& manager, const utf8* pInstanceName )
    {
        AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
        if ( pAssetManager == nullptr || _prefabPath.empty() )
        {
            SW_LOG_ERROR( "Prefab pool '%#' cannot spawn - %#", _prefabPath, _prefabPath.empty() ? "no prefab path" : "no asset manager" );
            return nullptr;
        }
        GameObject* pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, _prefabPath, pInstanceName );
        if ( pObject != nullptr )
            ++_createdCount;
        return pObject;
    }

    GameObject* PrefabPool::acquire( GameObjectManager& manager, const utf8* pInstanceName )
    {
        // 지워진 핸들(씬이 지웠다)은 걷어 내며 살아 있는 것을 찾는다.
        while ( _listIdle.empty() == false )
        {
            const GameObjectHandle handle = _listIdle.back();
            _listIdle.pop_back();
            GameObject* pObject = manager.resolveGameObject( handle );
            if ( pObject == nullptr || pObject->isPendingDestroy() )
                continue;
            pObject->setActive( true );
            return pObject;
        }
        return spawnInstance( manager, pInstanceName );
    }

    void PrefabPool::release( GameObjectManager& manager, GameObjectHandle handle )
    {
        GameObject* pObject = manager.resolveGameObject( handle );
        if ( pObject == nullptr || pObject->isPendingDestroy() )
            return;
        if ( _listIdle.size() >= _maxIdleCount )
        {
            manager.destroyObject( pObject );
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
            GameObject* pObject = manager.resolveGameObject( handle );
            if ( pObject != nullptr && pObject->isPendingDestroy() == false )
                manager.destroyObject( pObject );
        }
        _listIdle.clear();
    }
} // namespace sw
