#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorRemoteInspector.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerInspector" );

    namespace
    {
        struct MultiplayerInspectorTabInternal
        {
            /** @brief 하이어라키 · 값 칸이 차지하는 높이(남은 높이에 대한 비율)입니다. 나머지는 입력줄 결과입니다. */
            static constexpr float32 kTreeHeightRatio = 0.6f;
            /** @brief 하이어라키 칸의 너비(창 너비에 대한 비율)입니다. */
            static constexpr float32 kTreeWidthRatio = 0.4f;

            /** @brief 탭과 `netplay.inspect` 가 같이 쓰는 검사기입니다(창을 닫았다 열어도 연결이 남는다 — 에디터 컨텍스트가 사라지면 끊긴다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorRemoteInspector& getInspector() { return getEditorState<EditorRemoteInspector>(); }

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
            }

            /** @brief 세션이 띄운 전용 서버의 채널에 붙어 있으면 true 입니다. */
            static bool isAttachedToSession( const EditorNetPlaySession& session )
            {
                const EditorRemoteInspector& inspector = getInspector();
                return session.getDebugChannelPort() != 0 && inspector.isConnected() &&
                       inspector.getRemoteAddress() == NetAddress::makeLoopback( session.getDebugChannelPort() );
            }

            /** @brief 세션이 띄운 전용 서버의 채널에 붙습니다(이미 붙어 있으면 그대로). 채널이 없으면 false 이고 @p outReply 에 이유를 씁니다. */
            [[nodiscard]] static bool attachToSession( EditorNetPlaySession& session, string& outReply )
            {
                if ( isAttachedToSession( session ) )
                    return true;
                if ( session.isRunning() == false || session.getDebugChannelPort() == 0 )
                {
                    outReply = "no dedicated server session with a debug channel is running";
                    return false;
                }
                EditorRemoteInspector& inspector = getInspector();
                if ( inspector.connect( NetAddress::makeLoopback( session.getDebugChannelPort() ), session.getDebugChannelToken() ) == false )
                {
                    outReply = inspector.getLastError();
                    return false;
                }
                return true;
            }

            /** @brief 탐침 `Editor.NetPlayInspectorObjects` — 검사기가 마지막으로 읽은 하이어라키의 오브젝트 수입니다. */
            [[nodiscard]] static bool readObjectCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getInspector().getObjectRows().size() );
                return true;
            }

            /**
             * @brief `netplay.inspect [<명령 줄>]` — 세션의 전용 서버 디버그 채널에 붙어, 인자가 없으면 하이어라키를 새로 읽고 있으면 그 줄을 채널로 보냅니다.
             * @details 탭이 하는 일과 같은 길이다. 시나리오는 탭을 띄우지 않으므로 이 명령과 탐침 `Editor.NetPlayInspectorObjects` 로 확인한다.
             */
            static bool runInspect( const vector<string>& listArgument, string& outReply )
            {
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr )
                {
                    outReply = "no editor context";
                    return true;
                }
                if ( attachToSession( *pSession, outReply ) == false )
                    return true;
                EditorRemoteInspector& inspector = getInspector();
                if ( listArgument.empty() )
                {
                    if ( inspector.refreshTree() )
                        outReply = "inspector read " + to_string( inspector.getObjectRows().size() ) + " object(s)";
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

            /** @brief 입력줄의 Tab 자동완성입니다 — 첫 낱말을 검사기가 `help` 로 받은 명령 이름으로 채웁니다. 여럿이면 겹치는 머리까지 채웁니다. */
            static int32 onCommandLineEdit( ImGuiInputTextCallbackData* pData )
            {
                if ( pData->EventFlag != ImGuiInputTextFlags_CallbackCompletion )
                    return 0;
                const string_view line( pData->Buf, static_cast<size_t>( pData->BufTextLen ) );
                if ( line.find( ' ' ) != string_view::npos )
                    return 0; // 이름 뒤(인자)는 채우지 않는다
                vector<string> listName;
                getInspector().collectCompletions( line, listName );
                if ( listName.empty() )
                    return 0;
                string common = listName.front();
                for ( const string& name : listName )
                {
                    const size_t shorter = std::min( common.size(), name.size() );
                    size_t       length  = 0;
                    while ( length < shorter && common[length] == name[length] )
                    {
                        ++length;
                    }
                    common.resize( length );
                }
                if ( listName.size() == 1 )
                    common += ' ';
                if ( common.size() > line.size() )
                {
                    pData->DeleteChars( 0, pData->BufTextLen );
                    pData->InsertChars( 0, common.c_str() );
                }
                return 0;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayInspectorObjects, "Editor.NetPlayInspectorObjects",
                         "Objects the multiplayer Inspector tab last read from the server hierarchy (scene.tree over the debug channel)",
                         &MultiplayerInspectorTabInternal::readObjectCount );
    SW_DEV_COMMAND( NetPlayInspect, "netplay.inspect", "netplay.inspect [<command line>]",
                    "Attach the multiplayer Inspector to the session's dedicated server and read its hierarchy, or send that line over the debug channel",
                    &MultiplayerInspectorTabInternal::runInspect );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerInspectorTab
     * @brief Inspector 탭 — 전용 서버의 하이어라키(`scene.tree`) · 컴포넌트 값(`object.dump`)을 보이고 개발 명령 입력줄을 디버그 채널로 보냅니다(서버 디버깅 S3).
     * @details 언리얼 PIE 의 서버 월드 아웃라이너에 해당. 세션이 띄운 전용 서버에는 세션이 고른 포트 · 토큰으로 붙고(`EditorNetPlaySession::getDebugChannelPort`),
     *          이미 떠 있는 서버에는 주소 · 토큰으로 붙는다. 요청은 루프백에서 답을 기다리는 동기 방식이라 누를 때만 묻는다(프레임마다 묻지 않는다).
     *          리슨 호스트(App)는 채널을 열지 않아 이 탭의 대상이 아니다.
     */
    class MultiplayerInspectorTab final : public IMultiplayerTab
    {
    public:
        MultiplayerInspectorTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 연결 줄 — 세션 서버에 붙기 · 주소로 붙기 · 끊기 · 새로 읽기입니다. */
        void drawConnection( EditorNetPlaySession& session );
        /** @brief 하이어라키 표와 고른 오브젝트의 값입니다. */
        void drawTreeAndValues( float32 height );
        /** @brief 입력줄 결과와 입력줄입니다. */
        void drawConsole();

        fixed_string<constant::kMaxBuffer64>  _address;             ///< 주소로 붙기 — `127.0.0.1:<포트>`(편집 필드)
        fixed_string<constant::kMaxBuffer128> _token;               ///< 주소로 붙기 — 토큰(편집 필드)
        fixed_string<constant::kMaxBuffer512> _commandLine;         ///< 입력줄(편집 필드)
        bool                                  _bScrollConsoleToEnd; ///< 새 결과가 왔으니 다음 그리기에서 끝으로 스크롤
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerInspectorTab::MultiplayerInspectorTab()
        : _address{}
        , _token{}
        , _commandLine{}
        , _bScrollConsoleToEnd{ false }
    {
    }

    void MultiplayerInspectorTab::drawConnection( EditorNetPlaySession& session )
    {
        EditorRemoteInspector& inspector       = MultiplayerInspectorTabInternal::getInspector();
        const bool             bSessionChannel = session.isRunning() && session.getDebugChannelPort() != 0;
        if ( inspector.isConnected() )
        {
            ImGui::Text( "Connected to %s", inspector.getRemoteAddress().toString().c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Disconnect" ) );
            if ( ImGui::Button( "Disconnect" ) )
                inspector.disconnect();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Refresh" ) );
            if ( ImGui::Button( "Refresh" ) && inspector.refreshTree() == false )
                SW_LOG_WARNING( "Inspector could not read the hierarchy: %#", inspector.getLastError() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "InspectorRefresh" ) );
        }
        else if ( bSessionChannel )
        {
            ImGui::Text( "Session server debug channel 127.0.0.1:%u", static_cast<uint32>( session.getDebugChannelPort() ) );
            ImGui::SameLine();
            string     reply;
            const bool bAttached = ImGui::Button( "Connect" ) && MultiplayerInspectorTabInternal::attachToSession( session, reply );
            if ( bAttached && inspector.refreshTree() == false )
                SW_LOG_WARNING( "Inspector could not read the hierarchy: %#", inspector.getLastError() );
            if ( reply.empty() == false )
                SW_LOG_WARNING( "Inspector could not attach to the session server: %#", reply );
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "InspectorNoChannel" ) );
        }
        if ( inspector.isConnected() == false && inspector.getLastError().empty() == false )
            ImGui::TextDisabled( "Last error: %s", inspector.getLastError().c_str() );

        if ( ImGui::CollapsingHeader( "Attach by address" ) )
        {
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "InspectorAttach" ) );
            ImGui::InputTextWithHint( "##InspectorAddress", "127.0.0.1:<port>", _address.data(), _address.capacity() );
            ImGui::SameLine();
            ImGui::InputTextWithHint( "##InspectorToken", "token", _token.data(), _token.capacity(), ImGuiInputTextFlags_Password );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Attach" ) );
            if ( ImGui::Button( "Attach" ) )
            {
                NetAddress remote;
                // 채널은 루프백에만 묶이므로 다른 주소는 붙기 전에 거절한다.
                const bool bLoopback = NetAddress::parse( _address.c_str(), 0, remote ) && remote.isValid() && remote == NetAddress::makeLoopback( remote._port );
                if ( bLoopback == false )
                    SW_LOG_WARNING( "Inspector attaches only to 127.0.0.1:<port> - '%#' is not one", _address.c_str() );
                else if ( inspector.connect( remote, _token.c_str() ) == false )
                    SW_LOG_WARNING( "Inspector could not attach to %#: %#", remote.toString(), inspector.getLastError() );
                else if ( inspector.refreshTree() == false )
                    SW_LOG_WARNING( "Inspector could not read the hierarchy: %#", inspector.getLastError() );
            }
        }
    }

    void MultiplayerInspectorTab::drawTreeAndValues( float32 height )
    {
        EditorRemoteInspector& inspector = MultiplayerInspectorTabInternal::getInspector();
        const float32          treeWidth = ImGui::GetContentRegionAvail().x * MultiplayerInspectorTabInternal::kTreeWidthRatio;
        if ( ImGui::BeginChild( "##InspectorTree", ImVec2( treeWidth, height ), ImGuiChildFlags_Borders ) )
        {
            if ( ImGui::BeginTable( "##InspectorObjects", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY ) )
            {
                ImGui::TableSetupColumn( "Name" );
                ImGui::TableSetupColumn( "Parent" );
                ImGui::TableSetupColumn( "Components" );
                ImGui::TableSetupScrollFreeze( 0, 1 );
                ImGui::TableHeadersRow();
                // 덤프가 고른 이름을 바꾸므로 고를 이름을 모아 두었다가 테이블을 닫은 뒤 묻는다(요청은 테이블을 다시 채우지 않지만 순서를 분명히 한다).
                string pickedName;
                for ( const EditorRemoteObjectRow& row : inspector.getObjectRows() )
                {
                    ImGui::TableNextColumn();
                    const bool bNamed    = row._name != EditorRemoteInspector::kUnnamedObject;
                    const bool bSelected = bNamed && row._name == inspector.getSelectedName();
                    ImGui::PushID( static_cast<int32>( row._objectID ) );
                    if ( row._bActive == false )
                        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                    if ( ImGui::Selectable( row._name.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns ) && bNamed )
                        pickedName = row._name;
                    if ( row._bActive == false )
                        ImGui::PopStyleColor();
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._parentName.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._components.c_str() );
                }
                ImGui::EndTable();
                if ( pickedName.empty() == false )
                    (void)inspector.dumpObject( pickedName ); // 실패면 덤프 칸에 오류 글이 보인다
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if ( ImGui::BeginChild( "##InspectorValues", ImVec2( 0.0f, height ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            if ( inspector.getSelectedName().empty() )
                ImGui::TextDisabled( "Pick an object on the left" );
            else
                ImGui::TextUnformatted( inspector.getDumpText().c_str() );
        }
        ImGui::EndChild();
    }

    void MultiplayerInspectorTab::drawConsole()
    {
        EditorRemoteInspector& inspector     = MultiplayerInspectorTabInternal::getInspector();
        const float32          commandHeight = ImGui::GetFrameHeightWithSpacing();
        if ( ImGui::BeginChild( "##InspectorConsole", ImVec2( 0.0f, -commandHeight ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            for ( const string& line : inspector.getConsoleLines() )
            {
                ImGui::TextUnformatted( line.c_str() );
            }
            if ( _bScrollConsoleToEnd )
                ImGui::SetScrollHereY( 1.0f );
            _bScrollConsoleToEnd = false;
        }
        ImGui::EndChild();
        constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion;
        ImGui::SetNextItemWidth( -1.0f );
        const bool bEntered = ImGui::InputTextWithHint( "##InspectorCommand", SW_LOCKEY( "EditorMultiplayer", "InspectorConsoleHint" ), _commandLine.data(),
                                                        _commandLine.capacity(), kFlags, &MultiplayerInspectorTabInternal::onCommandLineEdit );
        if ( bEntered )
        {
            (void)inspector.execute( _commandLine.c_str() ); // 결과(오류 포함)는 결과 줄에 남는다
            _commandLine.clear();
            _bScrollConsoleToEnd = true;
            ImGui::SetKeyboardFocusHere( -1 );
        }
    }

    void MultiplayerInspectorTab::drawTab( const MultiplayerTabContext& context )
    {
        drawConnection( *context._pSession );
        if ( MultiplayerInspectorTabInternal::getInspector().isConnected() == false )
            return;
        ImGui::Separator();
        const float32 treeHeight = ImGui::GetContentRegionAvail().y * MultiplayerInspectorTabInternal::kTreeHeightRatio;
        drawTreeAndValues( treeHeight );
        drawConsole();
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerInspectorTab, "inspector", 250, "Inspector" );
} // namespace sw::editor
