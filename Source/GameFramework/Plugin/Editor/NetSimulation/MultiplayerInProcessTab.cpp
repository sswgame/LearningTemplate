#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorPreviewViewport.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Plugin/Editor/NetSimulation/EditorNetSimSession.h"

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
     * @details 탭이 세션을 가지므로 하니스는 **탭이 보이는 프레임에만** 진행한다(뷰포트도 보일 때만 그린다). 월드 칸을 클릭하면 그 월드가 입력 초점을 받는다.
     *          게임 목록이 비면(키트 세션을 구현한 게임이 없으면) 시작 단추가 꺼진다.
     */
    class MultiplayerInProcessTab final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override
        {
            (void)context; // 멀티 프로세스 재생의 대상 고르기는 이 탭과 무관하다
            const vector<const NetSimGameEntry*>& listGame = NetSimGameRegistry::getGames();
            if ( _session.isRunning() == false )
            {
                drawStartControls( listGame );
                return;
            }
            const bool bStopClicked = ImGui::Button( "Stop" );
            EditorSelfTestMarks::note( "multiplayer.inprocess.stop" );
            if ( bStopClicked )
            {
                _session.stop();
                return;
            }
            ImGui::SameLine();
            ImGui::Text( "tick %u  input focus: world %d", _session.getHarness()->getTick(), _session.getInputFocus() );
            (void)_session.update( static_cast<float64>( ImGui::GetIO().DeltaTime ) );
            // 재생 중인 이 창이 포커스면 키보드는 초점 월드(게임)가 받는다 — 게임 뷰 패널과 같다. 알리지 않으면 에디터가 키를 가져가 월드가 움직이지 않는다.
            if ( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) )
                editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().noteGameInputSurfaceFocused();
            drawWorlds();
        }

    private:
        void drawStartControls( const vector<const NetSimGameEntry*>& listGame )
        {
            if ( listGame.empty() )
            {
                ImGui::TextUnformatted( "No in-process game is registered (NetSimGameRegistry) - only games with a kit session (INetSimSession) can run here." );
                return;
            }
            _gameIndex = MathUtil::clamp( _gameIndex, 0, static_cast<int32>( listGame.size() ) - 1 );
            if ( ImGui::BeginCombo( "Game", listGame[static_cast<size_t>( _gameIndex )]->_name.c_str() ) )
            {
                for ( size_t index = 0; index < listGame.size(); ++index )
                {
                    if ( ImGui::Selectable( listGame[index]->_name.c_str(), static_cast<int32>( index ) == _gameIndex ) )
                        _gameIndex = static_cast<int32>( index );
                }
                ImGui::EndCombo();
            }
            ImGui::SliderInt( "Clients", &_clientCount, 1, EditorNetSimSession::kMaxClients );
            const bool bStartClicked = ImGui::Button( "Start in process" );
            EditorSelfTestMarks::note( "multiplayer.inprocess.start" );
            if ( bStartClicked )
            {
                EditorNetSimSettings settings;
                settings._gameName    = listGame[static_cast<size_t>( _gameIndex )]->_name;
                settings._clientCount = _clientCount;
                _lastError.clear();
                if ( _session.start( settings, _lastError ) )
                    (void)_session.setInputFocus( 1 ); // 첫 클라이언트가 입력을 받으며 시작한다
            }
            if ( _lastError.empty() == false )
                ImGui::TextUnformatted( _lastError.c_str() );
        }

        void drawWorlds()
        {
            _session.collectWorldIndices( _listWorldIndex );
            const int32   columnCount = MathUtil::max( 1, static_cast<int32>( _listWorldIndex.size() ) );
            const ImVec2  available   = ImGui::GetContentRegionAvail();
            const float32 width       = MathUtil::max( 1.0f, available.x / static_cast<float32>( columnCount ) - ImGui::GetStyle().ItemSpacing.x );
            const float32 height      = MathUtil::max( MultiplayerInProcessTabInternal::kMinViewHeight, available.y - ImGui::GetTextLineHeightWithSpacing() * 2.0f );
            for ( size_t index = 0; index < _listWorldIndex.size(); ++index )
            {
                const int32 worldIndex = _listWorldIndex[index];
                if ( index > 0 )
                    ImGui::SameLine();
                ImGui::BeginGroup();
                const bool bFocused = _session.getInputFocus() == worldIndex;
                if ( worldIndex == 0 )
                    ImGui::Text( "%sServer", bFocused ? "* " : "" );
                else
                    ImGui::Text( "%sClient %d", bFocused ? "* " : "", worldIndex );
                EditorPreviewViewport* pViewport = _session.findViewport( worldIndex );
                if ( pViewport != nullptr )
                {
                    // 카메라 끌기는 초점 월드에서만 받는다 — 다른 칸을 클릭하면 먼저 초점이 옮겨 간다.
                    pViewport->draw( float2{ width, height }, bFocused );
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::note( ( "multiplayer.inprocess.world" + to_string( worldIndex ) ).c_str() ); // 자동화가 클릭해 입력 초점을 옮긴다
                    if ( EditorInputGUI::wasItemClicked() && bFocused == false )
                        (void)_session.setInputFocus( worldIndex );
                }
                ImGui::EndGroup();
            }
        }

        EditorNetSimSession _session;
        vector<int32>       _listWorldIndex;
        string              _lastError;
        int32               _gameIndex{ 0 };
        int32               _clientCount{ 2 };
    };

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerInProcessTab, "inprocess", 600, "In process" );
} // namespace sw::editor
