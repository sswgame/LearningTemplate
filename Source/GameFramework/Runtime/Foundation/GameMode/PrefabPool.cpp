#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

#include <algorithm>

namespace sw
{
    PrefabPool::PrefabPool( string prefabPath, uint32 maxIdleCount, SpawnLifetime lifetime )
        : _prefabPath{ std::move( prefabPath ) }
        , _spawner{}
        , _listIdle{}
        , _busyOrder{}
        , _listScratchHandle{}
        , _maxIdleCount{ maxIdleCount }
        , _lifetime{ lifetime }
        , _createdCount{}
        , _bContractChecked{ SW_FALSE }
        , _bRefused{ SW_FALSE }
    {
    }

    const Component* PrefabPool::resetHierarchy( GameObject& object )
    {
        // 되돌리기가 컴포넌트를 떼거나(외형의 합친 구간) 자식을 거둘 수 있다 — 목록을 베껴 돌고, 아직 붙어 있는 것만 부른다(포인터는 비교만 한다).
        const vector<Component*> listComponent( object.getComponents().begin(), object.getComponents().end() );
        for ( Component* pComponent : listComponent )
        {
            const bool bAttached = pComponent != nullptr && VectorUtil::contains( object.getComponents(), pComponent ) && pComponent->isPendingDestroy() == false;
            if ( bAttached && pComponent->resetToSpawnState() == SpawnStateReset::Unsupported )
                return pComponent;
        }
        vector<GameObject*> listChild;
        object.getChildren( listChild );
        for ( GameObject* pChild : listChild )
        {
            const Component* pUnsupported = pChild != nullptr && pChild->isPendingDestroy() == false ? resetHierarchy( *pChild ) : nullptr;
            if ( pUnsupported != nullptr )
                return pUnsupported;
        }
        return nullptr;
    }

    GameObject* PrefabPool::findLive( GameObjectManager& manager, GameObjectHandle handle )
    {
        GameObject* pObject = manager.resolveGameObject( handle );
        return pObject != nullptr && pObject->isPendingDestroy() == false ? pObject : nullptr;
    }

    GameObject* PrefabPool::spawnInstance( GameObjectManager& manager, const utf8* pInstanceName )
    {
        if ( _bRefused == SW_TRUE )
            return nullptr; // 계약 위반은 처음 만들 때 알렸다
        GameObject* pObject = nullptr;
        if ( _spawner.isBound() )
        {
            pObject = _spawner( manager, pInstanceName );
            if ( pObject != nullptr )
                pObject->setTransient( _lifetime == SpawnLifetime::Derived );
        }
        else
        {
            AssetManager* pAssetManager = game::findSubsystem<AssetManager>();
            if ( pAssetManager == nullptr || _prefabPath.empty() )
            {
                SW_LOG_ERROR_ID( "PrefabPool.PrefabPoolCannotSpawn", "Prefab pool '%#' cannot spawn - %#", _prefabPath, _prefabPath.empty() ? "no prefab path" : "no asset manager" );
                return nullptr;
            }
            pObject = pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, _prefabPath, _lifetime, pInstanceName );
        }
        if ( pObject == nullptr )
            return nullptr;
        if ( _bContractChecked == SW_FALSE )
        {
            // 풀 계약: 갓 스폰한 인스턴스에 되돌리기를 불러 본다(갓 스폰한 상태에서는 아무것도 바뀌지 않아야 한다). 모르는 컴포넌트가 하나라도 있으면 이 프리팹은 풀에 들 수 없다.
            const Component* pUnsupported = resetHierarchy( *pObject );
            if ( pUnsupported != nullptr )
            {
                SW_LOG_ERROR_ID( "PrefabPool.ComponentNotPoolable", "Prefab pool '%#' refused '%#': component '%#' does not implement resetToSpawnState - pool it only after it can return to its spawn state",
                                 _prefabPath, pObject->getName().c_str(), pUnsupported->getTypeName().c_str() );
                manager.destroyGameObject( pObject );
                _bRefused = SW_TRUE;
                return nullptr;
            }
            _bContractChecked = SW_TRUE;
        }
        _createdCount.add();
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
            _busyOrder.insertOrAssign( pObject->getHandle(), 0 );
        return pObject;
    }

    GameObject* PrefabPool::acquireRecycling( GameObjectManager& manager, uint32 maxBusyCount, const utf8* pInstanceName )
    {
        // 앞이 가장 오래 꺼낸 것이다. 지워진 핸들은 수에 넣지 않는다.
        while ( _busyOrder.empty() == false && _busyOrder.size() >= maxBusyCount )
        {
            const GameObjectHandle oldest  = _busyOrder.begin()->_key;
            GameObject*            pObject = findLive( manager, oldest );
            if ( pObject == nullptr )
            {
                (void)_busyOrder.erase( oldest );
                continue;
            }
            // 쓰던 것을 빼앗아 다시 내준다 — 앞에 꺼낸 쪽의 핸들은 다음 생을 가리키지 않는다.
            (void)_busyOrder.erase( oldest );
            pObject->invalidateHandles();
            _busyOrder.insertOrAssign( pObject->getHandle(), 0 );
            return pObject;
        }
        return acquire( manager, pInstanceName );
    }

    void PrefabPool::release( GameObjectManager& manager, GameObjectHandle handle )
    {
        (void)_busyOrder.erase( handle ); // 꺼낸 것이 아니어도(이미 돌려준 핸들) 아래가 거른다
        GameObject* pObject = findLive( manager, handle );
        if ( pObject == nullptr )
            return;
        // 쉬는 수가 상한이라 지울 것도 먼저 되돌린다 — 되돌리기가 남에게 돌려줄 것(외형 부품 → 씬의 부품 풀, AI 조종자)을 돌려준 뒤 지운다.
        const Component* pUnsupported = resetHierarchy( *pObject );
        if ( pUnsupported != nullptr )
        {
            // 실행 중에 붙은 것(돌려주기 전에 누가 자식으로 단 오브젝트)이 되돌리기를 모르면 쉬게 하지 않고 지운다 — 반쯤 되돌린 것을 다시 내주지 않는다.
            SW_LOG_ERROR_ID( "PrefabPool.ReleasedComponentNotPoolable", "Prefab pool '%#' destroyed '%#' instead of keeping it: component '%#' does not implement resetToSpawnState", _prefabPath,
                             pObject->getName().c_str(), pUnsupported->getTypeName().c_str() );
            manager.destroyGameObject( pObject );
            return;
        }
        if ( _listIdle.size() >= _maxIdleCount )
        {
            manager.destroyGameObject( pObject );
            return;
        }
        pObject->setActive( false );
        pObject->invalidateHandles(); // 앞 생을 겨누던 쪽(잠금 조준 · 킬캠 · 포탑 · 가해자)이 다시 꺼낸 생을 보지 않는다 — 같은 핸들을 두 번 돌려줘도 두 번 쉬지 않는다
        _listIdle.push_back( pObject->getHandle() );
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
        for ( const auto& entry : _busyOrder )
        {
            GameObject* pObject = findLive( manager, entry._key );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _busyOrder.clear();
    }
} // namespace sw
