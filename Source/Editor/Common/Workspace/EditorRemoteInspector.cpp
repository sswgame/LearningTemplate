#include "pch.h"

#include "Editor/Common/Workspace/EditorRemoteInspector.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Observability/DebugChannel.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct EditorRemoteInspectorInternal
        {
            /** @brief 채널이 받는 줄 가운데 `help` 의 명령 목록 밖에 있는 것입니다 — 서버 루프 명령과 콘솔 내장 명령. */
            static constexpr const utf8* kArrBuiltinName[] = { "get", "help", "pause", "resume", "set", "step" };
            /** @brief `help` 답에서 개발 명령 한 줄의 머리입니다(`  <사용법> - <설명>`). */
            static constexpr string_view kCommandLinePrefix = "  ";
            /** @brief `scene.tree` 답의 첫 줄 머리입니다. */
            static constexpr string_view kTreeHeadPrefix = "objects ";
            /** @brief `scene.tree` 가 한도에 닿았을 때의 마지막 줄 머리입니다. */
            static constexpr string_view kTreeLimitPrefix = "...";

            /** @brief @p text 에서 @p cursor 부터 다음 줄 하나를 꺼냅니다. 더 없으면 false. */
            static bool takeLine( string_view text, size_t& inoutCursor, string_view& outLine )
            {
                if ( inoutCursor >= text.size() )
                    return false;
                const size_t lineEnd = text.find( '\n', inoutCursor );
                const size_t stop    = lineEnd == string_view::npos ? text.size() : lineEnd;
                outLine              = text.substr( inoutCursor, stop - inoutCursor );
                inoutCursor          = stop + 1;
                return true;
            }

            /** @brief `key=value` 칸에서 값을 꺼냅니다. 머리가 다르면 false. */
            [[nodiscard]] static bool tryGetField( string_view token, string_view key, string_view& outValue )
            {
                if ( StringUtil::startsWith( token, key ) == false )
                    return false;
                outValue = token.substr( key.size() );
                return true;
            }

            /** @brief `<id> <이름> parent=<이름> active=<0|1> components=<목록>` 한 줄을 나눕니다. */
            [[nodiscard]] static bool parseObjectRow( string_view line, EditorRemoteObjectRow& outRow )
            {
                // 이름에 빈칸이 있을 수 있어 뒤에서부터 칸을 찾는다 — 칸 머리 셋은 서버가 늘 같은 순서로 적는다.
                const size_t parentAt     = line.rfind( " parent=" );
                const size_t activeAt     = line.rfind( " active=" );
                const size_t componentsAt = line.rfind( " components=" );
                const size_t idEnd        = line.find( ' ' );
                const bool   bOrdered     = idEnd != string_view::npos && parentAt != string_view::npos && activeAt != string_view::npos &&
                                      componentsAt != string_view::npos && idEnd < parentAt && parentAt < activeAt && activeAt < componentsAt;
                if ( bOrdered == false )
                    return false;
                if ( StringUtil::parseUint64( line.substr( 0, idEnd ), outRow._objectID ) == false )
                    return false;
                string_view parentName;
                string_view active;
                string_view components;
                const bool  bFields = tryGetField( line.substr( parentAt + 1, activeAt - parentAt - 1 ), "parent=", parentName ) &&
                                     tryGetField( line.substr( activeAt + 1, componentsAt - activeAt - 1 ), "active=", active ) &&
                                     tryGetField( line.substr( componentsAt + 1 ), "components=", components );
                if ( bFields == false )
                    return false;
                outRow._name       = string( line.substr( idEnd + 1, parentAt - idEnd - 1 ) );
                outRow._parentName = string( parentName );
                outRow._bActive    = active == "1";
                outRow._components = string( components );
                return true;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorRemoteInspector::EditorRemoteInspector()
        : _client{ make_unique<DebugChannelClient>() }
        , _listObjectRow{}
        , _listCommandName{}
        , _listConsoleLine{}
        , _selectedName{}
        , _dumpText{}
        , _lastError{}
        , _remoteAddress{}
    {
    }

    EditorRemoteInspector::~EditorRemoteInspector()
    {
        disconnect();
    }

    bool EditorRemoteInspector::connect( const NetAddress& remote, string_view token )
    {
        disconnect();
        _listObjectRow.clear();
        _listCommandName.clear();
        _listConsoleLine.clear();
        _selectedName.clear();
        _dumpText.clear();
        _lastError.clear();
        _remoteAddress = remote;
        if ( _client->connect( remote, token, kConnectTimeoutMilliseconds ) == false )
        {
            _lastError = "could not connect to the debug channel at " + remote.toString();
            return false;
        }
        // 틀린 토큰은 연결이 열린 뒤 첫 요청에서 `ERR` 과 닫기로 드러난다 — 자동완성 원천을 묻는 첫 요청이 그 확인을 겸한다.
        string helpBody;
        if ( request( "help", helpBody ) == false )
        {
            _lastError = helpBody.empty() ? "the debug channel at " + remote.toString() + " did not answer help" : helpBody;
            disconnect();
            return false;
        }
        parseCommandNames( helpBody, _listCommandName );
        return true;
    }

    void EditorRemoteInspector::disconnect()
    {
        _client->disconnect();
    }

    bool EditorRemoteInspector::isConnected() const
    {
        return _client->isConnected();
    }

    bool EditorRemoteInspector::request( string_view commandLine, string& outBody )
    {
        if ( _client->isConnected() == false )
        {
            outBody.clear();
            _lastError = "not connected to a debug channel";
            return false;
        }
        const bool bOk = _client->request( commandLine, kRequestTimeoutMilliseconds, outBody );
        if ( bOk == false && _client->isConnected() == false )
            _lastError = "the debug channel closed the connection";
        else if ( bOk == false )
            _lastError = outBody.empty() ? "no answer within the request timeout" : outBody;
        return bOk;
    }

    bool EditorRemoteInspector::refreshTree()
    {
        string body;
        if ( request( "scene.tree", body ) == false )
            return false;
        vector<EditorRemoteObjectRow> listRow;
        if ( parseObjectRows( body, listRow ) == false )
        {
            _lastError = "scene.tree answered in an unknown format";
            return false;
        }
        _listObjectRow.swap( listRow );
        return true;
    }

    bool EditorRemoteInspector::dumpObject( string_view name )
    {
        _selectedName = string( name );
        string     body;
        const bool bOk = request( "object.dump " + _selectedName, body );
        _dumpText      = bOk ? std::move( body ) : _lastError;
        return bOk;
    }

    bool EditorRemoteInspector::execute( string_view line )
    {
        const string_view trimmed = StringUtil::trim( line );
        if ( trimmed.empty() )
            return true;
        appendConsoleLine( "> " + string( trimmed ) );
        string     body;
        const bool bOk = request( trimmed, body );
        appendConsoleLine( bOk ? std::move( body ) : "error: " + _lastError );
        return bOk;
    }

    void EditorRemoteInspector::appendConsoleLine( string line )
    {
        _listConsoleLine.push_back( std::move( line ) );
        if ( _listConsoleLine.size() > kMaxConsoleLine )
            _listConsoleLine.erase( _listConsoleLine.begin(), _listConsoleLine.begin() + static_cast<ptrdiff_t>( _listConsoleLine.size() - kMaxConsoleLine ) );
    }

    void EditorRemoteInspector::collectCompletions( string_view prefix, vector<string>& outListName ) const
    {
        outListName.clear();
        for ( const string& name : _listCommandName )
        {
            if ( StringUtil::startsWith( name, prefix, true ) )
                outListName.push_back( name );
        }
    }

    void EditorRemoteInspector::parseCommandNames( string_view helpBody, vector<string>& outListName )
    {
        outListName.clear();
        for ( const utf8* pName : EditorRemoteInspectorInternal::kArrBuiltinName )
        {
            outListName.emplace_back( pName );
        }
        size_t      cursor = 0;
        string_view line;
        while ( EditorRemoteInspectorInternal::takeLine( helpBody, cursor, line ) )
        {
            if ( StringUtil::startsWith( line, EditorRemoteInspectorInternal::kCommandLinePrefix ) == false )
                continue;
            const string_view usage   = StringUtil::trim( line );
            const size_t      nameEnd = usage.find( ' ' );
            const string_view name    = usage.substr( 0, nameEnd );
            if ( name.empty() == false )
                outListName.emplace_back( name );
        }
        std::sort( outListName.begin(), outListName.end() );
        outListName.erase( std::unique( outListName.begin(), outListName.end() ), outListName.end() );
    }

    bool EditorRemoteInspector::parseObjectRows( string_view treeBody, vector<EditorRemoteObjectRow>& outListRow )
    {
        outListRow.clear();
        size_t      cursor = 0;
        string_view line;
        while ( EditorRemoteInspectorInternal::takeLine( treeBody, cursor, line ) )
        {
            const bool bSkipped = line.empty() || StringUtil::startsWith( line, EditorRemoteInspectorInternal::kTreeHeadPrefix ) ||
                                  StringUtil::startsWith( line, EditorRemoteInspectorInternal::kTreeLimitPrefix );
            if ( bSkipped )
                continue;
            EditorRemoteObjectRow row;
            if ( EditorRemoteInspectorInternal::parseObjectRow( line, row ) == false )
                return false;
            outListRow.push_back( std::move( row ) );
        }
        return true;
    }
} // namespace sw::editor
