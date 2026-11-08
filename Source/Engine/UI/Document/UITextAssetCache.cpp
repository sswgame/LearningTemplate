#include "pch.h"

#include "Engine/UI/Document/UITextAssetCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"

namespace sw
{
    UITextAssetCache::UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse )
        : AssetCacheBase{ CacheTracking::counted( pAssetKindName ) } // 캐시는 엔진 범위마다 있어(엔진을 다시 세우면 새 캐시) 같은 이름의 칸을 이어 쓴다
        , _mapPathToAsset{}
        , _mapPathToMemoryText{}
        , _onReloaded{}
        , _pAssetKindName{ pAssetKindName }
        , _pAssetLabel{ pAssetLabel }
        , _pParse{ pParse }
        , _parseCount{ 0 }
    {
    }

    UITextAssetCache::~UITextAssetCache() = default;

    expected<shared_ptr<const void>> UITextAssetCache::findOrLoadAsset( string_view path )
    {
        const string key  = FileUtil::normalizePath( path );
        const auto   iter = _mapPathToAsset.find( key );
        if ( iter != _mapPathToAsset.end() )
        {
            getCacheStats().recordHit();
            return iter->second;
        }

        getCacheStats().recordMiss();
        string text;
        if ( readText( key, AssetID{ path }, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the UI text asset was not found", path }.appendDetail( " (" ).appendDetail( _pAssetLabel ).appendDetail( ")" ) );
        ++_parseCount;
        expected<shared_ptr<const void>> asset = _pParse( text, path );
        if ( asset.has_value() )
            _mapPathToAsset[key] = *asset;
        return asset;
    }

    void UITextAssetCache::registerMemoryText( string_view path, string_view text )
    {
        const string key          = FileUtil::normalizePath( path );
        _mapPathToMemoryText[key] = string( text );
        _mapPathToAsset.erase( key );
    }

    bool UITextAssetCache::isCached( string_view relativePath ) const
    {
        return _mapPathToAsset.find( FileUtil::normalizePath( relativePath ) ) != _mapPathToAsset.end();
    }

    void UITextAssetCache::clear()
    {
        _mapPathToAsset.clear();
        _mapPathToMemoryText.clear();
        getCacheStats().recordClear();
    }

    bool UITextAssetCache::readText( const string& key, const AssetID& assetID, string& outText ) const
    {
        const auto memory = _mapPathToMemoryText.find( key );
        if ( memory != _mapPathToMemoryText.end() )
        {
            outText = memory->second;
            return true;
        }
        // 낱개 파일 허용 정책은 `AssetIO` 한 곳이 본다(OS 절대 경로 · 작업 경로도 거기서 읽는다) — 실패 뒤 디스크를 다시 읽지 않는다.
        return AssetIO::readTextAsset( assetID, outText );
    }

    void UITextAssetCache::reload( const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        const string      key          = FileUtil::normalizePath( relativePath );
        string            text;
        if ( readText( key, assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "UITextAssetCache.ReloadFailedNotFound", "[UI] %# reload failed - not found, keeping the old one: %#", _pAssetLabel, relativePath );
            return;
        }
        ++_parseCount;
        const expected<shared_ptr<const void>> asset = _pParse( text, relativePath );
        if ( asset.has_value() == false )
        {
            SW_LOG_ERROR_ID( "UITextAssetCache.ReloadFailedKeepingOld", "[UI] %# reload failed, keeping the old one: %# (%#)", _pAssetLabel, asset.error().message(), asset.error().detail() );
            return;
        }
        _mapPathToAsset[key] = *asset;
        _onReloaded.broadcast( key );
    }
} // namespace sw
