#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/MultiplayerInProcessViewModel.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/NetSimPlayModel.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerInProcessTabInternal
        {
            /** @brief 월드 뷰포트 한 칸의 최소 높이(픽셀)입니다. */
            static constexpr float32 kMinViewHeight = 160.0f;
        };
    } // namespace

    /**
     * @class MultiplayerInProcessTab
     * @brief Multiplayer 창의 In process 탭 — 한 프로세스 재생(`EditorNetSimSession`)을 시작 · 멈추고 월드마다 뷰포트를 나란히 그립니다(ServerDebug S8).
     * @details 세션은 에디터 서브시스템이고 탭이 그리는 프레임에 진행하므로 하니스는 **탭이 보이는 프레임에만** 진행한다(뷰포트도 보일 때만 그린다). 월드 칸을 클릭하면 그 월드가 입력 초점을 받는다.
     *          게임 목록이 비면(키트 세션을 구현한 게임이 없으면) 시작 단추가 꺼진다. 세션은 `MultiplayerInProcessViewModel` 로만 본다(MVVM 계약).
     */
    class MultiplayerInProcessTab final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override
        {
            (void)context; // 멀티 프로세스 재생의 대상 고르기는 이 탭과 무관하다
            _viewModel.gather( _viewState );
            if ( _viewState._bRunning == SW_FALSE )
            {
                drawStartControls();
                return;
            }
            const bool bStopClicked = ImGui::Button( SW_LOCKEY( "EdPlugin", "Stop" ) );
            EditorSelfTestMarks::record( "multiplayer.inprocess.stop" );
            if ( bStopClicked )
            {
                _viewModel.stop();
                return;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "TickInputFocusWorld", TextArgumentList().addInteger( "tick", static_cast<int64>( _viewState._tick ) ).addInteger( "inputFocus", static_cast<int64>( _viewState._inputFocus ) ) ).c_str() );
            _viewModel.advance( static_cast<float64>( ImGui::GetIO().DeltaTime ) );
            // 재생 중인 이 창이 포커스면 키보드는 초점 월드(게임)가 받는다 — 게임 뷰 패널과 같다. 알리지 않으면 에디터가 키를 가져가 월드가 움직이지 않는다.
            if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
                _viewModel.recordGameInputFocused();
            _viewModel.gatherWorlds( _viewState ); // 진행이 월드를 바꿨을 수 있다
            drawWorlds();
        }

    private:
        void drawStartControls()
        {
            const vector<string>& listGame = _viewState._listGameName;
            if ( listGame.empty() )
            {
                ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "NoInProcessGameIsRegisteredNetSimGameRegistryOnly" ) );
                return;
            }
            _gameIndex = MathUtil::clamp( _gameIndex, 0, static_cast<int32>( listGame.size() ) - 1 );
            if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "Game" ), "Game" ), listGame[static_cast<size_t>( _gameIndex )].c_str() ) )
            {
                for ( size_t index = 0; index < listGame.size(); ++index )
                {
                    if ( ImGui::Selectable( listGame[index].c_str(), static_cast<int32>( index ) == _gameIndex ) )
                        _gameIndex = static_cast<int32>( index );
                }
                ImGui::EndCombo();
            }
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdPluginField", "Clients" ) );
            EditorWidgets::sliderInt( "##Clients", &_clientCount, 1, MultiplayerInProcessViewModel::kMaxClients );
            const bool bStartClicked = ImGui::Button( SW_LOCKEY( "EdPlugin", "StartInProcess" ) );
            EditorSelfTestMarks::record( "multiplayer.inprocess.start" );
            if ( bStartClicked )
                _viewModel.start( _gameIndex, _clientCount ); // 실패 이유는 다음 프레임에 모은 `_lastError` 로 보인다
            if ( _viewState._lastError.empty() == false )
                ImGui::TextUnformatted( _viewState._lastError.c_str() );
        }

        void drawWorlds()
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float2 viewSize  = NetSimWorldLayout::computeViewSize( float2{ available.x, available.y }, static_cast<int32>( _viewState._listWorldIndex.size() ), ImGui::GetStyle().ItemSpacing.x,
                                                                         ImGui::GetTextLineHeightWithSpacing(), MultiplayerInProcessTabInternal::kMinViewHeight );
            for ( size_t index = 0; index < _viewState._listWorldIndex.size(); ++index )
            {
                const int32 worldIndex = _viewState._listWorldIndex[index];
                if ( index > 0 )
                    ImGui::SameLine();
                ImGui::BeginGroup();
                const bool bFocused = _viewState._inputFocus == worldIndex;
                if ( worldIndex == 0 )
                    ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "Server", TextArgumentList().addText( "focused", bFocused ? "* " : "" ) ).c_str() );
                else
                    ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdPluginFormat", "Client", TextArgumentList().addText( "focused", bFocused ? "* " : "" ).addInteger( "worldIndex", static_cast<int64>( worldIndex ) ) ).c_str() );
                EditorPreviewViewport* pViewport = _viewModel.findViewport( worldIndex );
                if ( pViewport != nullptr )
                {
                    // 카메라 끌기는 초점 월드에서만 받는다 — 다른 칸을 클릭하면 먼저 초점이 옮겨 간다.
                    pViewport->draw( viewSize, bFocused );
                    if ( EditorSelfTestMarks::isEnabled() )
                    {
                        fixed_string<constant::buffer::kMax64> text;
                        formatstring( text.data(), text.capacity(), "multiplayer.inprocess.world%#", worldIndex );
                        EditorSelfTestMarks::record( text.c_str() ); // 자동화가 클릭해 입력 초점을 옮긴다
                    }
                    if ( EditorInputGUI::wasItemClicked() && bFocused == false )
                        _viewModel.setInputFocus( worldIndex );
                }
                ImGui::EndGroup();
            }
        }

    private:
        MultiplayerInProcessViewModel _viewModel;
        MultiplayerInProcessViewState _viewState;
        int32                         _gameIndex{ 0 };
        int32                         _clientCount{ 2 };
    };

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerInProcessTab, "inprocess", 600, "In process" );
} // namespace sw::editor
