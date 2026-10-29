/**
 * @file UITextAssetCacheReload.cpp
 * @brief UI 텍스트 에셋 캐시의 핫 리로드입니다(개발 전용 — 배포 구성은 `*Reload.cpp` 를 짓지 않고, 등록은 `SW_ASSET_CACHE_RELOAD_FUNCTION` 이 nullptr 이다).
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/UI/Document/UITextAssetCache.h"

namespace sw
{
    SW_LOG_CALLER( "UI" );

    void UITextAssetCache::reload( const AssetID& assetID, IRHIDevice* pDevice )
    {
        const string_view relativePath = assetID.view();
        (void)pDevice;
        const string key = FileUtil::normalizePath( relativePath );
        string       text;
        if ( readText( key, assetID, text ) == false )
        {
            SW_LOG_ERROR( "[UI] %# reload failed - not found, keeping the old one: %#", _pAssetLabel, relativePath );
            return;
        }
        ++_parseCount;
        string                       error;
        const shared_ptr<const void> asset = _pParse( text, relativePath, error );
        if ( asset == nullptr )
        {
            SW_LOG_ERROR( "[UI] %# reload failed, keeping the old one: %#", _pAssetLabel, error.c_str() );
            return;
        }
        _mapPathToAsset[key] = asset;
        _onReloaded.broadcast( key );
    }
} // namespace sw
