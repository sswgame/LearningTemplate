#include "pch.h"

#include "Engine/Renderer/Lighting/ShadowCache.h"

#include "Core/Memory/Memory.h"

namespace sw
{
    bool ShadowCache::isSameTile( const ShadowTileDraw& first, const ShadowTileDraw& second )
    {
        // 행렬은 비트로 본다(구조체 꼬리의 정렬 빈 칸은 보지 않는다).
        return Memory::compare( &first._viewProj, &second._viewProj, sizeof( float4x4 ) ) == 0 && first._cell._x == second._cell._x && first._cell._y == second._cell._y &&
               first._cell._size == second._cell._size;
    }

    ShadowCache::ShadowCache()
        : _listLastTile{}
        , _listTileValid{}
        , _listAction{}
        , _layerPool{}
        , _staticGeneration{ 0 }
        , _atlasResolution{ 0 }
        , _layerResolution{ 0 }
        , _layerFormat{ RHIFormat::Unknown }
        , _bNeedsRebuild{ SW_FALSE }
        , _bUsesLayer{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void ShadowCache::update( const vector<ShadowTileDraw>& listLocalTile, uint32 atlasResolution, uint64 staticGeneration, bool bEnabled )
    {
        const size_t tileCount = listLocalTile.size();
        _listAction.assign( tileCount, ShadowCacheTileAction::DrawAll );
        _bNeedsRebuild = SW_FALSE;
        _bUsesLayer    = SW_FALSE;
        if ( bEnabled == false )
        {
            invalidate();
            _listLastTile = listLocalTile;
            return;
        }

        // 층 전체가 무효인 변화 — 해상도(칸 자리가 다르다)와 정적 메시의 변화(어느 타일에 닿는지 모른다. 언리얼은 경계로 가르지만 여기서는 통째로 버린다).
        const bool bLayerInvalid = atlasResolution != _atlasResolution || staticGeneration != _staticGeneration;
        _atlasResolution         = atlasResolution;
        _staticGeneration        = staticGeneration;
        _listTileValid.resize( tileCount, SW_FALSE );
        bool bAnyInvalid = false;
        for ( size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex )
        {
            // 키는 비트 그대로다 — 빛이 엡실론 아래로 움직여도 다른 그림이다(GPUSceneBuilder 후보 비교와 같은 까닭).
            const bool bMoved = tileIndex >= _listLastTile.size() || isSameTile( listLocalTile[tileIndex], _listLastTile[tileIndex] ) == false;
            if ( bMoved )
            {
                _listTileValid[tileIndex] = SW_FALSE;
                continue; // DrawAll — 움직이는 빛은 지금까지처럼 다 그린다
            }
            bAnyInvalid = bAnyInvalid || bLayerInvalid || _listTileValid[tileIndex] == SW_FALSE;
        }
        // 층을 통째로 지우고 다시 그리므로 키가 그대로인 타일은 모두 Rebuild 이고 그 뒤로 유효하다.
        for ( size_t tileIndex = 0; tileIndex < tileCount; ++tileIndex )
        {
            const bool bMoved = tileIndex >= _listLastTile.size() || isSameTile( listLocalTile[tileIndex], _listLastTile[tileIndex] ) == false;
            if ( bMoved )
                continue;
            _listAction[tileIndex]    = bAnyInvalid ? ShadowCacheTileAction::Rebuild : ShadowCacheTileAction::Reuse;
            _listTileValid[tileIndex] = SW_TRUE;
            _bUsesLayer               = SW_TRUE;
            _bNeedsRebuild            = bAnyInvalid ? SW_TRUE : _bNeedsRebuild;
        }
        _listLastTile = listLocalTile;
    }

    ShadowCacheTileAction ShadowCache::getAction( uint32 tileIndex ) const
    {
        return tileIndex < _listAction.size() ? _listAction[tileIndex] : ShadowCacheTileAction::DrawAll;
    }

    void ShadowCache::invalidate()
    {
        _listTileValid.clear();
        _bNeedsRebuild = SW_FALSE;
        _bUsesLayer    = SW_FALSE;
    }

    bool ShadowCache::ensureLayer( IRHIDevice* pDevice, RHIFormat atlasFormat, uint32 atlasResolution )
    {
        if ( pDevice == nullptr || atlasResolution == 0 || atlasFormat == RHIFormat::Unknown )
            return false;
        const bool bRecreate = _layerResolution != atlasResolution || _layerFormat != atlasFormat;
        if ( bRecreate )
        {
            _layerPool.release( pDevice );
            _layerResolution = 0;
            // 새 층은 비어 있다 — 이번 프레임의 Reuse 를 Rebuild 로 바꾼다(규칙은 텍스처를 모른다).
            for ( ShadowCacheTileAction& action : _listAction )
            {
                if ( action == ShadowCacheTileAction::Reuse )
                    action = ShadowCacheTileAction::Rebuild;
            }
            _bNeedsRebuild = _bUsesLayer;
        }
        // 그림자 아틀라스와 같은 형식 · 크기(부르는 쪽이 아틀라스 첨부의 값을 준다) — 깊이를 텍셀 그대로 옮긴다. 다시 그릴 때마다 1 로 지운다.
        RHIAttachmentOps ops{};
        ops._clearDepth = 1.0f;
        ops._loadOp     = RHIRenderPassLoadOp::Clear;
        ops._storeOp    = RHIRenderPassStoreOp::Store;
        if ( _layerPool.allocateSized( pDevice, hashed_string( kCacheLayerName ), atlasFormat, true, ops, atlasResolution, atlasResolution ) == false )
            return false;
        _layerResolution = atlasResolution;
        _layerFormat     = atlasFormat;
        return true;
    }

    void ShadowCache::releaseLayer( IRHIDevice* pDevice )
    {
        _layerPool.release( pDevice );
        _layerResolution = 0;
        invalidate();
    }

    TransientAttachmentPool::Attachment ShadowCache::getLayer() const
    {
        return _layerPool.find( hashed_string( kCacheLayerName ) );
    }
} // namespace sw
