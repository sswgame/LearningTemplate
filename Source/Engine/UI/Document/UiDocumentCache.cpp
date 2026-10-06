#include "pch.h"

#include "Engine/UI/Document/UiDocumentCache.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Document/UiDocument.h"
#include "Engine/UI/Document/UiDocumentLoader.h"

namespace sw
{
    SW_LOG_CALLER( "UiDocumentCache" );

    UiDocumentCache::UiDocumentCache()
        : IAssetCache{}
        , _mapPathToDocument{}
        , _mapPathToMemoryText{}
        , _parseCount{ 0 }
    {
    }

    UiDocumentCache::~UiDocumentCache() = default;

    shared_ptr<const UiDocumentAsset> UiDocumentCache::findOrLoad( string_view path, string& outError )
    {
        const string key  = FileUtil::normalizePath( path );
        const auto   iter = _mapPathToDocument.find( key );
        if ( iter != _mapPathToDocument.end() )
            return iter->second;

        string text;
        if ( readDocumentText( key, path, text ) == false )
        {
            outError = string( path ) + ": UI document not found";
            return {};
        }
        ++_parseCount;
        shared_ptr<UiDocumentAsset> document = make_shared<UiDocumentAsset>();
        if ( UiDocumentLoader::parse( text, path, *document, outError ) == false )
            return {};
        _mapPathToDocument[key] = document;
        return document;
    }

    void UiDocumentCache::registerMemoryDocument( string_view path, string_view text )
    {
        const string key          = FileUtil::normalizePath( path );
        _mapPathToMemoryText[key] = string( text );
        _mapPathToDocument.erase( key );
    }

    bool UiDocumentCache::isCached( string_view relativePath ) const
    {
        return _mapPathToDocument.find( FileUtil::normalizePath( relativePath ) ) != _mapPathToDocument.end();
    }

    void UiDocumentCache::reload( string_view relativePath, IRHIDevice* pDevice )
    {
        (void)pDevice;
        _mapPathToDocument.erase( FileUtil::normalizePath( relativePath ) );
    }

    void UiDocumentCache::clear()
    {
        _mapPathToDocument.clear();
        _mapPathToMemoryText.clear();
    }

    bool UiDocumentCache::readDocumentText( const string& key, string_view path, string& outText ) const
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
