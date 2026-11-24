#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserCollectionStore.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

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

    bool ContentBrowserCollection::add( string_view assetID )
    {
        const string key{ assetID };
        if ( _uniqueAssetID.insert( key ).second == false )
            return false;
        _listAssetID.push_back( key );
        return true;
    }

    bool ContentBrowserCollection::remove( string_view assetID )
    {
        const string key{ assetID };
        if ( _uniqueAssetID.erase( key ) == 0 )
            return false;
        (void)VectorUtil::removeValue( _listAssetID, key ); // 색인에 있었으니 목록에도 있다
        return true;
    }

    ContentBrowserCollectionStore::ContentBrowserCollectionStore()
        : _listSavedSearch{}
        , _listCollection{}
        , _bLocalUnread{ SW_FALSE }
        , _bSharedUnread{ SW_FALSE }
        , _reserved{ 0 }
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
        _listCollection.push_back( ContentBrowserCollection{ ContentBrowserCollectionStoreInternal::sanitize( name ), bShared } );
        return true;
    }

    bool ContentBrowserCollectionStore::removeCollection( string_view name, bool bShared )
    {
        const size_t sizeBefore = _listCollection.size();
        _listCollection.erase( std::remove_if( _listCollection.begin(), _listCollection.end(), [name, bShared]( const ContentBrowserCollection& collection )
        { return collection.getName() == name && collection.isShared() == bShared; } ),
                               _listCollection.end() );
        return _listCollection.size() != sizeBefore;
    }

    bool ContentBrowserCollectionStore::addToCollection( string_view name, bool bShared, string_view assetID )
    {
        ContentBrowserCollection* const pCollection = findCollectionMutable( name, bShared );
        return pCollection != nullptr && pCollection->add( assetID );
    }

    bool ContentBrowserCollectionStore::removeFromCollection( string_view name, bool bShared, string_view assetID )
    {
        ContentBrowserCollection* const pCollection = findCollectionMutable( name, bShared );
        return pCollection != nullptr && pCollection->remove( assetID );
    }

    const ContentBrowserCollection* ContentBrowserCollectionStore::findCollection( string_view name, bool bShared ) const
    {
        for ( const ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection.getName() == name && collection.isShared() == bShared )
                return &collection;
        }
        return nullptr;
    }

    ContentBrowserCollection* ContentBrowserCollectionStore::findCollectionMutable( string_view name, bool bShared )
    {
        return const_cast<ContentBrowserCollection*>( findCollection( name, bShared ) );
    }

    string ContentBrowserCollectionStore::toText( bool bShared ) const
    {
        using Internal = ContentBrowserCollectionStoreInternal;
        StringBuilder<constant::buffer::kMax4096> text;
        for ( const ContentBrowserSavedSearch& search : _listSavedSearch )
        {
            if ( search._bShared != bShared )
                continue;
            text.appendFormat( "search\t%#\t%#\t%#\n", Internal::sanitize( search._name ), Internal::sanitize( search._typeFilterLabel ), Internal::sanitize( search._searchText ) );
        }
        for ( const ContentBrowserCollection& collection : _listCollection )
        {
            if ( collection.isShared() != bShared )
                continue;
            // 빈 컬렉션도 남도록 머리 줄을 하나 둔다(에셋 칸이 빈 줄)
            const string name = Internal::sanitize( collection.getName() );
            text.appendFormat( "collection\t%#\t\n", name );
            for ( const string& assetID : collection.getAssetIDs() )
                text.appendFormat( "collection\t%#\t%#\n", name, Internal::sanitize( assetID ) );
        }
        return string( text.view() );
    }

    bool ContentBrowserCollectionStore::fromText( string_view text, bool bShared )
    {
        _listSavedSearch.erase( std::remove_if( _listSavedSearch.begin(), _listSavedSearch.end(), [bShared]( const ContentBrowserSavedSearch& search )
        { return search._bShared == bShared; } ),
                                _listSavedSearch.end() );
        _listCollection.erase( std::remove_if( _listCollection.begin(), _listCollection.end(), [bShared]( const ContentBrowserCollection& collection )
        { return collection.isShared() == bShared; } ),
                               _listCollection.end() );
        vector<string_view> listField;
        bool                bAllKnown = true;
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
            if ( line.empty() )
                continue;
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
                continue;
            }
            bAllKnown = false;
        }
        return bAllKnown;
    }

    void ContentBrowserCollectionStore::loadFiles( const AbsolutePath& localFilePath, const AbsolutePath& sharedFilePath )
    {
        _bLocalUnread  = loadFile( localFilePath, false ) ? SW_FALSE : SW_TRUE;
        _bSharedUnread = loadFile( sharedFilePath, true ) ? SW_FALSE : SW_TRUE;
    }

    bool ContentBrowserCollectionStore::loadFile( const AbsolutePath& filePath, bool bShared )
    {
        if ( filePath.empty() )
            return true;
        string                 text;
        const OptionalFileRead read  = FileUtil::readOptionalTextFile( filePath, text );
        const bool             bRead = read == OptionalFileRead::Missing || ( read == OptionalFileRead::Read && fromText( FileUtil::skipUTF8Bom( text ), bShared ) );
        if ( bRead == false )
            SW_LOG_ERROR_ID( "ContentBrowserCollectionStore.NotRead", "Content Browser collections '%#' could not be read completely - it is not written back until it is fixed and the editor is reopened", string( filePath.str() ).c_str() );
        return bRead;
    }

    bool ContentBrowserCollectionStore::saveFile( const AbsolutePath& filePath, bool bShared ) const
    {
        if ( ( bShared ? _bSharedUnread : _bLocalUnread ) == SW_TRUE )
        {
            SW_LOG_ERROR_ID( "ContentBrowserCollectionStore.NotOverwritten", "Content Browser collections were not saved: '%#' was not read completely and writing would lose the rest of it", string( filePath.str() ).c_str() );
            return false;
        }
        if ( filePath.empty() || FileUtil::ensureParentDirectoryExists( filePath ) == false || FileUtil::writeTextFile( filePath, toText( bShared ) ) == false )
        {
            SW_LOG_WARNING_ID( "ContentBrowserCollectionStore.NotSaved", "Content Browser collections could not be saved to %#", string( filePath.str() ).c_str() );
            return false;
        }
        return true;
    }

    AbsolutePath ContentBrowserCollectionStore::getLocalFilePath()
    {
        return EditorPaths::resolveEditorStateFile( kLocalFileName );
    }

    AbsolutePath ContentBrowserCollectionStore::getSharedFilePath()
    {
        return ProcessRoots::get()._project.join( kSharedFileName );
    }
} // namespace sw::editor
