#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerInspectorViewModel.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/NetPlay/EditorRemoteInspector.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct MultiplayerInspectorViewModelInternal
        {
            /** @brief 탭과 `netplay.inspect` 가 같이 쓰는 검사기입니다(창을 닫았다 열어도 연결이 남는다 — 에디터 컨텍스트가 사라지면 끊긴다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorRemoteInspector& getInspector() { return getEditorState<EditorRemoteInspector>(); }

            /** @brief 세션이 띄운 전용 서버의 채널에 붙어 있으면 true 입니다. */
            static bool isAttachedToSession( const EditorNetPlaySession& session )
            {
                const EditorRemoteInspector& inspector = getInspector();
                return session.getDebugChannelPort() != 0 && inspector.isConnected() &&
                       inspector.getRemoteAddress() == NetAddress::makeLoopback( session.getDebugChannelPort() );
            }

            /**
             * @brief 세션이 띄운 전용 서버의 채널에 붙습니다(이미 붙어 있으면 그대로). 채널이 없거나 붙지 못하면 그 이유입니다.
             * @details 붙기 실패의 온전한 글은 검사기의 마지막 오류다(`describeAttachFailure`) — 오류의 대상 칸은 길이가 잘린다.
             */
            [[nodiscard]] static expected<void> attachToSession( EditorNetPlaySession& session )
            {
                if ( isAttachedToSession( session ) )
                    return {};
                if ( session.isRunning() == false || session.getDebugChannelPort() == 0 )
                    return unexpected( Error{ ErrorKind::NotFound, "no dedicated server session with a debug channel is running" } );
                EditorRemoteInspector& inspector = getInspector();
                if ( inspector.connect( NetAddress::makeLoopback( session.getDebugChannelPort() ), session.getDebugChannelToken() ) == false )
                    return unexpected( Error{ ErrorKind::ExternalFailure, "the inspector could not attach to the session server", inspector.getLastError() } );
                return {};
            }

            /** @brief `attachToSession` 실패를 답 · 경고 글로 씁니다. 채널 실패는 검사기의 마지막 오류 글 그대로입니다. */
            static string describeAttachFailure( const Error& error )
            {
                return error.detail().empty() ? string( error.message() ) : getInspector().getLastError();
            }

            /** @brief 하이어라키를 새로 읽고, 읽지 못하면 경고로 알립니다. */
            static void refreshTreeOrWarn( EditorRemoteInspector& inspector )
            {
                if ( inspector.refreshTree() == false )
                    SW_LOG_WARNING_ID( "MultiplayerInspector.InspectorCouldNotRead", "Inspector could not read the hierarchy: %#", inspector.getLastError() );
            }

            /** @brief 감시 값 `Editor.NetPlayInspectorObjects` — 검사기가 마지막으로 읽은 하이어라키의 오브젝트 수입니다. */
            [[nodiscard]] static bool readObjectCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getInspector().getObjectRows().size() );
                return true;
            }

            /**
             * @brief `netplay.inspect [<명령 줄>]` — 세션의 전용 서버 디버그 채널에 붙어, 인자가 없으면 하이어라키를 새로 읽고 있으면 그 줄을 채널로 보냅니다.
             * @details 탭이 하는 일과 같은 길이다. 시나리오는 탭을 띄우지 않으므로 이 명령과 감시 값 `Editor.NetPlayInspectorObjects` 로 확인한다.
             */
            static bool runInspect( const vector<string>& listArgument, string& outReply )
            {
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr )
                {
                    outReply = "no editor context";
                    return true;
                }
                if ( const expected<void> attached = attachToSession( *pSession ); attached.has_value() == false )
                {
                    outReply = describeAttachFailure( attached.error() );
                    return true;
                }
                EditorRemoteInspector& inspector = getInspector();
                if ( listArgument.empty() )
                {
                    if ( inspector.refreshTree() )
                    {
                        StringBuilder<constant::buffer::kMax256> replyBuilder;
                        replyBuilder.appendFormat( "inspector read %# object(s)", inspector.getObjectRows().size() );
                        outReply = string( replyBuilder.view() );
                    }
                    else
                        outReply = inspector.getLastError();
                    return true;
                }
                string line;
                for ( const string& argument : listArgument )
                {
                    if ( line.empty() == false )
                        line += ' ';
                    line += argument;
                }
                (void)inspector.execute( line ); // 결과(오류 포함)는 결과 줄에 남았다 — 마지막 줄을 답으로 돌려준다
                outReply = inspector.getConsoleLines().empty() ? string{} : inspector.getConsoleLines().back();
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayInspectorObjects, "Editor.NetPlayInspectorObjects",
                         "Objects the multiplayer Inspector tab last read from the server hierarchy (scene.tree over the debug channel)",
                         &MultiplayerInspectorViewModelInternal::readObjectCount );
    SW_DEV_COMMAND( NetPlayInspect, "netplay.inspect", "netplay.inspect [<command line>]",
                    "Attach the multiplayer Inspector to the session's dedicated server and read its hierarchy, or send that line over the debug channel",
                    &MultiplayerInspectorViewModelInternal::runInspect );
} // namespace sw::editor

namespace sw::editor
{
    void MultiplayerInspectorViewModel::gather( MultiplayerInspectorViewState& outState ) const
    {
        const EditorRemoteInspector&         inspector = MultiplayerInspectorViewModelInternal::getInspector();
        const vector<EditorRemoteObjectRow>& listRow   = inspector.getObjectRows();
        outState._listObjectRow.resize( listRow.size() );
        for ( size_t index = 0; index < listRow.size(); ++index )
        {
            const EditorRemoteObjectRow&   source = listRow[index];
            MultiplayerInspectorObjectRow& row    = outState._listObjectRow[index];
            row._name                             = source._name;
            row._parentName                       = source._parentName;
            row._components                       = source._components;
            row._objectID                         = source._objectID;
            row._bActive                          = source._bActive ? SW_TRUE : SW_FALSE;
            row._bNamed                           = source._name != EditorRemoteInspector::kUnnamedObject ? SW_TRUE : SW_FALSE;
        }
        outState._listConsoleLine = inspector.getConsoleLines();
        outState._remoteAddress   = inspector.getRemoteAddress().toString();
        outState._selectedName    = inspector.getSelectedName();
        outState._dumpText        = inspector.getDumpText();
        outState._lastError       = inspector.getLastError();
        outState._bConnected      = inspector.isConnected() ? SW_TRUE : SW_FALSE;
    }

    void MultiplayerInspectorViewModel::connectToSession()
    {
        const expected<void> attached = MultiplayerInspectorViewModelInternal::attachToSession( MultiplayerSessionViewModel::getSession() );
        if ( attached.has_value() )
            MultiplayerInspectorViewModelInternal::refreshTreeOrWarn( MultiplayerInspectorViewModelInternal::getInspector() );
        else
            SW_LOG_WARNING_ID( "MultiplayerInspector.InspectorCouldNotAttach", "Inspector could not attach to the session server: %#",
                               MultiplayerInspectorViewModelInternal::describeAttachFailure( attached.error() ) );
    }

    void MultiplayerInspectorViewModel::attachByAddress( string_view address, string_view token )
    {
        EditorRemoteInspector& inspector = MultiplayerInspectorViewModelInternal::getInspector();
        NetAddress             remote;
        // 채널은 루프백에만 묶이므로 다른 주소는 붙기 전에 거절한다.
        const bool bLoopback = NetAddress::parse( address, 0, remote ) && remote.isValid() && remote.isLoopback();
        if ( bLoopback == false )
            SW_LOG_WARNING_ID( "MultiplayerInspector.InspectorAttachesOnlyPort", "Inspector attaches only to 127.0.0.1:<port> - '%#' is not one", string( address ) );
        else if ( inspector.connect( remote, token ) == false )
            SW_LOG_WARNING_ID( "MultiplayerInspector.InspectorCouldNotAttach2", "Inspector could not attach to %#: %#", remote.toString(), inspector.getLastError() );
        else
            MultiplayerInspectorViewModelInternal::refreshTreeOrWarn( inspector );
    }

    void MultiplayerInspectorViewModel::disconnect()
    {
        MultiplayerInspectorViewModelInternal::getInspector().disconnect();
    }

    void MultiplayerInspectorViewModel::refresh()
    {
        MultiplayerInspectorViewModelInternal::refreshTreeOrWarn( MultiplayerInspectorViewModelInternal::getInspector() );
    }

    void MultiplayerInspectorViewModel::selectObject( const string& name )
    {
        (void)MultiplayerInspectorViewModelInternal::getInspector().dumpObject( name ); // 실패면 덤프 칸에 오류 글이 보인다
    }

    void MultiplayerInspectorViewModel::execute( string_view line )
    {
        (void)MultiplayerInspectorViewModelInternal::getInspector().execute( line ); // 결과(오류 포함)는 결과 줄에 남는다
    }

    void MultiplayerInspectorViewModel::collectCompletions( string_view prefix, vector<string>& outListName ) const
    {
        MultiplayerInspectorViewModelInternal::getInspector().collectCompletions( prefix, outListName );
    }
} // namespace sw::editor
