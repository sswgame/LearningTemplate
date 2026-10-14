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

    EditorTaskPoll ContentBrowserReferenceSearch::takeFinishedScan()
    {
        EditorReferenceIndexData data{};
        const EditorTaskPoll     poll = _job.take( data );
        if ( poll != EditorTaskPoll::Ready )
            return poll;
        SW_LOG_INFO( "Reference index ready: %# text asset(s), %# reference(s) in %# ms", data._scannedFileCount, data._listReference.size(), data._scanMilliseconds );
        _index.assign( std::move( data ) );
        return poll;
    }

    void ContentBrowserReferenceSearch::requestScanIfStale( uint64 contentChangeSerial, EditorTaskPoll poll )
    {
        // 스캔하는 동안 바뀐 것은 스캔이 끝난 뒤 번호가 달라지므로 다시 스캔한다. 저장이 몰려도 스캔은 하나씩 실행한다.
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( _bScanRequested == SW_TRUE && contentChangeSerial == _scanSerial )
            return;
        _scanSerial     = contentChangeSerial;
        _bScanRequested = SW_TRUE;
        _job.request( ResourcePaths::getRootFolderPath() );
    }

    void ContentBrowserReferenceSearch::discardPendingScan()
    {
        // 대기 여부를 따로 읽지 않고 꺼내 본다 — 막 끝난 낡은 결과는 여기서 버려지고, 아직 도는 스캔은 다시 요청해 그 세대를 버린다.
        EditorReferenceIndexData staleData{};
        if ( _job.take( staleData ) == EditorTaskPoll::Running )
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
