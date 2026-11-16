#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserCollectionStore.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorPaths.h"

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserCollectionStoreInternal
        {
            static constexpr utf8 kSeparator = '\t';

            /** @brief 탭으로 나눈 칸을 @p outListField 에 담습니다(빈 칸 포함). */
            static void splitProperties( string_view line, vector<string_view>& outListField )
            {
                outListField.clear();
                size_t start = 0;
                while ( true )
                {
                    const size_t end = line.find( kSeparator, start );
                    if ( end == string_view::npos )
                    {
                        outListField.push_back( line.substr( start ) );
                        return;
                    }
                    outListField.push_back( line.substr( start, end - start ) );
                    start = end + 1;
                }
            }

            /** @brief 칸 안의 탭 · 줄바꿈은 공백으로 바꿉니다(줄 형식이 깨지지 않게). */
            static string sanitize( string_view text )
            {
                string clean( text );
                for ( utf8& ch : clean )
                {
                    if ( ch == '\t' || ch == '\n' || ch == '\r' )
                        ch = ' ';
                }
                return clean;
            }
        };
    } // namespace

    ContentBrowserCollectionStore::ContentBrowserCollectionStore()
        : _listSavedSearch{}
        , _listCollection{}
    {
    }

    bool ContentBrowserCollectionStore::saveSearch( const ContentBrowserSavedSearch& search )
    {
        if ( search._name.empty() )
            return false;
        for ( ContentBrowserSavedSearch& existing : _listSavedSearch )
        {
            if ( existing._name == search._name && existing._bShared == search._bShared )
            {
                existing = search;
                return true;
            }
        }
        _listSavedSearch.push_back( search );
        return true;
    }

    bool ContentBrowserCollectionStore::removeSearch( string_view name, bool bShared )
    {
        const size_t sizeBefore = _listSavedSearch.size();
        _listSavedSearch.erase( std::remove_if( _listSavedSearch.begin(), _listSavedSearch.end(), [name, bShared]( const ContentBrowserSavedSearch& search )
        { return search._name == name && search._bShared == bShared; } ),
                                _listSavedSearch.end() );
        return _listSavedSearch.size() != sizeBefore;
    }

    bool ContentBrowserCollectionStore::createCollection( string_view name, bool bShared )
    {
        if ( name.empty() || findCollection( name, bShared ) != nullptr )
            return false;
        ContentBrowserCollection collection{};
        collection._name    = ContentBrowserCollectionStoreInternal::sanitize( name );
        collection._bShared = bShared;
        _listCollection.push_back( std::move( collection ) );
        return true;
    }

    bool ContentBrowserCollectionStore::removeCollection( string_view name, bool bShared )
    {
        const size_t sizeBefore = _listCollection.size();
        _listCollection.erase( std::remove_if( _listCollection.begin(), _listCollection.end(), [name, bShared]( const ContentBrowserCollection& collection )
        { return collection._name == name && collection._bShared == bShared; } ),
                               _listCollection.end() );
        return _listCollection.size() != sizeBefore;
    }

    bool ContentBrowserCollectionStore::addToCollection( string_view name, bool bShared, string_view assetID )
    {
        for ( ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection._name != name || collection._bShared != bShared )
                continue;
            for ( const string& existing : collection._listAssetID )
            {
                if ( existing == assetID )
                    return false;
            }
            collection._listAssetID.push_back( string( assetID ) );
            return true;
        }
        return false;
    }

    bool ContentBrowserCollectionStore::removeFromCollection( string_view name, bool bShared, string_view assetID )
    {
        for ( ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection._name != name || collection._bShared != bShared )
                continue;
            const size_t sizeBefore = collection._listAssetID.size();
            collection._listAssetID.erase( std::remove( collection._listAssetID.begin(), collection._listAssetID.end(), string( assetID ) ), collection._listAssetID.end() );
            return collection._listAssetID.size() != sizeBefore;
        }
        return false;
    }

    const ContentBrowserCollection* ContentBrowserCollectionStore::findCollection( string_view name, bool bShared ) const
    {
        for ( const ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection._name == name && collection._bShared == bShared )
                return &collection;
        }
        return nullptr;
    }

    string ContentBrowserCollectionStore::toText( bool bShared ) const
    {
        using Internal = ContentBrowserCollectionStoreInternal;
        string text;
        for ( const ContentBrowserSavedSearch& search : _listSavedSearch )
        {
            if ( search._bShared != bShared )
                continue;
            text += "search\t" + Internal::sanitize( search._name ) + "\t" + Internal::sanitize( search._typeFilterLabel ) + "\t" + Internal::sanitize( search._searchText ) + "\n";
        }
        for ( const ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection._bShared != bShared )
                continue;
            // 빈 컬렉션도 남도록 머리 줄을 하나 둔다(에셋 칸이 빈 줄)
            text += "collection\t" + Internal::sanitize( collection._name ) + "\t\n";
            for ( const string& assetID : collection._listAssetID )
                text += "collection\t" + Internal::sanitize( collection._name ) + "\t" + Internal::sanitize( assetID ) + "\n";
        }
        return text;
    }

    void ContentBrowserCollectionStore::fromText( string_view text, bool bShared )
    {
        _listSavedSearch.erase( std::remove_if( _listSavedSearch.begin(), _listSavedSearch.end(), [bShared]( const ContentBrowserSavedSearch& search )
        { return search._bShared == bShared; } ),
                                _listSavedSearch.end() );
        _listCollection.erase( std::remove_if( _listCollection.begin(), _listCollection.end(), [bShared]( const ContentBrowserCollection& collection )
        { return collection._bShared == bShared; } ),
                               _listCollection.end() );
        vector<string_view> listField;
        size_t              lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            if ( lineEnd == string_view::npos )
                lineEnd = text.size();
            string_view line = text.substr( lineStart, lineEnd - lineStart );
            lineStart        = lineEnd + 1;
            if ( line.empty() == false && line.back() == '\r' )
                line = line.substr( 0, line.size() - 1 );
            ContentBrowserCollectionStoreInternal::splitProperties( line, listField );
            if ( listField.size() == 4 && listField[0] == "search" && listField[1].empty() == false )
            {
                (void)saveSearch( ContentBrowserSavedSearch{ string( listField[1] ), string( listField[2] ), string( listField[3] ), bShared } );
                continue;
            }
            if ( listField.size() == 3 && listField[0] == "collection" && listField[1].empty() == false )
            {
                if ( findCollection( listField[1], bShared ) == nullptr )
                    (void)createCollection( listField[1], bShared );
                if ( listField[2].empty() == false )
                    (void)addToCollection( listField[1], bShared, listField[2] );
            }
        }
    }

    void ContentBrowserCollectionStore::loadFiles( string_view localFilePath, string_view sharedFilePath )
    {
        string text;
        if ( localFilePath.empty() == false && FileUtil::readOptionalTextFile( localFilePath, text ) == OptionalFileRead::Read )
            fromText( FileUtil::skipUTF8Bom( text ), false );
        text.clear();
        if ( sharedFilePath.empty() == false && FileUtil::readOptionalTextFile( sharedFilePath, text ) == OptionalFileRead::Read )
            fromText( FileUtil::skipUTF8Bom( text ), true );
    }

    bool ContentBrowserCollectionStore::saveFile( string_view filePath, bool bShared ) const
    {
        if ( filePath.empty() || FileUtil::ensureParentDirectoryExists( filePath ) == false || FileUtil::writeTextFile( filePath, toText( bShared ) ) == false )
        {
            SW_LOG_WARNING_ID( "ContentBrowserCollectionStore.NotSaved", "Content Browser collections could not be saved to %#", string( filePath ).c_str() );
            return false;
        }
        return true;
    }

    string ContentBrowserCollectionStore::getLocalFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kLocalFileName );
    }

    string ContentBrowserCollectionStore::getSharedFilePath()
    {
        return FileUtil::joinPath( ProcessRoots::get()._project.str(), kSharedFileName );
    }
} // namespace sw::editor
