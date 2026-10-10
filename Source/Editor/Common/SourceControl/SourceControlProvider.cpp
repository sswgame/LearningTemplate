#include "pch.h"

#include "Editor/Common/SourceControl/SourceControlProvider.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct SourceControlProviderInternal
        {
            /** @brief 명령줄 인자 하나를 큰따옴표로 감쌉니다. 경로 안의 큰따옴표는 파일 이름에 쓸 수 없으므로 빼 버립니다. */
            static string quoteArgument( string_view value )
            {
                string quoted{ "\"" };
                for ( const utf8 character : value )
                {
                    if ( character != '"' )
                        quoted += character;
                }
                quoted += "\"";
                return quoted;
            }
        };
    } // namespace

    string GitLfsSourceControlProvider::makeProbeCommand()
    {
        return "git lfs version";
    }

    string GitLfsSourceControlProvider::makeRefreshCommand() const
    {
        return "git lfs locks --json";
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

    string GitLfsSourceControlProvider::makeLockCommand( string_view repositoryPath ) const
    {
        return "git lfs lock " + SourceControlProviderInternal::quoteArgument( repositoryPath );
    }

    string GitLfsSourceControlProvider::makeUnlockCommand( string_view repositoryPath ) const
    {
        return "git lfs unlock " + SourceControlProviderInternal::quoteArgument( repositoryPath );
    }
} // namespace sw::editor
