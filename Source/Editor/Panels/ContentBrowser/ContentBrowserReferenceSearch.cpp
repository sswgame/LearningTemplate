#include "pch.h"

#include "Editor/Panels/ContentBrowser/ContentBrowserReferenceSearch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserReferenceSearch" );

    ContentBrowserReferenceSearch::ContentBrowserReferenceSearch()
        : _index{}
        , _job{}
        , _pChangeSource{ nullptr }
        , _queryID{}
        , _listResult{}
        , _scanSerial{ 0 }
        , _bScanRequested{ SW_FALSE }
        , _bScanInFlight{ SW_FALSE }
        , _bDependencyQuery{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void ContentBrowserReferenceSearch::update( uint64 contentChangeSerial, AssetHotReload* pChangeSource )
    {
        _pChangeSource = pChangeSource;
        EditorReferenceIndexData data{};
        const EditorTaskPoll     poll = _job.take( data );
        if ( poll == EditorTaskPoll::Ready )
        {
            SW_LOG_INFO( "Reference index ready: %# text asset(s), %# reference(s) in %# ms", data._scannedFileCount, data._listReference.size(), data._scanMilliseconds );
            _index.assign( std::move( data ) );
            _bScanInFlight = SW_FALSE;
        }
        // 스캔하는 동안 바뀐 것은 스캔이 끝난 뒤 번호가 달라지므로 다시 스캔한다. 저장이 몰려도 스캔은 하나씩 실행한다.
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( _bScanRequested == SW_TRUE && contentChangeSerial == _scanSerial )
            return;
        // 색인이 있고 스캔이 돌지 않으면 바뀐 파일만 다시 읽어 본다. 목록은 여기서만 꺼낸다 — 스캔이 도는 동안은 쌓아 두었다가 끝난 뒤 쓴다.
        const bool bCanRefreshIncrementally = _index.isReady() && _bScanInFlight == SW_FALSE;
        if ( bCanRefreshIncrementally && _pChangeSource != nullptr )
        {
            vector<string> listChangedPath;
            if ( _pChangeSource->takeChangedPaths( listChangedPath ) && tryRefreshChangedFiles( listChangedPath ) )
            {
                _scanSerial = contentChangeSerial;
                return;
            }
        }
        requestScan( contentChangeSerial );
    }

    bool ContentBrowserReferenceSearch::tryRefreshChangedFiles( const vector<string>& listChangedPath )
    {
        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        for ( const string& resourceID : listChangedPath )
        {
            const bool bExists = FileUtil::exists( FileUtil::joinPath( resourceRoot, resourceID ) );
            if ( bExists != _index.containsFile( resourceID ) )
                return false; // 생기거나 사라진 파일 — 다른 파일의 참조 판정이 바뀐다
        }
        for ( const string& resourceID : listChangedPath )
        {
            _index.refreshFile( resourceRoot, resourceID );
        }
        return true;
    }

    void ContentBrowserReferenceSearch::restartPendingScan( uint64 contentChangeSerial )
    {
        // 대기 여부를 따로 읽지 않고 꺼내 본다 — 막 끝난 낡은 결과는 여기서 버려지고, 아직 도는 스캔은 새 요청이 그 세대를 버린다.
        EditorReferenceIndexData staleData{};
        if ( _job.take( staleData ) != EditorTaskPoll::Idle )
            requestScan( contentChangeSerial );
    }

    bool ContentBrowserReferenceSearch::isCurrent( uint64 contentChangeSerial ) const
    {
        const bool bNoScanSinceChange = _bScanInFlight == SW_FALSE && _scanSerial == contentChangeSerial;
        return _index.isReady() && bNoScanSinceChange;
    }

    void ContentBrowserReferenceSearch::requestScan( uint64 contentChangeSerial )
    {
        _scanSerial     = contentChangeSerial;
        _bScanRequested = SW_TRUE;
        _bScanInFlight  = SW_TRUE;
        // 이 스캔이 지금까지의 변경을 모두 담는다 — 그 뒤에 오는 변경만 기억한다.
        if ( _pChangeSource != nullptr )
            _pChangeSource->discardChangedPaths();
        // 워커는 에디터 범위를 찾지 않는다 — 여기서 찾아 넘긴다. 작업 관리자는 엔진 기동이 늘 올리므로 없으면 버그다.
        _job.request( ResourcePaths::getRootFolderPath(), &editor::getSubsystem<TaskManager>() );
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
