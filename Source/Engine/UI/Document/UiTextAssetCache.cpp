#include "pch.h"

#include "Engine/UI/Document/UiTextAssetCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    SW_LOG_CALLER( "UiTextAssetCache" );

    UiTextAssetCache::UiTextAssetCache( const utf8* pAssetKindName, const utf8* pAssetLabel, UiTextAssetParseFunction pParse )
        : IAssetCache{}
        , _mapPathToAsset{}
        , _mapPathToMemoryText{}
        , _reloadedHandler{}
        , _pAssetKindName{ pAssetKindName }
        , _pAssetLabel{ pAssetLabel }
        , _pParse{ pParse }
        , _parseCount{ 0 }
    {
    }

    UiTextAssetCache::~UiTextAssetCache() = default;

    shared_ptr<const void> UiTextAssetCache::findOrLoadAsset( string_view path, string& outError )
    {
        const string key  = FileUtil::normalizePath( path );
        const auto   iter = _mapPathToAsset.find( key );
        if ( iter != _mapPathToAsset.end() )
            return iter->second;

        string text;
        if ( readText( key, path, text ) == false )
        {
            outError = string( path ) + ": " + _pAssetLabel + " not found";
            return {};
        }
        ++_parseCount;
        shared_ptr<const void> asset = _pParse( text, path, outError );
        if ( asset == nullptr )
            return {};
        _mapPathToAsset[key] = asset;
        return asset;
    }

    void UiTextAssetCache::registerMemoryText( string_view path, string_view text )
    {
        const string key          = FileUtil::normalizePath( path );
        _mapPathToMemoryText[key] = string( text );
        _mapPathToAsset.erase( key );
    }

    bool UiTextAssetCache::isCached( string_view relativePath ) const
    {
        return _mapPathToAsset.find( FileUtil::normalizePath( relativePath ) ) != _mapPathToAsset.end();
    }

    void UiTextAssetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        const string key = FileUtil::normalizePath( relativePath );
        string       text;
        if ( readText( key, relativePath, text ) == false )
        {
            SW_LOG_ERROR( "[Ui] %# reload failed - not found, keeping the old one: %#", _pAssetLabel, relativePath );
            return;
        }
        ++_parseCount;
        string                       error;
        const shared_ptr<const void> asset = _pParse( text, relativePath, error );
        if ( asset == nullptr )
        {
            SW_LOG_ERROR( "[Ui] %# reload failed, keeping the old one: %#", _pAssetLabel, error.c_str() );
            return;
        }
        _mapPathToAsset[key] = asset;
        if ( _reloadedHandler.isBound() )
            _reloadedHandler( key );
    }

    void UiTextAssetCache::clear()
    {
        _mapPathToAsset.clear();
        _mapPathToMemoryText.clear();
    }

    bool UiTextAssetCache::readText( const string& key, string_view path, string& outText ) const
    {
        const auto memory = _mapPathToMemoryText.find( key );
        if ( memory != _mapPathToMemoryText.end() )
        {
            outText = memory->second;
            return true;
        }
        if ( ResourceUtil::readTextResource( path, outText ) )
            return true;
        return FileUtil::isRegularFile( path ) && FileUtil::readTextFile( path, outText );
    }
} // namespace sw
