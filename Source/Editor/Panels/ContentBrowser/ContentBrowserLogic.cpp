#include "pch.h"

#include "Editor/Panels/ContentBrowser/ContentBrowserLogic.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserLogicInternal
        {
            /** @brief 즐겨찾기 묶음 조각의 이름입니다(누를 수 없다). */
            static constexpr const utf8* kFavoritesLabel = "Favorites";

            /** @brief 구분자를 `/` 로 맞추고 끝 슬래시를 뗀 경로입니다(대소문자는 그대로 — 탐색은 실제 이름을 쓴다). */
            static string makeFolderPath( string_view path ) { return FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( path ) ); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void ContentBrowserLogic::makeFavoriteTrail( string_view label, string_view absolutePath, vector<ContentBrowserCrumb>& outListCrumb )
    {
        outListCrumb.clear();
        outListCrumb.push_back( ContentBrowserCrumb{ string{ ContentBrowserLogicInternal::kFavoritesLabel }, string{} } );
        outListCrumb.push_back( ContentBrowserCrumb{ string{ label }, ContentBrowserLogicInternal::makeFolderPath( absolutePath ) } );
    }

    void ContentBrowserLogic::makeFolderTrail( string_view rootLabel, string_view rootAbs, string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb )
    {
        outListCrumb.clear();
        const string root       = ContentBrowserLogicInternal::makeFolderPath( rootAbs );
        const string folder     = ContentBrowserLogicInternal::makeFolderPath( folderAbs );
        const bool   bUnderRoot = root.empty() == false && FileUtil::startsWithPathComponent( FileUtil::normalizePath( folder ), FileUtil::normalizePath( root ) );
        if ( bUnderRoot == false )
        {
            outListCrumb.push_back( ContentBrowserCrumb{ FileUtil::getFileNamePart( folder ), folder } );
            return;
        }

        outListCrumb.push_back( ContentBrowserCrumb{ string{ rootLabel }, root } );
        // 루트 뒤의 폴더마다 한 조각 — 조각의 경로는 원래 경로의 그 자리까지다(대소문자 · 구분자 그대로).
        const string_view folderView{ folder };
        size_t            start = root.size() + 1;
        while ( start < folderView.size() )
        {
            size_t slash = folderView.find( '/', start );
            if ( slash == string_view::npos )
                slash = folderView.size();
            outListCrumb.push_back( ContentBrowserCrumb{ string{ folderView.substr( start, slash - start ) }, string{ folderView.substr( 0, slash ) } } );
            start = slash + 1;
        }
    }

    void ContentBrowserLogic::appendChildCrumb( string_view childAbs, vector<ContentBrowserCrumb>& inoutListCrumb )
    {
        const string child = ContentBrowserLogicInternal::makeFolderPath( childAbs );
        inoutListCrumb.push_back( ContentBrowserCrumb{ FileUtil::getFileNamePart( child ), child } );
    }
} // namespace sw::editor

namespace sw::editor
{
    ContentBrowserFolderCache::ContentBrowserFolderCache( ContentBrowserFolderScanFunc pfnScan )
        : _mapChildFolder{}
        , _pfnScan{ pfnScan }
    {
    }

    const vector<string>& ContentBrowserFolderCache::getOrScanChildFolders( string_view folderAbs )
    {
        const string key = FileUtil::normalizePath( FileUtil::trimTrailingSlashes( folderAbs ) );
        const auto   it  = _mapChildFolder.find( key );
        if ( it != _mapChildFolder.end() )
            return *it->second;

        unique_ptr<vector<string>> pListChild = make_unique<vector<string>>();
        if ( _pfnScan != nullptr )
            _pfnScan( folderAbs, *pListChild );
        std::sort( pListChild->begin(), pListChild->end() );
        const vector<string>& listChild = *pListChild;
        _mapChildFolder.emplace( key, std::move( pListChild ) );
        return listChild;
    }
} // namespace sw::editor

namespace sw::editor
{
    ContentBrowserHistory::ContentBrowserHistory()
        : _listEntry{}
        , _currentIndex{ -1 }
    {
    }

    void ContentBrowserHistory::record( string_view folderAbs, const vector<ContentBrowserCrumb>& listCrumb )
    {
        const bool bSameAsCurrent = 0 <= _currentIndex && _currentIndex < static_cast<int32>( _listEntry.size() ) &&
                                    FileUtil::pathsEqualNormalized( _listEntry[static_cast<size_t>( _currentIndex )]._folderPathAbs, folderAbs );
        if ( bSameAsCurrent )
            return;

        if ( canNavigateForward() )
            _listEntry.erase( _listEntry.begin() + ( _currentIndex + 1 ), _listEntry.end() );
        ContentBrowserHistoryEntry newEntry;
        newEntry._folderPathAbs = string{ folderAbs };
        newEntry._listCrumb     = listCrumb;
        _listEntry.push_back( std::move( newEntry ) );

        if ( _listEntry.size() > kMaxEntryCount )
        {
            const size_t dropCount = _listEntry.size() - kMaxEntryCount;
            _listEntry.erase( _listEntry.begin(), _listEntry.begin() + static_cast<std::ptrdiff_t>( dropCount ) );
        }
        _currentIndex = static_cast<int32>( _listEntry.size() ) - 1;
    }

    const ContentBrowserHistoryEntry* ContentBrowserHistory::stepBack()
    {
        if ( canNavigateBack() == false )
            return nullptr;
        --_currentIndex;
        return &_listEntry[static_cast<size_t>( _currentIndex )];
    }

    const ContentBrowserHistoryEntry* ContentBrowserHistory::stepForward()
    {
        if ( canNavigateForward() == false )
            return nullptr;
        ++_currentIndex;
        return &_listEntry[static_cast<size_t>( _currentIndex )];
    }
} // namespace sw::editor
