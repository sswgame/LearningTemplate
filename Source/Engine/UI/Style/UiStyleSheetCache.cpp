#include "pch.h"

#include "Engine/UI/Style/UiStyleSheetCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Style/UiStyleSheet.h"

namespace sw
{
    SW_LOG_CALLER( "UiStyleSheetCache" );

    UiStyleSheetCache::UiStyleSheetCache()
        : IAssetCache{}
        , _mapPathToSheet{}
        , _mapPathToMemoryText{}
        , _reloadedHandler{}
    {
    }

    UiStyleSheetCache::~UiStyleSheetCache() = default;

    shared_ptr<const UiStyleSheetAsset> UiStyleSheetCache::findOrLoad( string_view path, string& outError )
    {
        const string key  = FileUtil::normalizePath( path );
        const auto   iter = _mapPathToSheet.find( key );
        if ( iter != _mapPathToSheet.end() )
            return iter->second;

        string text;
        if ( readSheetText( key, path, text ) == false )
        {
            outError = string( path ) + ": UI style sheet not found";
            return {};
        }
        shared_ptr<UiStyleSheetAsset> sheet = make_shared<UiStyleSheetAsset>();
        if ( UiStyleSheetLoader::parse( text, path, *sheet, outError ) == false )
            return {};
        _mapPathToSheet[key] = sheet;
        return sheet;
    }

    void UiStyleSheetCache::registerMemorySheet( string_view path, string_view text )
    {
        const string key          = FileUtil::normalizePath( path );
        _mapPathToMemoryText[key] = string( text );
        _mapPathToSheet.erase( key );
    }

    bool UiStyleSheetCache::isCached( string_view relativePath ) const
    {
        return _mapPathToSheet.find( FileUtil::normalizePath( relativePath ) ) != _mapPathToSheet.end();
    }

    void UiStyleSheetCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        const string key = FileUtil::normalizePath( relativePath );
        string       text;
        if ( readSheetText( key, relativePath, text ) == false )
        {
            SW_LOG_ERROR( "[Ui] UI style sheet reload failed - not found, keeping the old one: %#", relativePath );
            return;
        }
        shared_ptr<UiStyleSheetAsset> sheet = make_shared<UiStyleSheetAsset>();
        string                        error;
        if ( UiStyleSheetLoader::parse( text, relativePath, *sheet, error ) == false )
        {
            SW_LOG_ERROR( "[Ui] UI style sheet reload failed, keeping the old one: %#", error.c_str() );
            return;
        }
        _mapPathToSheet[key] = sheet;
        if ( _reloadedHandler.isBound() )
            _reloadedHandler( key );
    }

    bool UiStyleSheetCache::readSheetText( const string& key, string_view path, string& outText ) const
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

    void UiStyleSheetCache::clear()
    {
        _mapPathToSheet.clear();
        _mapPathToMemoryText.clear();
    }
} // namespace sw
