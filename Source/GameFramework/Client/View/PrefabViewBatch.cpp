#include "pch.h"

#include "GameFramework/Client/View/PrefabViewBatch.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    PrefabViewBatch::PrefabViewBatch()
        : _listSlot{}
        , _tintCache{}
        , _shownCount{ 0 }
        , _bRefused{ SW_FALSE }
    {
    }

    bool PrefabViewBatch::show( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const AbsolutePosition& position, const float3& scale, float32 yaw, const float4& tint )
    {
        if ( _bRefused == SW_TRUE )
            return false;
        if ( _shownCount >= _listSlot.size() )
            _listSlot.emplace_back();
        Slot&          slot    = _listSlot[_shownCount];
        GameObject*    pObject = manager.resolveGameObject( slot._object );
        MeshComponent* pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
        if ( pObject == nullptr )
        {
            AssetManager* const pAssets = game::findSubsystem<AssetManager>();
            pObject                     = pAssets != nullptr && prefab.empty() == false ? pAssets->getAssetCache<PrefabCache>().spawn( &manager, prefab, SpawnLifetime::Derived, "RuleView" ) : nullptr;
            pMesh                       = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            if ( pMesh == nullptr )
            {
                SW_LOG_ERROR_ID( "PrefabViewBatch.PrefabUnusable", "View prefab '%#' could not be spawned or has no mesh on its root object - its views are not shown", prefab.c_str() );
                if ( pObject != nullptr )
                    manager.destroyGameObject( pObject );
                _bRefused = SW_TRUE;
                return false;
            }
            slot._object       = pObject->getHandle();
            slot._prefabOffset = pMesh->getLocalPosition();
            slot._prefabScale  = pMesh->getLocalScale();
        }
        if ( pMesh == nullptr )
            return false; // 세운 뒤 메시를 뗀 인스턴스다 — 다른 쪽이 한 일이라 다시 붙이지 않는다
        if ( pObject->isActive() == false )
            pObject->setActive( true );
        pMesh->setLocalPosition( manager.getWorldOrigin().toLocal( position + slot._prefabOffset ) );
        pMesh->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
        pMesh->setLocalScale( float3{ slot._prefabScale._x * scale._x, slot._prefabScale._y * scale._y, slot._prefabScale._z * scale._z } );
        (void)_tintCache.apply( *pMesh, tint ); // 머티리얼이 아직 없으면 다음 프레임에 다시 건다(같은 색은 메시가 거른다)
        ++_shownCount;
        return true;
    }

    void PrefabViewBatch::showStanding( GameObjectManager& manager, const AssetRef<PrefabAsset>& prefab, const AbsolutePosition& foot, const float3& size, const float4& tint )
    {
        (void)show( manager, prefab, foot + float3{ 0.0f, size._y * 0.5f, 0.0f }, size, 0.0f, tint );
    }

    void PrefabViewBatch::end( GameObjectManager& manager )
    {
        for ( size_t slotIndex = _shownCount; slotIndex < _listSlot.size(); ++slotIndex )
        {
            GameObject* const pObject = manager.resolveGameObject( _listSlot[slotIndex]._object );
            if ( pObject != nullptr && pObject->isActive() )
                pObject->setActive( false );
        }
    }

    void PrefabViewBatch::destroyAll( GameObjectManager& manager )
    {
        for ( const Slot& slot : _listSlot )
        {
            GameObject* const pObject = manager.resolveGameObject( slot._object );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _listSlot.clear();
        _tintCache.clear();
        _shownCount = 0;
        _bRefused   = SW_FALSE;
    }
} // namespace sw
