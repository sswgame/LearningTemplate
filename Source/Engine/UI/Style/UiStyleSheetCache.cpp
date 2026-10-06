#include "pch.h"

#include "Engine/UI/Style/UiStyleSheetCache.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/UI/Style/UiStyleSheet.h"

namespace sw
{
    UiStyleSheetCache::UiStyleSheetCache()
        : IAssetCache{}
        , _mapPathToSheet{}
        , _mapPathToMemoryText{}
    {
    }

    UiStyleSheetCache::~UiStyleSheetCache() = default;

    shared_ptr<const UiStyleSheetAsset> UiStyleSheetCache::findOrLoad( string_view path, string& outError )
    {
        const string key  = FileUtil::normalizePath( path );
        const auto   iter = _mapPathToSheet.find( key );
        if ( iter != _mapPathToSheet.end() )
            return iter->second;

        string     text;
        const auto memory = _mapPathToMemoryText.find( key );
        if ( memory != _mapPathToMemoryText.end() )
            text = memory->second;
        else if ( ResourceUtil::readTextResource( path, text ) == false && ( FileUtil::isRegularFile( path ) == false || FileUtil::readTextFile( path, text ) == false ) )
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
        _mapPathToSheet.erase( FileUtil::normalizePath( relativePath ) );
    }

    void UiStyleSheetCache::clear()
    {
        _mapPathToSheet.clear();
        _mapPathToMemoryText.clear();
    }
} // namespace sw
