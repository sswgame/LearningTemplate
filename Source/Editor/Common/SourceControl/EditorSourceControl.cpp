#include "pch.h"

#include "Editor/Common/SourceControl/EditorSourceControl.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorExternalToolJob.h"

namespace sw::editor
{
    SW_LOG_CALLER( "SourceControl" );

    EditorSourceControl::EditorSourceControl( string_view repositoryRoot )
        : _repositoryRoot{ FileUtil::normalizeSeparators( repositoryRoot ) }
        , _pProvider{ make_unique<NullSourceControlProvider>() }
        , _pJob{ make_unique<EditorExternalToolJob>() }
        , _listLock{}
        , _listPending{}
        , _running{}
    {
    }

    EditorSourceControl::~EditorSourceControl() = default;

    void EditorSourceControl::initialize()
    {
        if ( _repositoryRoot.empty() == false )
            enqueue( SourceControlOperation::Probe, {} );
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
        if ( _pProvider->makeRefreshCommand().empty() == false )
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
        if ( _running._operation != SourceControlOperation::None && _pJob->take( result ) )
        {
            const PendingOperation finished = std::move( _running );
            _running                        = PendingOperation{};
            applyResult( finished, result._exitCode, result._bLaunched, result._listLine );
        }
        if ( _running._operation != SourceControlOperation::None || _listPending.empty() )
            return;

        const PendingOperation next = _listPending.front();
        _listPending.erase( _listPending.begin() );
        string command;
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
            case SourceControlOperation::None:
            {
                break;
            }
        }
        if ( command.empty() )
            return;
        if ( _pJob->request( command, _repositoryRoot ) )
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
                    SW_LOG_INFO( "Source control: %# (%#)", _pProvider->getName().data(), lastLine.c_str() );
                    requestRefresh();
                }
                else
                {
                    SW_LOG_INFO( "Source control: git lfs is not available - locking is off, read-only files are still shown" );
                }
                return;
            }
            case SourceControlOperation::Refresh:
            {
                vector<SourceControlLock> listLock;
                if ( exitCode == 0 && _pProvider->parseRefreshOutput( listLine, listLock ) )
                    _listLock = std::move( listLock );
                else
                    SW_LOG_WARNING( "Source control: listing locks failed (exit %#): %#", exitCode, lastLine.c_str() );
                return;
            }
            case SourceControlOperation::Lock:
            case SourceControlOperation::Unlock:
            {
                const utf8* pVerb = operation._operation == SourceControlOperation::Lock ? "lock" : "unlock";
                if ( exitCode == 0 )
                    SW_LOG_INFO( "Source control: %# %# - %#", pVerb, operation._repositoryPath.c_str(), lastLine.c_str() );
                else
                    SW_LOG_WARNING( "Source control: %# %# failed (exit %#): %#", pVerb, operation._repositoryPath.c_str(), exitCode, lastLine.c_str() );
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
        for ( const SourceControlLock& lock : _listLock )
        {
            if ( StringUtil::equals( lock._path, repositoryPath, true ) )
                return &lock;
        }
        return nullptr;
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
        // 비교는 소문자로(Windows 경로), 돌려주는 경로는 원래 철자로 — git 은 리눅스에서 대소문자를 가린다.
        const string root      = FileUtil::normalizePath( repositoryRoot );
        const string lowerPath = FileUtil::normalizePath( absolutePath );
        if ( FileUtil::startsWithPathComponent( lowerPath, root ) == false )
            return {};
        const string lowerSuffix = FileUtil::suffixAfterPathComponent( lowerPath, root );
        const string original    = FileUtil::normalizeSeparators( absolutePath );
        if ( lowerSuffix.empty() || lowerSuffix.size() > original.size() )
            return {};
        return original.substr( original.size() - lowerSuffix.size() );
    }
} // namespace sw::editor
