#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorNetProfilerHistory.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Probe/AutomationProbe.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerProfiler" );

    namespace
    {
        struct MultiplayerProfilerTabInternal
        {
            /** @brief `netplay.profile` 의 기본 시간(초)입니다. */
            static constexpr int32 kDefaultProfileSeconds = 3;
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

            /** @brief 탭과 `netplay.profile` 이 같이 쓰는 시계열입니다(창을 닫았다 열어도 남는다 — 에디터 모듈을 다시 불러오면 사라진다). */
            static EditorNetProfilerHistory& getHistory()
            {
                static EditorNetProfilerHistory s_history;
                return s_history;
            }

            static float64 nowSeconds()
            {
                return static_cast<float64>( MonotonicClock::nowNanoseconds() ) / static_cast<float64>( constant::kNanosecondsPerSecond );
            }

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
            }

            /** @brief 탐침 `Editor.NetPlayProfilerSamples` — Profiler 시계열에 쌓인 표본 수(모든 프로세스의 합)입니다. */
            [[nodiscard]] static bool readSampleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getHistory().computeSampleCount() );
                return true;
            }

            /** @brief 탐침 `Editor.NetPlayProfilerPeakSentBytesPerSecond` — 시계열에서 한 프로세스가 보인 가장 큰 초당 보낸 바이트입니다. */
            [[nodiscard]] static bool readPeakSentBytes( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getHistory().computePeak( EditorNetProfilerChannel::SentBytesPerSecond ) );
                return true;
            }

            /**
             * @brief `netplay.profile [<초>]` — 그 시간 동안 Profiler 탭처럼 모든 프로세스에 `net.stats` 를 묻고 답을 시계열에 쌓으며 이 프레임을 붙잡습니다.
             * @details 탭은 보일 때만 묻는다. 시나리오는 탭을 띄우지 않으므로 이 명령으로 같은 일을 한다(`netplay.stats` 와 같은 모양). 탐침 `Editor.NetPlayProfiler*` 가 결과를 읽는다.
             */
            static bool runProfile( const vector<string>& listArgument, string& outReply )
            {
                int32      seconds                  = kDefaultProfileSeconds;
                const bool bInvalidProfileArguments = listArgument.size() > 1 || ( listArgument.size() == 1 && StringUtil::parseInt( listArgument[0], seconds ) == false ) || seconds < 0;
                if ( bInvalidProfileArguments )
                    return false;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                EditorNetProfilerHistory& history = getHistory();
                pSession->updateUntil( static_cast<int64>( seconds ) * constant::kMillisecondsPerSecond, [&]()
                {
                    history.update( *pSession, nowSeconds() );
                    return false; // 시한까지 모은다
                } );
                outReply = "netplay profile has " + to_string( history.computeSampleCount() ) + " sample(s) from " + to_string( history.getTrackCount() ) +
                           " process(es), peak sent " + to_string( static_cast<int64>( history.computePeak( EditorNetProfilerChannel::SentBytesPerSecond ) ) ) + " B/s";
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_PROBE( editorNetPlayProfilerSamples, "Editor.NetPlayProfilerSamples",
                         "Samples the multiplayer Profiler tab collected from net.stats reports (every process, netplay.profile collects too)",
                         &MultiplayerProfilerTabInternal::readSampleCount );
    SW_AUTOMATION_PROBE( editorNetPlayProfilerPeakSentBytesPerSecond, "Editor.NetPlayProfilerPeakSentBytesPerSecond",
                         "Highest bytes per second one process of the multiplayer session sent, over the Profiler tab's history",
                         &MultiplayerProfilerTabInternal::readPeakSentBytes );
    SW_DEV_COMMAND( NetPlayProfile, "netplay.profile", "netplay.profile [<seconds>]",
                    "Collect the multiplayer Profiler tab's net.stats history for that many seconds (what the tab does while it is shown)",
                    &MultiplayerProfilerTabInternal::runProfile );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerProfilerTab
     * @brief Profiler 탭 — 고른 대상 프로세스의 연결 · 트래픽 수치를 시간축 그래프로, 받은 메시지를 종류별 비중으로 보입니다(서버 디버깅 S5).
     * @details 언리얼 Network Insights · 유니티 Netcode Runtime Stats Monitor 에 해당. 보일 때만 0.5 초마다 자식 프로세스에 `net.stats` 를 묻는다 —
     *          자식은 이미 세는 연결 통계와 라우터 셈을 한 줄로 옮길 뿐이라 프레임에 수집이 더해지지 않는다. 시계열 · 비중 계산은 `EditorNetProfilerHistory`.
     */
    class MultiplayerProfilerTab final : public IMultiplayerTab
    {
    public:
        MultiplayerProfilerTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 대상의 시계열을 칸마다 한 장씩 그립니다. */
        void drawGraphs( const EditorNetProfilerTrack& track );
        /** @brief 메시지 종류별 비중 테이블을 그립니다. */
        void drawKindShares( const EditorNetProfilerTrack& track );

        vector<float32>            _listValueScratch;
        vector<NetStatsKindEntry>  _listKindEntryScratch;
        vector<EditorNetKindShare> _listShareScratch;
        bool                       _bAllProcessKinds; ///< 비중을 대상 대신 모든 프로세스의 합으로 본다
        bool                       _bSentKinds;       ///< 비중을 받은 메시지 대신 보낸 메시지(큐에 넣은 것)로 본다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerProfilerTab::MultiplayerProfilerTab()
        : _listValueScratch{}
        , _listKindEntryScratch{}
        , _listShareScratch{}
        , _bAllProcessKinds{ false }
        , _bSentKinds{ false }
    {
    }

    void MultiplayerProfilerTab::drawGraphs( const EditorNetProfilerTrack& track )
    {
        const EditorNetProfilerSample* pLatest = track._series.findLatest();
        const float32                  height  = ImGui::GetTextLineHeight() * MultiplayerProfilerTabInternal::kGraphHeightInLines;
        for ( int32 channelIndex = 0; channelIndex < static_cast<int32>( EditorNetProfilerChannel::Count ); ++channelIndex )
        {
            const EditorNetProfilerChannel channel = static_cast<EditorNetProfilerChannel>( channelIndex );
            track._series.copyChannel( channel, _listValueScratch );
            const float32 latest = pLatest != nullptr ? pLatest->_arrValue[static_cast<size_t>( channelIndex )] : 0.0f;
            const float32 peak   = track._series.computePeak( channel );
            utf8          arrOverlay[constant::kMaxBuffer64];
            (void)std::snprintf( arrOverlay, sizeof( arrOverlay ), "%.1f %s (peak %.1f)", static_cast<float64>( latest ), MultiplayerProfilerTabInternal::getGraphUnit( channel ),
                                 static_cast<float64>( peak ) );
            ImGui::TextUnformatted( MultiplayerProfilerTabInternal::getGraphTitle( channel ) );
            ImGui::PushID( channelIndex );
            ImGui::PlotLines( "##Graph", _listValueScratch.data(), static_cast<int32>( _listValueScratch.size() ), 0, arrOverlay, 0.0f, peak > 0.0f ? peak : 1.0f,
                              ImVec2( -1.0f, height ) );
            ImGui::PopID();
        }
    }

    void MultiplayerProfilerTab::drawKindShares( const EditorNetProfilerTrack& track )
    {
        _listKindEntryScratch.clear();
        const NetStatsKindDirection direction = _bSentKinds ? NetStatsKindDirection::Sent : NetStatsKindDirection::Received;
        if ( _bAllProcessKinds )
            MultiplayerProfilerTabInternal::getHistory().collectKindEntries( direction, _listKindEntryScratch );
        else
            _listKindEntryScratch = track._lastReport.getKindEntries( direction );
        EditorNetProfilerUtil::computeKindShares( _listKindEntryScratch, _listShareScratch );
        if ( _listShareScratch.empty() )
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
            utf8 arrNumber[constant::kMaxBuffer32];
            for ( const EditorNetKindShare& share : _listShareScratch )
            {
                ImGui::TableNextColumn();
                ImGui::Text( "0x%02X", static_cast<uint32>( share._kind ) );
                ImGui::TableNextColumn();
                (void)StringUtil::formatNumber( arrNumber, constant::kMaxBuffer32, share._messageCount );
                ImGui::TextUnformatted( arrNumber );
                ImGui::TableNextColumn();
                (void)StringUtil::formatNumber( arrNumber, constant::kMaxBuffer32, share._byteCount );
                ImGui::TextUnformatted( arrNumber );
                ImGui::TableNextColumn();
                ImGui::ProgressBar( share._share, ImVec2( -1.0f, 0.0f ) );
            }
            ImGui::EndTable();
        }
    }

    void MultiplayerProfilerTab::drawTab( const MultiplayerTabContext& context )
    {
        EditorNetPlaySession&     session = *context._pSession;
        EditorNetProfilerHistory& history = MultiplayerProfilerTabInternal::getHistory();
        history.update( session, MultiplayerProfilerTabInternal::nowSeconds() );
        const EditorNetProfilerTrack* pTrack          = history.findTrack( context._targetProcessIndex );
        const bool                    bNoProfilerData = context._targetProcessIndex < 0 || pTrack == nullptr || pTrack->_series.getCount() == 0;
        if ( bNoProfilerData )
        {
            if ( session.isRunning() )
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerWaiting" ) );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ProfilerNoSession" ) );
            return;
        }
        const EditorNetProfilerTrack& track = *pTrack;
        ImGui::Text( "%s  connections %d  samples %d", session.getProcessStatus( context._targetProcessIndex )._label.c_str(), track._lastReport._connectionCount,
                     track._series.getCount() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerSource" ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Clear" ) )
        {
            history.clear();
            return;
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerClear" ) );

        ImGui::Separator();
        drawGraphs( track );

        ImGui::Separator();
        ImGui::TextUnformatted( SW_LOCKEY( "EditorMultiplayer", "ProfilerKinds" ) );
        ImGui::SameLine();
        ImGui::Checkbox( "All processes", &_bAllProcessKinds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerKindsAllProcesses" ) );
        ImGui::SameLine();
        ImGui::Checkbox( "Sent", &_bSentKinds );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ProfilerKindsSent" ) );
        drawKindShares( track );
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerProfilerTab, "profiler", 300, "Profiler" );
} // namespace sw::editor
