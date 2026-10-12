#include "pch.h"

#include "Editor/Panels/ContentBrowser/ContentBrowserReferenceSearch.h"

#include "Core/Common/StdHeaders.h"

#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserReferenceSearch" );

    ContentBrowserReferenceSearch::ContentBrowserReferenceSearch()
        : _index{}
        , _job{}
        , _queryID{}
        , _listResult{}
        , _scanSerial{ 0 }
        , _bScanRequested{ SW_FALSE }
        , _bDependencyQuery{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void ContentBrowserReferenceSearch::takeFinishedScan()
    {
        EditorReferenceIndexData data{};
        if ( _job.take( data ) == false )
            return;
        SW_LOG_INFO( "Reference index ready: %# text asset(s), %# reference(s) in %# ms", data._scannedFileCount, data._listReference.size(), data._scanMilliseconds );
        _index.assign( std::move( data ) );
    }

    void ContentBrowserReferenceSearch::requestScanIfStale( uint64 contentChangeSerial )
    {
        // 스캔하는 동안 바뀐 것은 스캔이 끝난 뒤 번호가 달라지므로 다시 스캔한다. 저장이 몰려도 스캔은 하나씩 실행한다.
        if ( _job.isPending() )
            return;
        if ( _bScanRequested == SW_TRUE && contentChangeSerial == _scanSerial )
            return;
        _scanSerial     = contentChangeSerial;
        _bScanRequested = SW_TRUE;
        _job.request( ResourcePaths::getRootFolderPath() );
    }

    void ContentBrowserReferenceSearch::discardPendingScan()
    {
        if ( _job.isPending() )
            _job.request( ResourcePaths::getRootFolderPath() );
    }

    void ContentBrowserReferenceSearch::query( string_view absolutePath, bool bDependencies )
    {
        _queryID          = ResourcePaths::toResourceID( absolutePath );
        _bDependencyQuery = bDependencies ? SW_TRUE : SW_FALSE;
        if ( bDependencies )
            _index.findDependencies( _queryID, _listResult );
        else
            _index.findReferrers( _queryID, _listResult );
    }
} // namespace sw::editor
