#include "pch.h"

#include "Editor/Common/SourceControl/SourceControlProvider.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    ProcessCommand GitLfsSourceControlProvider::makeProbeCommand()
    {
        return ProcessCommand::fromSearchPath( "git", { "lfs", "version" } );
    }

    ProcessCommand GitLfsSourceControlProvider::makeRefreshCommand() const
    {
        return ProcessCommand::fromSearchPath( "git", { "lfs", "locks", "--json" } );
    }

    bool GitLfsSourceControlProvider::parseRefreshOutput( const vector<string>& listLine, vector<SourceControlLock>& outListLock ) const
    {
        outListLock.clear();
        // 출력은 JSON 배열 한 줄이지만 앞뒤에 경고 줄이 섞일 수 있다 — '[' 로 시작하는 줄을 찾는다.
        string text;
        for ( const string& line : listLine )
        {
            if ( text.empty() && ( line.empty() || line.front() != '[' ) )
                continue;
            text += line;
        }
        if ( text.empty() )
            return false;

        JSONDocument document;
        if ( document.tryParse( text ) == false )
            return false;
        const JSONValue root = document.getRoot();
        if ( root.isArray() == false )
            return false;
        for ( size_t index = 0; index < root.size(); ++index )
        {
            const JSONValue entry = root.at( index );
            if ( entry.isObject() == false || entry.get( "path" ).isString() == false )
            {
                outListLock.clear();
                return false;
            }
            SourceControlLock lock;
            lock._path            = entry.get( "path" ).asString();
            lock._id              = entry.get( "id" ).isString() ? entry.get( "id" ).asString() : string{};
            const JSONValue owner = entry.get( "owner" );
            lock._owner           = owner.isObject() && owner.get( "name" ).isString() ? owner.get( "name" ).asString() : string{ "?" };
            outListLock.push_back( std::move( lock ) );
        }
        return true;
    }

    ProcessCommand GitLfsSourceControlProvider::makeLockCommand( string_view repositoryPath ) const
    {
        return ProcessCommand::fromSearchPath( "git", { "lfs", "lock", string( repositoryPath ) } );
    }

    ProcessCommand GitLfsSourceControlProvider::makeUnlockCommand( string_view repositoryPath ) const
    {
        return ProcessCommand::fromSearchPath( "git", { "lfs", "unlock", string( repositoryPath ) } );
    }

    ProcessCommand GitSourceControlProvider::makeProbeCommand()
    {
        return ProcessCommand::fromSearchPath( "git", { "--version" } );
    }

    ProcessCommand GitChangeCommands::makeStatusCommand()
    {
        // 경로를 따옴표로 감싸지 않게 한다(한글 파일 이름) — 그래도 감싼 경로는 아래에서 벗긴다
        return ProcessCommand::fromSearchPath( "git", { "-c", "core.quotepath=false", "status", "--porcelain=v1", "--untracked-files=all" } );
    }

    bool GitChangeCommands::parseStatusOutput( const vector<string>& listLine, vector<SourceControlChange>& outListChange )
    {
        outListChange.clear();
        const auto unquote = []( string_view path )
        {
            if ( path.size() >= 2 && path.front() == '"' && path.back() == '"' )
                path = path.substr( 1, path.size() - 2 );
            return string( path );
        };
        for ( const string& line : listLine )
        {
            if ( line.empty() )
                continue;
            if ( line.size() < 4 || line[2] != ' ' )
            {
                outListChange.clear();
                return false;
            }
            const utf8          indexState    = line[0];
            const utf8          worktreeState = line[1];
            const string_view   rest          = string_view( line ).substr( 3 );
            SourceControlChange change{};
            const size_t        arrow = rest.find( " -> " );
            if ( arrow != string_view::npos )
            {
                change._originalPath = unquote( rest.substr( 0, arrow ) );
                change._path         = unquote( rest.substr( arrow + 4 ) );
            }
            else
            {
                change._path = unquote( rest );
            }
            const bool bConflict = indexState == 'U' || worktreeState == 'U' || ( indexState == 'A' && worktreeState == 'A' ) || ( indexState == 'D' && worktreeState == 'D' );
            if ( indexState == '?' && worktreeState == '?' )
                change._kind = SourceControlChangeKind::Untracked;
            else if ( bConflict )
                change._kind = SourceControlChangeKind::Conflicted;
            else if ( indexState == 'R' || indexState == 'C' )
                change._kind = SourceControlChangeKind::Renamed;
            else if ( indexState == 'A' )
                change._kind = SourceControlChangeKind::Added;
            else if ( indexState == 'D' || worktreeState == 'D' )
                change._kind = SourceControlChangeKind::Deleted;
            else if ( indexState == 'M' || worktreeState == 'M' || indexState == 'T' || worktreeState == 'T' )
                change._kind = SourceControlChangeKind::Modified;
            else
            {
                outListChange.clear();
                return false; // 모르는 상태 글자 — 틀린 목록을 보이지 않는다
            }
            change._bStaged = indexState != ' ' && indexState != '?';
            outListChange.push_back( std::move( change ) );
        }
        return true;
    }

    ProcessCommand GitChangeCommands::makeDiffCommand( string_view repositoryPath )
    {
        return ProcessCommand::fromSearchPath( "git", { "--no-pager", "diff", "--no-color", "HEAD", "--", string( repositoryPath ) } );
    }

    ProcessCommand GitChangeCommands::makeHistoryCommand( string_view repositoryPath )
    {
        return ProcessCommand::fromSearchPath( "git", { "--no-pager", "log", "--no-color", "--date=short", "--pretty=format:%h %ad %an  %s", "-n", to_string( kHistoryCount ), "--", string( repositoryPath ) } );
    }

    ProcessCommand GitChangeCommands::makeRevertCommand( string_view repositoryPath )
    {
        return ProcessCommand::fromSearchPath( "git", { "restore", "--source=HEAD", "--staged", "--worktree", "--", string( repositoryPath ) } );
    }

    bool GitChangeCommands::canRevert( const SourceControlChange& change )
    {
        // 추적 밖 파일을 "되돌리면" 지워진다 — 되돌리기 어려운 삭제라 하지 않는다. 충돌은 손으로 푼다.
        return change._kind != SourceControlChangeKind::Untracked && change._kind != SourceControlChangeKind::Conflicted;
    }
} // namespace sw::editor
