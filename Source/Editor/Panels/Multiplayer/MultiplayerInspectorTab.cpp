#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerInspectorViewModel.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerInspectorTabInternal
        {
            /** @brief 하이어라키 · 값 칸이 차지하는 높이(남은 높이에 대한 비율)입니다. 나머지는 입력줄 결과입니다. */
            static constexpr float32 kTreeHeightRatio = 0.6f;
            /** @brief 하이어라키 칸의 너비(창 너비에 대한 비율)입니다. */
            static constexpr float32 kTreeWidthRatio = 0.4f;

            /** @brief 입력줄의 Tab 자동완성입니다 — 첫 낱말을 검사기가 `help` 로 받은 명령 이름으로 채웁니다. 여럿이면 겹치는 머리까지 채웁니다. 사용자 자료는 탭의 ViewModel 입니다. */
            static int32 onCommandLineEdit( ImGuiInputTextCallbackData* pData )
            {
                if ( pData->EventFlag != ImGuiInputTextFlags_CallbackCompletion )
                    return 0;
                const string_view line( pData->Buf, static_cast<size_t>( pData->BufTextLen ) );
                if ( line.find( ' ' ) != string_view::npos )
                    return 0; // 이름 뒤(인자)는 채우지 않는다
                vector<string> listName;
                static_cast<const MultiplayerInspectorViewModel*>( pData->UserData )->collectCompletions( line, listName );
                if ( listName.empty() )
                    return 0;
                string common = listName.front();
                for ( const string& name : listName )
                {
                    const size_t shorter = MathUtil::min( common.size(), name.size() );
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
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerInspectorTab
     * @brief Inspector 탭 — 전용 서버의 하이어라키(`scene.tree`) · 컴포넌트 값(`object.dump`)을 보이고 개발 명령 입력줄을 디버그 채널로 보냅니다(서버 디버깅 S3).
     * @details 언리얼 PIE 의 서버 월드 아웃라이너에 해당. 세션이 띄운 전용 서버에는 세션이 고른 포트 · 토큰으로 붙고, 이미 떠 있는 서버에는 주소 · 토큰으로 붙는다.
     *          값은 `MultiplayerInspectorViewModel::gather` 가 모으고 붙기 · 읽기 · 입력줄은 그 명령이다(MVVM 계약). 리슨 호스트(App)는 채널을 열지 않아 이 탭의 대상이 아니다.
     */
    class MultiplayerInspectorTab final : public IMultiplayerTab
    {
    public:
        MultiplayerInspectorTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 연결 줄 — 세션 서버에 붙기 · 주소로 붙기 · 끊기 · 새로 읽기입니다. */
        void drawConnection( const MultiplayerSessionViewState& session );
        /** @brief 하이어라키 표와 고른 오브젝트의 값입니다. */
        void drawTreeAndValues( float32 height );
        /** @brief 입력줄 결과와 입력줄입니다. */
        void drawConsole();

    private:
        MultiplayerInspectorViewModel           _viewModel;
        MultiplayerInspectorViewState           _viewState;
        fixed_string<constant::buffer::kMax64>  _address;                 ///< 주소로 붙기 — `127.0.0.1:<포트>`(편집 필드)
        fixed_string<constant::buffer::kMax128> _token;                   ///< 주소로 붙기 — 토큰(편집 필드)
        fixed_string<constant::buffer::kMax512> _commandLine;             ///< 입력줄(편집 필드)
        uint8                                   _bScrollConsoleToEnd : 1; ///< 새 결과가 왔으니 다음 그리기에서 끝으로 스크롤
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerInspectorTab::MultiplayerInspectorTab()
        : _viewModel{}
        , _viewState{}
        , _address{}
        , _token{}
        , _commandLine{}
        , _bScrollConsoleToEnd{ SW_FALSE }
    {
    }

    void MultiplayerInspectorTab::drawConnection( const MultiplayerSessionViewState& session )
    {
        const bool bSessionChannel = session._bRunning != SW_FALSE && session._debugChannelPort != 0;
        if ( _viewState._bConnected != SW_FALSE )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "ConnectedTo", TextArgumentList().addText( "address", _viewState._remoteAddress ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Disconnect" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Disconnect" ) ) )
                _viewModel.disconnect();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Refresh" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Refresh" ) ) )
                _viewModel.refresh();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "InspectorRefresh" ) );
        }
        else if ( bSessionChannel )
        {
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "SessionServerDebugChannel", TextArgumentList().addInteger( "debugChannelPort", static_cast<int64>( session._debugChannelPort ) ) ).c_str() );
            ImGui::SameLine();
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Connect" ) ) )
                _viewModel.connectToSession();
        }
        else
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "InspectorNoChannel" ) );
        }
        if ( _viewState._bConnected == SW_FALSE && _viewState._lastError.empty() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "LastError", TextArgumentList().addText( "error", _viewState._lastError ) ).c_str() );

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "AttachByAddress" ) ) )
        {
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "InspectorAttach" ) );
            ImGui::InputTextWithHint( "##InspectorAddress", "127.0.0.1:<port>", _address.data(), _address.capacity() );
            ImGui::SameLine();
            ImGui::InputTextWithHint( "##InspectorToken", "token", _token.data(), _token.capacity(), ImGuiInputTextFlags_Password );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Attach" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Attach" ) ) )
                _viewModel.attachByAddress( _address.c_str(), _token.c_str() );
        }
    }

    void MultiplayerInspectorTab::drawTreeAndValues( float32 height )
    {
        const float32 treeWidth = ImGui::GetContentRegionAvail().x * MultiplayerInspectorTabInternal::kTreeWidthRatio;
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
                for ( const MultiplayerInspectorObjectRow& row : _viewState._listObjectRow )
                {
                    ImGui::TableNextColumn();
                    const bool bNamed    = row._bNamed != SW_FALSE;
                    const bool bSelected = bNamed && row._name == _viewState._selectedName;
                    ImGui::PushID( static_cast<int32>( row._objectID ) );
                    if ( row._bActive == SW_FALSE )
                        ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
                    if ( ImGui::Selectable( row._name.c_str(), bSelected, ImGuiSelectableFlags_SpanAllColumns ) && bNamed )
                        pickedName = row._name;
                    if ( row._bActive == SW_FALSE )
                        ImGui::PopStyleColor();
                    ImGui::PopID();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._parentName.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( row._components.c_str() );
                }
                ImGui::EndTable();
                if ( pickedName.empty() == false )
                    _viewModel.selectObject( pickedName ); // 값은 다음 프레임에 모은다
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if ( ImGui::BeginChild( "##InspectorValues", ImVec2( 0.0f, height ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            if ( _viewState._selectedName.empty() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "PickAnObjectOnTheLeft" ) );
            else
                ImGui::TextUnformatted( _viewState._dumpText.c_str() );
        }
        ImGui::EndChild();
    }

    void MultiplayerInspectorTab::drawConsole()
    {
        const float32 commandHeight = ImGui::GetFrameHeightWithSpacing();
        if ( ImGui::BeginChild( "##InspectorConsole", ImVec2( 0.0f, -commandHeight ), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            for ( const string& line : _viewState._listConsoleLine )
            {
                ImGui::TextUnformatted( line.c_str() );
            }
            if ( _bScrollConsoleToEnd )
                ImGui::SetScrollHereY( 1.0f );
            _bScrollConsoleToEnd = SW_FALSE;
        }
        ImGui::EndChild();
        constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion;
        ImGui::SetNextItemWidth( -1.0f );
        const bool bEntered = ImGui::InputTextWithHint( "##InspectorCommand", SW_LOCKEY( "EditorMultiplayer", "InspectorConsoleHint" ), _commandLine.data(),
                                                        _commandLine.capacity(), kFlags, &MultiplayerInspectorTabInternal::onCommandLineEdit, &_viewModel );
        if ( bEntered )
        {
            _viewModel.execute( _commandLine.c_str() ); // 결과(오류 포함)는 결과 줄에 남는다 — 다음 프레임에 모은다
            _commandLine.clear();
            _bScrollConsoleToEnd = SW_TRUE;
            ImGui::SetKeyboardFocusHere( -1 );
        }
    }

    void MultiplayerInspectorTab::drawTab( const MultiplayerTabContext& context )
    {
        _viewModel.gather( _viewState );
        drawConnection( *context._pState );
        if ( _viewState._bConnected == SW_FALSE )
            return;
        ImGui::Separator();
        const float32 treeHeight = ImGui::GetContentRegionAvail().y * MultiplayerInspectorTabInternal::kTreeHeightRatio;
        drawTreeAndValues( treeHeight );
        drawConsole();
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerInspectorTab, "inspector", 250, "Inspector" );
} // namespace sw::editor
