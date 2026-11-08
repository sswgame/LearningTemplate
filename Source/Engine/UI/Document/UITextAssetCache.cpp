#include "pch.h"

#include "Engine/UI/Document/UITextAssetCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    UITextAssetCache::UITextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UITextAssetParseFunction pParse )
        : AssetCacheBase{ CacheTracking::counted( pAssetKindName ) } // 캐시는 엔진 범위마다 있어(엔진을 다시 세우면 새 캐시) 같은 이름의 칸을 이어 쓴다
        , _mapKeyToEntry{}
        , _mapPathToMemoryText{}
        , _onReloaded{}
        , _pAssetKindName{ pAssetKindName }
        , _pAssetLabel{ pAssetLabel }
        , _pParse{ pParse }
        , _parseCount{ 0 }
    {
    }

    UITextAssetCache::~UITextAssetCache() = default;

    string UITextAssetCache::makeKey( const string& path, const ResourceTagSet& tagOverride )
    {
        string key = path;
        for ( const string& tag : tagOverride.getTags() )
        {
            key += '|'; // 경로에 들지 않는 글자 — 엔진 판(경로 그대로)과 겹치지 않는다
            key += tag;
        }
        return key;
    }

    expected<shared_ptr<const void>> UITextAssetCache::parseText( string_view text, string_view path, const ResourceTagSet& tagOverride )
    {
        ++_parseCount;
        // 리소스 id 로 읽는다 — 조건(`_when`) · 덮어쓰기 층 · 다시 읽기 기록이 이 경로를 쓴다. 변형은 바꿔 끼운 태그로 합치고 기록하지 않는다.
        return _pParse( text, path, ResourceContext::forDocument().withAsset( AssetID{ path } ).withTagOverride( tagOverride ) );
    }

    expected<shared_ptr<const void>> UITextAssetCache::findOrLoadAsset( string_view path, const ResourceTagSet& tagOverride )
    {
        const string normalized = FileUtil::normalizePath( path );
        const string key        = makeKey( normalized, tagOverride );
        const auto   iter       = _mapKeyToEntry.find( key );
        if ( iter != _mapKeyToEntry.end() )
        {
            getCacheStats().recordHit();
            return iter->second._asset;
        }

        getCacheStats().recordMiss();
        string text;
        if ( readText( normalized, AssetID{ path }, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the UI text asset was not found", path }.appendDetail( " (" ).appendDetail( _pAssetLabel ).appendDetail( ")" ) );
        expected<shared_ptr<const void>> asset = parseText( text, path, tagOverride );
        if ( asset.has_value() )
            _mapKeyToEntry[key] = Entry{ normalized, tagOverride, *asset };
        return asset;
    }

    void UITextAssetCache::registerMemoryText( string_view path, string_view text )
    {
        const string key          = FileUtil::normalizePath( path );
        _mapPathToMemoryText[key] = string( text );
        for ( auto iter = _mapKeyToEntry.begin(); iter != _mapKeyToEntry.end(); )
        {
            if ( iter->second._path == key )
                iter = _mapKeyToEntry.erase( iter );
            else
                ++iter;
        }
    }

    bool UITextAssetCache::isCached( string_view relativePath ) const
    {
        const string key = FileUtil::normalizePath( relativePath );
        for ( const auto& [entryKey, entry] : _mapKeyToEntry )
        {
            if ( entry._path == key )
                return true;
        }
        return false;
    }

    void UITextAssetCache::clear()
    {
        _mapKeyToEntry.clear();
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
        // 그 경로의 엔진 판과 변형을 모두 다시 읽는다(글은 하나다). 들고 있지 않으면 엔진 판을 읽어 둔다 — 예전 규칙 그대로다.
        vector<ResourceTagSet> listTagOverride;
        for ( const auto& [entryKey, entry] : _mapKeyToEntry )
        {
            if ( entry._path == key )
                listTagOverride.push_back( entry._tagOverride );
        }
        if ( listTagOverride.empty() )
            listTagOverride.emplace_back();
        bool bReplaced = false;
        for ( const ResourceTagSet& tagOverride : listTagOverride )
        {
            const expected<shared_ptr<const void>> asset = parseText( text, relativePath, tagOverride );
            if ( asset.has_value() == false )
            {
                SW_LOG_ERROR_ID( "UITextAssetCache.ReloadFailedKeepingOld", "[UI] %# reload failed, keeping the old one: %# (%#)", _pAssetLabel, asset.error().message(), asset.error().detail() );
                continue;
            }
            _mapKeyToEntry[makeKey( key, tagOverride )] = Entry{ key, tagOverride, *asset };
            bReplaced                                   = true;
        }
        if ( bReplaced )
            _onReloaded.broadcast( key );
    }

    bool UITextAssetCache::reloadComposed( string_view relativePath )
    {
        if ( isCached( relativePath ) == false )
            return false;
        reload( AssetID{ relativePath } );
        return true;
    }
} // namespace sw
