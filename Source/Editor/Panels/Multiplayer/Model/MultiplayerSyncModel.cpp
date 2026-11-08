#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerSyncModel.h"

#include "Core/Log/Logger.h"
#include "Core/String/formatString.h"

namespace sw::editor
{
    MultiplayerSyncModel::MultiplayerSyncModel()
        : _arrPendingProcess{ -1, -1 }
        , _arrPendingReportCount{}
        , _arrObjectPendingProcess{ -1, -1 }
        , _arrObjectPendingCount{}
        , _arrObjectTick{}
        , _arrObjectList{}
        , _arrPendingWatch{}
        , _objectWatch{}
        , _errorText{}
        , _bObjectRequest{ SW_FALSE }
    {
        static_assert( EditorStateHashCompare::kSourceCount == 2, "the pending client tables are initialized for two sources" );
    }

    bool MultiplayerSyncModel::requestClientHistory( IEditorNetPlayProcesses& processes, const EditorStateHashCompare& compare, int32 sourceIndex, int32 processIndex )
    {
        const EditorNetPlayProcessStatus status  = processes.getProcessStatus( processIndex );
        const string&                    channel = compare.getHashChannel();
        const string                     command = formatToString( "state.hashline %#%#", kClientHistoryTicks, ( channel == StateHashHistory::kSceneChannelName ? string{} : " " + channel ) );
        if ( processes.sendProcessCommand( processIndex, command ) == false )
        {
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotAsk", "Sync could not ask %# for its history (the process has exited)", status._label );
            return false;
        }
        _arrPendingProcess[sourceIndex]     = processIndex;
        _arrPendingReportCount[sourceIndex] = status._stateHashReportCount;
        _arrPendingWatch[sourceIndex].restart();
        _errorText.clear();
        return true;
    }

    void MultiplayerSyncModel::pollHistory( const IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds )
    {
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = _arrPendingProcess[sourceIndex];
            if ( processIndex < 0 )
                continue;
            if ( processes.isRunning() == false )
            {
                _arrPendingProcess[sourceIndex] = -1; // 세션이 끝났다 — 답이 올 곳이 없다
                continue;
            }
            const EditorNetPlayProcessStatus status = processes.getProcessStatus( processIndex );
            if ( status._stateHashReportCount == _arrPendingReportCount[sourceIndex] )
            {
                // 클라이언트는 잘못된 질문에 오류 줄로 답하고 그 답은 이력 답 수를 늘리지 않는다 - 상한 없이 기다리면 영원히 대기 상태로 남는다.
                if ( _arrPendingWatch[sourceIndex].getElapsedMilliseconds() >= timeoutMilliseconds )
                {
                    _arrPendingProcess[sourceIndex] = -1;
                    _errorText                      = formatToString( "Timed out waiting for %#'s history (no valid answer within %# ms)", status._label, timeoutMilliseconds );
                    SW_LOG_WARNING_ID( "MultiplayerSync.Sync", "Sync %#", _errorText );
                }
                continue;
            }
            _arrPendingProcess[sourceIndex] = -1;
            string report;
            StateHashHistory::writeReport( status._listStateHash, report );
            if ( compare.loadFromReport( sourceIndex, status._label, report ) == false )
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotTake", "Sync could not take %#'s history: %#", status._label, compare.getLastError() );
            else
                compare.setClientOrigin( sourceIndex, processIndex );
        }
    }

    void MultiplayerSyncModel::startObjectCompare( IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds )
    {
        _bObjectRequest    = SW_FALSE;
        uint64 channelTick = 0;
        // 채널(서버) 칸을 먼저 읽어 그 틱을 안다 — 클라이언트에는 그 틱 이상의 답만 받는다(멈춘 서버를 클라이언트가 아직 따라오는 중일 수 있다).
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            _arrObjectPendingProcess[sourceIndex] = -1;
            if ( compare.getClientOrigin( sourceIndex ) >= 0 )
                continue;
            if ( compare.readObjectsFromChannel( sourceIndex, _arrObjectTick[sourceIndex], _arrObjectList[sourceIndex] ) == false )
            {
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotCompare", "Sync could not compare objects: %#", compare.getLastError() );
                return;
            }
            channelTick = _arrObjectTick[sourceIndex] > channelTick ? _arrObjectTick[sourceIndex] : channelTick;
        }
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = compare.getClientOrigin( sourceIndex );
            if ( processIndex < 0 )
                continue;
            // 클라이언트는 디버그 채널이 없다 - 표준 입력의 한 줄 답(`state.objectline`)을 기다린다.
            const uint32 reportCount = processes.isRunning() ? processes.getProcessStatus( processIndex )._objectHashReportCount : 0;
            if ( processes.isRunning() == false || processes.requestObjectHashes( processIndex, channelTick ) == false )
            {
                SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotAsk2", "Sync could not ask slot %# for its object hashes (the session or the process is gone)", sourceIndex );
                return;
            }
            _arrObjectPendingProcess[sourceIndex] = processIndex;
            _arrObjectPendingCount[sourceIndex]   = reportCount;
        }
        _bObjectRequest = SW_TRUE;
        _errorText.clear();
        _objectWatch.restart();
        pollObjects( processes, compare, timeoutMilliseconds ); // 채널 칸뿐이면 바로 견준다
    }

    void MultiplayerSyncModel::pollObjects( const IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds )
    {
        if ( _bObjectRequest == SW_FALSE )
            return;
        bool bAllIn = true;
        for ( int32 sourceIndex = 0; sourceIndex < EditorStateHashCompare::kSourceCount; ++sourceIndex )
        {
            const int32 processIndex = _arrObjectPendingProcess[sourceIndex];
            if ( processIndex < 0 )
                continue;
            if ( processes.isRunning() == false )
            {
                _bObjectRequest = SW_FALSE; // 세션이 끝났다 - 답이 올 곳이 없다
                return;
            }
            const EditorNetPlayProcessStatus status = processes.getProcessStatus( processIndex );
            if ( status._objectHashReportCount == _arrObjectPendingCount[sourceIndex] )
            {
                bAllIn = false;
                continue;
            }
            _arrObjectTick[sourceIndex]           = status._objectHashTick;
            _arrObjectList[sourceIndex]           = status._listObjectHash;
            _arrObjectPendingProcess[sourceIndex] = -1;
        }
        if ( bAllIn == false )
        {
            if ( _objectWatch.getElapsedMilliseconds() >= timeoutMilliseconds )
            {
                _bObjectRequest = SW_FALSE;
                _errorText      = formatToString( "Timed out waiting for the object hashes (no valid answer within %# ms)", timeoutMilliseconds );
                SW_LOG_WARNING_ID( "MultiplayerSync.Sync", "Sync %#", _errorText );
            }
            return;
        }
        _bObjectRequest = SW_FALSE;
        if ( compare.compareObjects( _arrObjectTick[0], _arrObjectList[0], _arrObjectTick[1], _arrObjectList[1] ) == false )
            SW_LOG_WARNING_ID( "MultiplayerSync.SyncCouldNotCompare", "Sync could not compare objects: %#", compare.getLastError() );
    }

    void MultiplayerSyncModel::poll( IEditorNetPlayProcesses& processes, EditorStateHashCompare& compare, int64 timeoutMilliseconds )
    {
        pollHistory( processes, compare, timeoutMilliseconds );
        pollObjects( processes, compare, timeoutMilliseconds );
    }
} // namespace sw::editor
