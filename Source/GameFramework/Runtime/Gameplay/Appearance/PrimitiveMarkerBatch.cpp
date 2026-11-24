#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/PrimitiveMarkerBatch.h"

#include "Core/Log/Logger.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabCache.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    PrimitiveMarkerBatch::PrimitiveMarkerBatch()
        : _prefabPath{}
        , _listMarker{}
        , _tintCache{}
        , _usedCount{ 0 }
        , _bRefused{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void PrimitiveMarkerBatch::setPrefab( string_view prefabPath )
    {
        _prefabPath = string( prefabPath );
        _bRefused   = SW_FALSE;
    }

    GameObject* PrimitiveMarkerBatch::spawnMarker( GameObjectManager& manager )
    {
        AssetManager* const pAssetManager = game::findSubsystem<AssetManager>();
        GameObject* const   pObject       = pAssetManager != nullptr && _prefabPath.empty() == false ? pAssetManager->getAssetCache<PrefabCache>().spawn( &manager, _prefabPath, "PrimitiveMarker" ) : nullptr;
        if ( pObject != nullptr && pObject->getComponent<MeshComponent>() != nullptr )
        {
            pObject->setTransient( true ); // 상태에서 다시 세우는 파생이다
            return pObject;
        }
        SW_LOG_ERROR_ID( "PrimitiveMarkerBatch.PrefabRefused", "PrimitiveMarkerBatch: the prefab '%#' could not be spawned or has no mesh component - the markers are not shown", _prefabPath.c_str() );
        if ( pObject != nullptr )
            manager.destroyGameObject( pObject );
        _bRefused = SW_TRUE;
        return nullptr;
    }

    void PrimitiveMarkerBatch::draw( GameObjectManager& manager, const AbsolutePosition& center, const float3& size, const float4& color )
    {
        if ( _bRefused == SW_TRUE )
            return;
        if ( _usedCount == _listMarker.size() )
            _listMarker.push_back( Marker{} );
        Marker&     marker  = _listMarker[_usedCount];
        GameObject* pObject = manager.resolveGameObject( marker._object );
        if ( pObject == nullptr )
        {
            pObject = spawnMarker( manager );
            if ( pObject == nullptr )
                return;
            marker         = Marker{};
            marker._object = pObject->getHandle();
        }
        ++_usedCount;
        MeshComponent& mesh = *pObject->getComponent<MeshComponent>(); // 세울 때 있는 것을 보았다
        mesh.setWorldPosition( manager.getWorldOrigin().toLocal( center ) );
        mesh.setLocalScale( size );
        if ( mesh.isVisible() == false )
            mesh.setVisible( true );
        const bool bSameColor = marker._bColored == SW_TRUE && marker._color._x == color._x && marker._color._y == color._y && marker._color._z == color._z && marker._color._w == color._w;
        if ( bSameColor || mesh.getMaterial() == nullptr )
            return; // 머티리얼은 비동기로 온다 — 온 프레임에 입힌다
        _tintCache.apply( mesh, color );
        marker._color    = color;
        marker._bColored = SW_TRUE;
    }

    void PrimitiveMarkerBatch::end( GameObjectManager& manager )
    {
        for ( size_t index = _usedCount; index < _listMarker.size(); ++index )
        {
            GameObject* const    pObject = manager.resolveGameObject( _listMarker[index]._object );
            MeshComponent* const pMesh   = pObject != nullptr ? pObject->getComponent<MeshComponent>() : nullptr;
            if ( pMesh != nullptr && pMesh->isVisible() )
                pMesh->setVisible( false );
        }
    }

    void PrimitiveMarkerBatch::destroyAll( GameObjectManager& manager )
    {
        for ( const Marker& marker : _listMarker )
        {
            GameObject* const pObject = manager.resolveGameObject( marker._object );
            if ( pObject != nullptr )
                manager.destroyGameObject( pObject );
        }
        _listMarker.clear();
        _usedCount = 0;
    }
} // namespace sw
