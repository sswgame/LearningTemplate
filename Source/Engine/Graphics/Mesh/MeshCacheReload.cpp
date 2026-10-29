/**
 * @file MeshCacheReload.cpp
 * @brief 메시 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않고, 등록은 `SW_ASSET_CACHE_RELOAD_FUNCTION` 이 nullptr 이다).
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"

namespace sw
{
    SW_LOG_CALLER( "MeshCache" );

    bool MeshCache::reloadShared( string_view path, IRHIDevice* pDevice )
    {
        if ( path.empty() )
            return false;
        shared_ptr<Mesh> live = findLiveMesh( path );
        if ( live == nullptr )
            return false;

        // 읽기에 실패하면 이전 정점을 유지한다. 저장 중이라 반쯤 쓰인 파일을 읽은 경우이며, 다음 감시 이벤트가 다시 읽는다.
        MeshAssetData data{};
        if ( MeshAssetFormat::loadFromAsset( AssetID{ path }, data ) == false )
        {
            SW_LOG_ERROR( "Hot-Reload failed for Mesh %#", path );
            return false;
        }

        // 지난 프레임이 아직 이전 정점 버퍼로 그리고 있을 수 있다. setVertices 는 버퍼를 곧바로 반환하므로 먼저 기다린다(TextureCache::reload 와 같은 이유).
        if ( pDevice != nullptr )
            pDevice->waitIdle( RHIWaitIdleReason::HotReload );
        live->setVerticesWithTangents( std::move( data._listVertex ) );
        live->setSkin( std::move( data._listSkinVertex ), data._skinBoneCount );
        live->setMorphTargets( std::move( data._listMorphTarget ) );
        return live->setSections( std::move( data._listSection ) ) && live->setLODs( std::move( data._listLOD ) );
    }

    void MeshCache::reload( const AssetID& assetID, IRHIDevice* pDevice )
    {
        const string_view relativePath = assetID.view();
        (void)reloadShared( relativePath, pDevice ); // 사용 중인 메시가 없으면 다시 읽을 것이 없다. 다음에 읽는 쪽이 새 내용을 읽는다
        MeshInvalidationHook::notifyAll();           // 메시에서 기하를 만든 위 층 캐시(물리 셰이프)를 비운다. 이미 만든 바디는 이전 기하 그대로다
    }
} // namespace sw
