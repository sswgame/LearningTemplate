#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerProfilerViewModel.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MultiplayerProfilerTabInternal
        {
            /** @brief 그래프 한 장의 높이(글자 높이 배수)입니다. */
            static constexpr float32 kGraphHeightInLines = 3.0f;

            /** @brief 그래프 한 장의 제목입니다(로컬라이제이션 키 — 글 수집이 `SW_LOCKEY` 줄을 읽으므로 키를 글자 그대로 둔다). */
            static const utf8* getGraphTitle( EditorNetProfilerChannel channel )
            {
                switch ( channel )
                {
                    case EditorNetProfilerChannel::SentBytesPerSecond:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerSentBytes" );
                    case EditorNetProfilerChannel::ReceivedBytesPerSecond:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerReceivedBytes" );
                    case EditorNetProfilerChannel::SentPacketsPerSecond:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerSentPackets" );
                    case EditorNetProfilerChannel::ReceivedPacketsPerSecond:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerReceivedPackets" );
                    case EditorNetProfilerChannel::ResentMessagesPerSecond:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerResent" );
                    case EditorNetProfilerChannel::RttMilliseconds:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerRtt" );
                    case EditorNetProfilerChannel::LossPercent:
                        return SW_LOCKEY( "EditorMultiplayer", "ProfilerLoss" );
                    case EditorNetProfilerChannel::Count:
                        return "";
                }
                return "";
            }

            /** @brief 그래프 한 장의 단위입니다(오버레이 글). */
            static const utf8* getGraphUnit( EditorNetProfilerChannel channel )
            {
                switch ( channel )
                {
                    case EditorNetProfilerChannel::SentBytesPerSecond:
                    case EditorNetProfilerChannel::ReceivedBytesPerSecond:
                        return "B/s";
                    case EditorNetProfilerChannel::SentPacketsPerSecond:
                    case EditorNetProfilerChannel::ReceivedPacketsPerSecond:
                        return "pkt/s";
                    case EditorNetProfilerChannel::ResentMessagesPerSecond:
                        return "msg/s";
                    case EditorNetProfilerChannel::RttMilliseconds:
                        return "ms";
                    case EditorNetProfilerChannel::LossPercent:
                        return "%";
                    case EditorNetProfilerChannel::Count:
                        return "";
                }
                return "";
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerProfilerTab
     * @brief Profiler 탭 — 고른 대상 프로세스의 연결 · 트래픽 수치를 시간축 그래프로, 받은 메시지를 종류별 비중으로 보입니다(서버 디버깅 S5).
     * @details 언리얼 Network Insights · 유니티 Netcode Runtime Stats Monitor 에 해당. 보일 때만 0.5 초마다 자식 프로세스에 `net.stats` 를 묻는다 —
     *          자식은 이미 세는 연결 통계와 라우터 셈을 한 줄로 옮길 뿐이라 프레임에 수집이 더해지지 않는다. 수집 · 값 모으기는 `MultiplayerProfilerViewModel`(MVVM 계약).
     */
    class MultiplayerProfilerTab final : public IMultiplayerTab
    {
    public:
        MultiplayerProfilerTab();

        void drawTab( const MultiplayerTabContext& context ) override;
        void updateWhileHidden( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 대상의 시계열을 칸마다 한 장씩 그립니다. */
        void drawGraphs();
        /** @brief 메시지 종류별 비중 테이블을 그립니다. */
        void drawKindShares();
        /** @brief 복제 키트가 올린 셈(`replicas=`)의 초당 갱신 · 바이트 비중 테이블을 그립니다. */
        void drawReplicaRates();

    private:
        MultiplayerProfilerViewModel _viewModel;
        MultiplayerProfilerViewState _viewState;
        bool                         _bAllProcessKinds; ///< 비중을 대상 대신 모든 프로세스의 합으로 본다 NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        bool                         _bSentKinds;       ///< 비중을 받은 메시지 대신 보낸 메시지(큐에 넣은 것)로 본다 NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
        bool                         _bRecordHidden;    ///< 다른 탭을 보는 동안에도 `net.stats` 를 묻는다(기본 꺼짐 — 자식 로그에 2 Hz 줄이 쌓인다) NOLINT(sw-bool-flag): 주소(`&`)를 넘긴다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerProfilerTab::MultiplayerProfilerTab()
        : _viewModel{}
        , _viewState{}
        , _bAllProcessKinds{ false }
        , _bSentKinds{ false }
        , _bRecordHidden{ false }
    {
    }

    void MultiplayerProfilerTab::updateWhileHidden( const MultiplayerTabContext& context )
    {
        (void)context;
        if ( _bRecordHidden == false )
            return;
        _viewModel.update();
    }

    void MultiplayerProfilerTab::drawGraphs()
    {
        const float32 height = ImGui::GetTextLineHeight() * MultiplayerProfilerTabInternal::kGraphHeightInLines;
        for ( int32 channelIndex = 0; channelIndex < static_cast<int32>( EditorNetProfilerChannel::Count ); ++channelIndex )
        {
            const EditorNetProfilerChannel         channel = static_cast<EditorNetProfilerChannel>( channelIndex );
            const MultiplayerProfilerGraph&        graph   = _viewState._arrGraph[static_cast<size_t>( channelIndex )];
            fixed_string<constant::buffer::kMax64> overlay;
            formatstring( overlay.data(), overlay.capacity(), "%# %# (peak %#)", Fmt( graph._latest, Format().precision( 1 ) ), MultiplayerProfilerTabInternal::getGraphUnit( channel ),
                          Fmt( graph._peak, Format().precision( 1 ) ) );
            ImGui::TextUnformatted( MultiplayerProfilerTabInternal::getGraphTitle( channel ) );
            ImGui::PushID( channelIndex );
            ImGui::PlotLines( "##Graph", graph._listValue.data(), static_cast<int32>( graph._listValue.size() ), 0, overlay.c_str(), 0.0f, graph._peak > 0.0f ? graph._peak : 1.0f,
                              ImVec2( -1.0f, height ) );
            ImGui::PopID();
        }
    }

    void MultiplayerProfilerTab::drawKindShares()
    {
        if ( _viewState._listKindShare.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerNoKinds" ) );
            return;
        }
        if ( ImGui::BeginTable( "##ProfilerKinds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Kind" );
            ImGui::TableSetupColumn( "Messages" );
            ImGui::TableSetupColumn( "Bytes" );
            ImGui::TableSetupColumn( "Share" );
            ImGui::TableHeadersRow();
            utf8 arrNumber[constant::buffer::kMax32];
            for ( const EditorNetKindShare& share : _viewState._listKindShare )
            {
                ImGui::TableNextColumn();
                ImGui::Text( "0x%02X", static_cast<uint32>( share._kind ) );
                ImGui::TableNextColumn();
                (void)StringUtil::formatNumber( arrNumber, constant::buffer::kMax32, share._messageCount );
                ImGui::TextUnformatted( arrNumber );
                ImGui::TableNextColumn();
                (void)StringUtil::formatNumber( arrNumber, constant::buffer::kMax32, share._byteCount );
                ImGui::TextUnformatted( arrNumber );
                ImGui::TableNextColumn();
                ImGui::ProgressBar( share._share, ImVec2( -1.0f, 0.0f ) );
            }
            ImGui::EndTable();
        }
    }

    void MultiplayerProfilerTab::drawReplicaRates()
    {
        if ( _viewState._listReplicaRate.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerNoReplicas" ) );
            return;
        }
        if ( ImGui::BeginTable( "##ProfilerReplicas", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "Name" );
            ImGui::TableSetupColumn( "Updates/s" );
            ImGui::TableSetupColumn( "Bytes/s" );
            ImGui::TableSetupColumn( "Share" );
            ImGui::TableHeadersRow();
            for ( const EditorNetReplicaRate& rate : _viewState._listReplicaRate )
            {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( rate._name.c_str() );
                ImGui::TableNextColumn();
                ImGui::Text( "%.1f", static_cast<float64>( rate._updatesPerSecond ) );
                ImGui::TableNextColumn();
                ImGui::Text( "%.1f", static_cast<float64>( rate._bytesPerSecond ) );
                ImGui::TableNextColumn();
                ImGui::ProgressBar( rate._share, ImVec2( -1.0f, 0.0f ) );
            }
            ImGui::EndTable();
        }
    }

    void MultiplayerProfilerTab::drawTab( const MultiplayerTabContext& context )
    {
        const MultiplayerSessionViewState& session = *context._pState;
        _viewModel.update();
        _viewModel.gather( context._targetProcessIndex, _bAllProcessKinds, _bSentKinds, _viewState );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "RecordWhileHidden" ), &_bRecordHidden );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerRecordHidden" ) );
        if ( _viewState._bHasData == SW_FALSE )
        {
            if ( session._bRunning != SW_FALSE )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerWaiting" ) );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerNoSession" ) );
            return;
        }
        // 대상 번호는 모은 프로세스 목록에서 왔다(창의 대상 고르기가 그 수로 맞춘다). 자료가 있으면 대상도 있다(`gather`).
        const int32   targetIndex = *context._targetProcessIndex;
        const string& label       = session._listProcess[static_cast<size_t>( targetIndex )]._label;
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "ConnectionsSamples", TextArgumentList().addText( "label", label.c_str() ).addInteger( "connectionCount", static_cast<int64>( _viewState._connectionCount ) ).addInteger( "seriesCount", static_cast<int64>( _viewState._sampleCount ) ) ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerSource" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Clear" ) ) )
        {
            _viewModel.clear();
            return;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerClear" ) );

        ImGui::Separator();
        drawGraphs();

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EditorMultiplayer", "ProfilerKinds" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "AllProcesses" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "AllProcesses" ), &_bAllProcessKinds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerKindsAllProcesses" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Sent" ) ) );
        ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Sent" ), &_bSentKinds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerKindsSent" ) );
        drawKindShares();

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EditorMultiplayer", "ProfilerReplicas" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerReplicasHelp" ) );
        drawReplicaRates();
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerProfilerTab, "profiler", 300, "Profiler" );
} // namespace sw::editor
