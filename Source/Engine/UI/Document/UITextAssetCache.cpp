#include "pch.h"

#include "Engine/UI/Document/UITextAssetCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/FileSystem/AssetIO.h"

namespace sw
{
    SW_LOG_CALLER( "UITextAssetCache" );

    UITextAssetCache::UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse )
        : IAssetCache{ CacheTracking::counted( pAssetKindName ) } // 캐시는 UI 시스템마다 있어 같은 이름의 칸을 이어 쓴다
        , _mapPathToAsset{}
        , _mapPathToMemoryText{}
        , _reloadedHandler{}
        , _pAssetKindName{ pAssetKindName }
        , _pAssetLabel{ pAssetLabel }
        , _pParse{ pParse }
        , _parseCount{ 0 }
    {
    }

    UITextAssetCache::~UITextAssetCache() = default;

    shared_ptr<const void> UITextAssetCache::findOrLoadAsset( string_view path, string& outError )
    {
        shared_ptr<const void> asset;
        const string           key  = FileUtil::normalizePath( path );
        const auto             iter = _mapPathToAsset.find( key );
        if ( iter != _mapPathToAsset.end() )
        {
            asset = iter->second;
            getCacheStats().recordHit();
            return asset;
        }

        getCacheStats().recordMiss();
        string text;
        if ( readText( key, AssetID{ path }, text ) == false )
        {
            outError = string( path ) + ": " + _pAssetLabel + " not found";
            return asset;
        }
        ++_parseCount;
        asset = _pParse( text, path, outError );
        if ( asset != nullptr )
            _mapPathToAsset[key] = asset;
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
} // namespace sw
