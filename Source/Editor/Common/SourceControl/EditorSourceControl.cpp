#include "pch.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Task/EditorExternalToolJob.h"

namespace sw::editor
{
    EditorSourceControl::EditorSourceControl( string_view repositoryRoot )
        : _repositoryRoot{ FileUtil::normalizeSeparators( repositoryRoot ) }
        , _pProvider{ make_unique<NullSourceControlProvider>() }
        , _pJob{ make_unique<EditorExternalToolJob>() }
        , _listLock{}
        , _listPending{}
        , _running{}
        , _listChange{}
        , _detailPath{}
        , _listDetailLine{}
        , _lastError{}
        , _changeRevision{ 0 }
        , _detailOperation{ SourceControlOperation::None }
        , _bStatusKnown{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorSourceControl::requestStatus()
    {
        if ( _pProvider->canTrackChanges() == false )
            return false;
        enqueue( SourceControlOperation::Status, {} );
        return true;
    }

    bool EditorSourceControl::requestDiff( string_view repositoryPath )
    {
        if ( _pProvider->canTrackChanges() == false || repositoryPath.empty() )
            return false;
        enqueue( SourceControlOperation::Diff, repositoryPath );
        return true;
    }

    bool EditorSourceControl::requestHistory( string_view repositoryPath )
    {
        if ( _pProvider->canTrackChanges() == false || repositoryPath.empty() )
            return false;
        enqueue( SourceControlOperation::History, repositoryPath );
        return true;
    }

    bool EditorSourceControl::requestRevert( const SourceControlChange& change )
    {
        if ( _pProvider->canTrackChanges() == false || GitChangeCommands::canRevert( change ) == false )
            return false;
        enqueue( SourceControlOperation::Revert, change._path );
        // 이름 바꾸기는 옛 경로도 HEAD 로 되돌려야 원래 파일이 돌아온다
        if ( change._kind == SourceControlChangeKind::Renamed && change._originalPath.empty() == false )
            enqueue( SourceControlOperation::Revert, change._originalPath );
        return true;
    }

    EditorSourceControl::~EditorSourceControl() = default;

    void EditorSourceControl::initialize()
    {
        if ( _repositoryRoot.empty() == false )
            enqueue( SourceControlOperation::Probe, {} );
    }

    void EditorSourceControl::shutdownSubsystem()
    {
        _pJob->cancelAndWait();
        _running = PendingOperation{};
        _listPending.clear();
    }

    void EditorSourceControl::setProvider( unique_ptr<ISourceControlProvider> pProvider )
    {
        _pProvider = pProvider != nullptr ? std::move( pProvider ) : make_unique<NullSourceControlProvider>();
        _listLock.clear();
    }

    bool EditorSourceControl::isBusy() const
    {
        return _running._operation != SourceControlOperation::None || _listPending.empty() == false;
    }

    void EditorSourceControl::enqueue( SourceControlOperation operation, string_view repositoryPath )
    {
        for ( const PendingOperation& pending : _listPending )
        {
            if ( pending._operation == operation && pending._repositoryPath == repositoryPath )
                return;
        }
        PendingOperation pending;
        pending._operation      = operation;
        pending._repositoryPath = string{ repositoryPath };
        _listPending.push_back( std::move( pending ) );
    }

    void EditorSourceControl::requestRefresh()
    {
        if ( _pProvider->makeRefreshCommand()._executable.empty() == false )
            enqueue( SourceControlOperation::Refresh, {} );
    }

    bool EditorSourceControl::requestLock( string_view absolutePath )
    {
        const string repositoryPath = makeRepositoryPath( _repositoryRoot, absolutePath );
        if ( _pProvider->canLock() == false || repositoryPath.empty() )
            return false;
        enqueue( SourceControlOperation::Lock, repositoryPath );
        return true;
    }

    bool EditorSourceControl::requestUnlock( string_view absolutePath )
    {
        const string repositoryPath = makeRepositoryPath( _repositoryRoot, absolutePath );
        if ( _pProvider->canLock() == false || repositoryPath.empty() )
            return false;
        enqueue( SourceControlOperation::Unlock, repositoryPath );
        return true;
    }

    void EditorSourceControl::update()
    {
        EditorExternalToolResult result;
        if ( _running._operation != SourceControlOperation::None && _pJob->take( result ) == EditorTaskPoll::Ready )
        {
            const PendingOperation finished = std::move( _running );
            _running                        = PendingOperation{};
            applyResult( finished, result._exitCode, result._bLaunched, result._listLine );
        }
        if ( _running._operation != SourceControlOperation::None || _listPending.empty() )
            return;

        const PendingOperation next = _listPending.front();
        _listPending.erase( _listPending.begin() );
        ProcessCommand command;
        switch ( next._operation )
        {
            case SourceControlOperation::Probe:
            {
                command = GitLfsSourceControlProvider::makeProbeCommand();
                break;
            }
            case SourceControlOperation::Refresh:
            {
                command = _pProvider->makeRefreshCommand();
                break;
            }
            case SourceControlOperation::Lock:
            {
                command = _pProvider->makeLockCommand( next._repositoryPath );
                break;
            }
            case SourceControlOperation::Unlock:
            {
                command = _pProvider->makeUnlockCommand( next._repositoryPath );
                break;
            }
            case SourceControlOperation::ProbeGit:
            {
                command = GitSourceControlProvider::makeProbeCommand();
                break;
            }
            case SourceControlOperation::Status:
            {
                command = GitChangeCommands::makeStatusCommand();
                break;
            }
            case SourceControlOperation::Diff:
            {
                command = GitChangeCommands::makeDiffCommand( next._repositoryPath );
                break;
            }
            case SourceControlOperation::History:
            {
                command = GitChangeCommands::makeHistoryCommand( next._repositoryPath );
                break;
            }
            case SourceControlOperation::Revert:
            {
                command = GitChangeCommands::makeRevertCommand( next._repositoryPath );
                break;
            }
            case SourceControlOperation::None:
            {
                break;
            }
        }
        if ( command._executable.empty() )
            return;
        if ( _pJob->request( std::move( command ), _repositoryRoot ) )
            _running = next;
    }

    void EditorSourceControl::applyResult( const PendingOperation& operation, int32 exitCode, bool bLaunched, const vector<string>& listLine )
    {
        const string lastLine = listLine.empty() ? string{} : listLine.back();
        switch ( operation._operation )
        {
            case SourceControlOperation::Probe:
            {
                if ( bLaunched && exitCode == 0 )
                {
                    setProvider( make_unique<GitLfsSourceControlProvider>() );
                    SW_LOG_INFO_ID( "SourceControl.SourceControl", "Source control: %# (%#)", _pProvider->getName().data(), lastLine.c_str() );
                    requestRefresh();
                    (void)requestStatus();
                }
                else
                {
                    SW_LOG_INFO_ID( "SourceControl.SourceControlGitLfs", "Source control: git lfs is not available - locking is off, read-only files are still shown" );
                    enqueue( SourceControlOperation::ProbeGit, {} ); // 잠금은 없어도 변경 목록 · diff · 되돌리기는 git 하나로 된다
                }
                return;
            }
            case SourceControlOperation::ProbeGit:
            {
                if ( bLaunched && exitCode == 0 )
                {
                    setProvider( make_unique<GitSourceControlProvider>() );
                    SW_LOG_INFO_ID( "SourceControl.GitOnly", "Source control: %# (%#) - changes, diff and revert without locking", _pProvider->getName().data(), lastLine.c_str() );
                    (void)requestStatus();
                }
                else
                {
                    SW_LOG_INFO_ID( "SourceControl.NoGit", "Source control: git is not available - the Changes window stays empty" );
                }
                return;
            }
            case SourceControlOperation::Status:
            {
                vector<SourceControlChange> listChange;
                if ( exitCode == 0 && GitChangeCommands::parseStatusOutput( listLine, listChange ) )
                {
                    _listChange   = std::move( listChange );
                    _bStatusKnown = SW_TRUE;
                    _lastError.clear();
                }
                else
                {
                    StringBuilder<constant::buffer::kMax256> lastErrorBuilder;
                    lastErrorBuilder.appendFormat( "Listing changes failed (exit %#): %#", exitCode, lastLine );
                    _lastError = string( lastErrorBuilder.view() );
                    SW_LOG_WARNING_ID( "SourceControl.StatusFailed", "Source control: %#", _lastError );
                }
                ++_changeRevision;
                return;
            }
            case SourceControlOperation::Diff:
            case SourceControlOperation::History:
            {
                _detailPath      = operation._repositoryPath;
                _detailOperation = operation._operation;
                _listDetailLine  = listLine;
                if ( exitCode != 0 )
                {
                    StringBuilder<constant::buffer::kMax256> lastErrorBuilder;
                    lastErrorBuilder.appendFormat( "%# of %# failed (exit %#): %#", operation._operation == SourceControlOperation::Diff ? "Diff" : "History", operation._repositoryPath, exitCode, lastLine );
                    _lastError = string( lastErrorBuilder.view() );
                }
                ++_changeRevision;
                return;
            }
            case SourceControlOperation::Revert:
            {
                if ( exitCode == 0 )
                {
                    SW_LOG_INFO_ID( "SourceControl.Reverted", "Source control: reverted %# to HEAD", operation._repositoryPath );
                    _lastError.clear();
                }
                else
                {
                    StringBuilder<constant::buffer::kMax256> lastErrorBuilder;
                    lastErrorBuilder.appendFormat( "Revert of %# failed (exit %#): %#", operation._repositoryPath, exitCode, lastLine );
                    _lastError = string( lastErrorBuilder.view() );
                    SW_LOG_WARNING_ID( "SourceControl.RevertFailed", "Source control: %#", _lastError );
                }
                (void)requestStatus();
                return;
            }
            case SourceControlOperation::Refresh:
            {
                vector<SourceControlLock> listLock;
                if ( exitCode == 0 && _pProvider->parseRefreshOutput( listLine, listLock ) )
                    _listLock = std::move( listLock );
                else
                    SW_LOG_WARNING_ID( "SourceControl.SourceControlListingLocks", "Source control: listing locks failed (exit %#): %#", exitCode, lastLine.c_str() );
                return;
            }
            case SourceControlOperation::Lock:
            case SourceControlOperation::Unlock:
            {
                const utf8* pVerb = operation._operation == SourceControlOperation::Lock ? "lock" : "unlock";
                if ( exitCode == 0 )
                    SW_LOG_INFO_ID( "SourceControl.SourceControl2", "Source control: %# %# - %#", pVerb, operation._repositoryPath.c_str(), lastLine.c_str() );
                else
                    SW_LOG_WARNING_ID( "SourceControl.SourceControlFailedExit", "Source control: %# %# failed (exit %#): %#", pVerb, operation._repositoryPath.c_str(), exitCode, lastLine.c_str() );
                requestRefresh();
                return;
            }
            case SourceControlOperation::None:
            {
                return;
            }
        }
    }

    const SourceControlLock* EditorSourceControl::findLock( string_view absolutePath ) const
    {
        const string repositoryPath = makeRepositoryPath( _repositoryRoot, absolutePath );
        if ( repositoryPath.empty() )
            return nullptr;
        return VectorUtil::findIf( _listLock, [&]( const SourceControlLock& lock )
        { return StringUtil::equals( lock._path, repositoryPath, true ); } );
    }

    string EditorSourceControl::describeStatus( string_view absolutePath, bool bReadOnly ) const
    {
        const SourceControlLock* pLock = findLock( absolutePath );
        if ( pLock != nullptr )
            return "Locked by " + pLock->_owner + " (" + string{ _pProvider->getName() } + ")";
        if ( bReadOnly )
            return _pProvider->canLock() ? string{ "Read-only - Check Out (lock) before editing" } : string{ "Read-only file" };
        return {};
    }

    string EditorSourceControl::makeRepositoryPath( string_view repositoryRoot, string_view absolutePath )
    {
        if ( repositoryRoot.empty() || absolutePath.empty() )
            return {};
        // 비교는 OS 대소문자 규칙으로, 돌려주는 경로는 원래 철자로 — git 은 리눅스에서 대소문자를 가린다.
        string relative;
        if ( FileUtil::tryMakePathUnderRoot( repositoryRoot, absolutePath, relative ) == false )
            return {};
        return relative;
    }
} // namespace sw::editor
