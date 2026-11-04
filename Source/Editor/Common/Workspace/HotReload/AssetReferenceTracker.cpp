#include "pch.h"

#include "Editor/Common/Workspace/HotReload/AssetReferenceTracker.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    AssetReferenceTracker::AssetReferenceTracker()
        : _index{}
        , _job{}
        , _scanSerial{ 0 }
        , _bScanRequested{ SW_FALSE }
        , _bScanInFlight{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    void AssetReferenceTracker::update( uint64 contentChangeSerial, AssetHotReload& changeSource )
    {
        EditorReferenceIndexData data{};
        const EditorTaskPoll     poll = _job.take( data );
        if ( poll == EditorTaskPoll::Ready )
        {
            SW_LOG_INFO_ID( "AssetReferenceTracker.ReferenceIndexReady", "Reference index ready: %# text asset(s), %# reference(s) in %# ms", data._scannedFileCount, data._listReference.size(), data._scanMilliseconds );
            _index.assign( std::move( data ) );
            _bScanInFlight = SW_FALSE;
        }
        // 스캔하는 동안 바뀐 것은 스캔이 끝난 뒤 번호가 달라지므로 다시 스캔한다. 저장이 몰려도 스캔은 하나씩 실행한다.
        if ( poll == EditorTaskPoll::Running )
            return;
        if ( _bScanRequested == SW_TRUE && contentChangeSerial == _scanSerial )
            return;
        // 색인이 있고 스캔이 돌지 않으면 바뀐 파일만 다시 읽어 본다. 목록은 여기서만 꺼낸다 — 스캔이 도는 동안은 쌓아 두었다가 끝난 뒤 쓴다.
        if ( _index.isReady() && _bScanInFlight == SW_FALSE )
        {
            vector<string> listChangedPath;
            if ( changeSource.takeChangedPaths( listChangedPath ) && tryRefreshChangedFiles( listChangedPath ) )
            {
                _scanSerial = contentChangeSerial;
                return;
            }
        }
        requestScan( contentChangeSerial, changeSource );
    }

    bool AssetReferenceTracker::tryRefreshChangedFiles( const vector<string>& listChangedPath )
    {
        const string& resourceRoot = editor::getSubsystem<AssetPaths>().getRootFolderPath();
        for ( const string& assetID : listChangedPath )
        {
            const bool bExists = FileUtil::exists( FileUtil::joinPath( resourceRoot, assetID ) );
            if ( bExists != _index.containsFile( assetID ) )
                return false; // 생기거나 사라진 파일 — 다른 파일의 참조 판정이 바뀐다
        }
        for ( const string& assetID : listChangedPath )
        {
            _index.refreshFile( resourceRoot, assetID );
        }
        return true;
    }

    void AssetReferenceTracker::refreshFile( string_view assetID )
    {
        if ( _index.isReady() )
            _index.refreshFile( editor::getSubsystem<AssetPaths>().getRootFolderPath(), assetID );
    }

    void AssetReferenceTracker::restartPendingScan( uint64 contentChangeSerial, AssetHotReload& changeSource )
    {
        // 대기 여부를 따로 읽지 않고 꺼내 본다 — 막 끝난 낡은 결과는 여기서 버려지고, 아직 도는 스캔은 새 요청이 그 세대를 버린다.
        EditorReferenceIndexData staleData{};
        if ( _job.take( staleData ) != EditorTaskPoll::Idle )
            requestScan( contentChangeSerial, changeSource );
    }

    bool AssetReferenceTracker::isCurrent( uint64 contentChangeSerial ) const
    {
        const bool bNoScanSinceChange = _bScanInFlight == SW_FALSE && _scanSerial == contentChangeSerial;
        return _index.isReady() && bNoScanSinceChange;
    }

    void AssetReferenceTracker::requestScan( uint64 contentChangeSerial, AssetHotReload& changeSource )
    {
        _scanSerial     = contentChangeSerial;
        _bScanRequested = SW_TRUE;
        _bScanInFlight  = SW_TRUE;
        // 이 스캔이 지금까지의 변경을 모두 담는다 — 그 뒤에 오는 변경만 기억한다.
        changeSource.discardChangedPaths();
        // 워커는 에디터 범위를 찾지 않는다 — 여기서 찾아 넘긴다. 작업 관리자는 엔진 기동이 늘 올리므로 없으면 버그다.
        _job.request( editor::getSubsystem<AssetPaths>().getRootFolderPath(), &editor::getSubsystem<TaskManager>() );
    }
} // namespace sw::editor
