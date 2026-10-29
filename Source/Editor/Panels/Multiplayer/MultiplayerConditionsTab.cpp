#include "pch.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Commands/Play/EditorNetConditions.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/NetEmulationPresetTable.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "MultiplayerConditions" );

    namespace
    {
        struct MultiplayerConditionsTabInternal
        {
            /** @brief `netplay.stats` 의 기본 시한(초)입니다. */
            static constexpr int32 kDefaultStatsWaitSeconds = 30;
            /** @brief `netplay.waitResent` 가 보고를 다시 묻는 간격(밀리초)입니다. 재전송은 벽시계 시한으로 일어나므로 시간 간격으로 묻습니다. */
            static constexpr int64 kResentPollMilliseconds = 200;
            /** @brief 자식 프로세스에 보고를 묻는 명령입니다(`GameNetSession` 의 개발 명령). */
            static constexpr const utf8* kStatsCommand = "net.emulation stats";

            static EditorNetPlaySession* findSession()
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                return pContext != nullptr ? &pContext->getSubsystem<EditorNetPlaySession>() : nullptr;
            }

            /** @brief 돌고 있는 프로세스마다의 마지막 보고를 모읍니다. 재전송 · 버림은 합, 손실 · RTT 는 가장 큰 값입니다. */
            static NetEmulationReport sumReports( const EditorNetPlaySession& session )
            {
                NetEmulationReport total;
                for ( int32 processIndex = 0; processIndex < session.getProcessCount(); ++processIndex )
                {
                    const NetEmulationReport report = session.getProcessStatus( processIndex )._emulationReport;
                    total._sentPacketCount += report._sentPacketCount;
                    total._droppedPacketCount += report._droppedPacketCount;
                    total._resentMessageCount += report._resentMessageCount;
                    total._connectionCount += report._connectionCount;
                    total._measuredLossPercent = MathUtil::max( total._measuredLossPercent, report._measuredLossPercent );
                    total._rttMilliseconds     = MathUtil::max( total._rttMilliseconds, report._rttMilliseconds );
                }
                return total;
            }

            /** @brief 감시 값 `Editor.NetPlayResentMessages` — 프로세스들이 마지막으로 알린 재전송 신뢰 메시지의 합입니다(`netplay.stats` 가 새로 묻는다). */
            [[nodiscard]] static bool readResentMessages( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._resentMessageCount ) : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayEmulationDroppedPackets` — 회선 흉내가 버린 패킷의 합입니다. */
            [[nodiscard]] static bool readDroppedPackets( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._droppedPacketCount ) : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayMeasuredLossPercent` — 연결이 측정한 손실(%) 가운데 가장 큰 값입니다(연결 쪽 추정이 흉내를 따라가는지 본다). */
            [[nodiscard]] static bool readMeasuredLoss( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._measuredLossPercent ) : 0.0;
                return true;
            }

            /** @brief `netplay.command <server|client<N>|clients|all|<번호>> <명령>` — 그 프로세스들의 표준 입력에 한 줄을 보냅니다(회선 조절 · 끊기 · 개발 명령). */
            static bool runCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 2 )
                    return false;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                int32 firstIndex = 0;
                int32 lastIndex  = 0;
                if ( EditorNetConditionsUtil::tryParseTarget( listArgument[0], pSession->getProcessCount(), firstIndex, lastIndex ) == false )
                {
                    outReply = "no process '" + listArgument[0] + "' (server, client<N>, clients, all)";
                    return true;
                }
                string command;
                for ( size_t index = 1; index < listArgument.size(); ++index )
                {
                    if ( command.empty() == false )
                        command += ' ';
                    command += listArgument[index];
                }
                int32 sentCount = 0;
                for ( int32 processIndex = firstIndex; processIndex <= lastIndex; ++processIndex )
                {
                    sentCount += pSession->sendProcessCommand( processIndex, command ) ? 1 : 0;
                }
                outReply = formatToString( "sent to %# process(es): %#", sentCount, command );
                return true;
            }

            /** @brief 모든 프로세스에 보고를 한 번 묻고 모두 답하거나 @p timeoutMilliseconds 가 지날 때까지 기다립니다. 답한 수를 돌려줍니다. */
            static int32 askStats( EditorNetPlaySession& session, int64 timeoutMilliseconds, int32& outAskedCount )
            {
                const int32    processCount = session.getProcessCount();
                vector<uint32> listReportCount;
                listReportCount.reserve( static_cast<size_t>( processCount ) );
                outAskedCount = 0;
                for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
                {
                    listReportCount.push_back( session.getProcessStatus( processIndex )._emulationReportCount );
                    outAskedCount += session.sendProcessCommand( processIndex, kStatsCommand ) ? 1 : 0;
                }
                const int32 askedCount    = outAskedCount;
                int32       answeredCount = 0;
                session.updateUntil( timeoutMilliseconds, [&]()
                {
                    answeredCount = 0;
                    for ( int32 processIndex = 0; processIndex < processCount; ++processIndex )
                    {
                        answeredCount += session.getProcessStatus( processIndex )._emulationReportCount != listReportCount[static_cast<size_t>( processIndex )] ? 1 : 0;
                    }
                    return answeredCount >= askedCount;
                } );
                return answeredCount;
            }

            /** @brief `netplay stats from <답한 수> of <물은 수> process(es): ...` 답 줄입니다. 시나리오가 이 앞머리를 로그에서 찾습니다. */
            static string makeStatsReply( const EditorNetPlaySession& session, int32 answeredCount, int32 askedCount )
            {
                const NetEmulationReport total = sumReports( session );
                return "netplay stats from " + to_string( answeredCount ) + " of " + to_string( askedCount ) + " process(es): resent " +
                       to_string( total._resentMessageCount ) + ", dropped " + to_string( total._droppedPacketCount ) + ", measured loss " +
                       formatToString( "%#%%", static_cast<int32>( total._measuredLossPercent ) );
            }

            /**
             * @brief `netplay.stats [<초>]` — 돌고 있는 프로세스마다 `net.emulation stats` 를 묻고 모두 답할 때까지 이 프레임을 붙잡습니다.
             * @details 시나리오는 프레임 단위라 답이 언제 오는지 모른다(`netplay.wait` 와 같은 이유). 답은 감시 값 `Editor.NetPlay*` 가 읽는다.
             */
            static bool runStats( const vector<string>& listArgument, string& outReply )
            {
                int32 seconds = kDefaultStatsWaitSeconds;
                if ( listArgument.size() > 1 || ( listArgument.size() == 1 && StringUtil::parseInt( listArgument[0], seconds ) == false ) )
                    return false;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                int32       askedCount    = 0;
                const int32 answeredCount = askStats( *pSession, static_cast<int64>( seconds ) * constant::kMillisecondsPerSecond, askedCount );
                outReply                  = makeStatsReply( *pSession, answeredCount, askedCount );
                return true;
            }

            /**
             * @brief `netplay.waitResent <수> <초>`: 프로세스들이 다시 보낸 신뢰 메시지의 합이 <수> 이상이 될 때까지 보고를 거듭 물으며 이 프레임을 붙잡습니다.
             * @details 재전송은 연결의 벽시계 시한으로 일어납니다. 프레임 수로 기다리면 프레임이 빠른 구성(Release)에서 시한이 오기 전에 묻습니다.
             *          그래서 시간으로 기다립니다. 시한(<초>)을 넘기면 마지막 보고를 그대로 남기고, 뒤의 `Expect` 가 그 값으로 집니다. 기본 시한은 두지 않습니다.
             */
            static bool runWaitResent( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                int32      wantedCount = 0;
                int32      seconds     = 0;
                const bool bParsed     = StringUtil::parseInt( listArgument[0], wantedCount ) && StringUtil::parseInt( listArgument[1], seconds );
                if ( bParsed == false )
                    return false;
                EditorNetPlaySession* pSession = findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                const Deadline deadline      = Deadline::afterMilliseconds( static_cast<int64>( seconds ) * constant::kMillisecondsPerSecond );
                int32          askedCount    = 0;
                int32          answeredCount = 0;
                bool           bReached      = false;
                while ( true )
                {
                    answeredCount = askStats( *pSession, deadline.getRemainingMilliseconds(), askedCount );
                    bReached      = static_cast<int32>( sumReports( *pSession )._resentMessageCount ) >= wantedCount;
                    if ( bReached || deadline.isExpired() )
                        break;
                    pSession->updateUntil( kResentPollMilliseconds, []()
                    { return false; } );
                }
                outReply = makeStatsReply( *pSession, answeredCount, askedCount );
                if ( bReached )
                    outReply += formatToString( " (resent reached %#)", wantedCount );
                else
                    outReply += formatToString( " (resent stayed below %# for %# s)", wantedCount, seconds );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayResentMessages, "Editor.NetPlayResentMessages",
                         "Reliable messages the multiplayer session's processes resent, as last reported (netplay.stats asks again)",
                         &MultiplayerConditionsTabInternal::readResentMessages );
    SW_AUTOMATION_WATCH( editorNetPlayEmulationDroppedPackets, "Editor.NetPlayEmulationDroppedPackets",
                         "Packets the multiplayer session's line emulation dropped, as last reported", &MultiplayerConditionsTabInternal::readDroppedPackets );
    SW_AUTOMATION_WATCH( editorNetPlayMeasuredLossPercent, "Editor.NetPlayMeasuredLossPercent",
                         "Highest packet loss (%) a connection of the multiplayer session measured, as last reported", &MultiplayerConditionsTabInternal::readMeasuredLoss );
    SW_DEV_COMMAND( NetPlayCommand, "netplay.command", "netplay.command <server|client<N>|clients|all> <command>",
                    "Send a console line (net.emulation, net.disconnect, dev commands) to processes of the multiplayer session", &MultiplayerConditionsTabInternal::runCommand );
    SW_DEV_COMMAND( NetPlayStats, "netplay.stats", "netplay.stats [<seconds>]", "Ask every process of the multiplayer session for its net emulation stats and wait for the answers",
                    &MultiplayerConditionsTabInternal::runStats );
    SW_DEV_COMMAND( NetPlayWaitResent, "netplay.waitResent", "netplay.waitResent <count> <seconds>",
                    "Ask the multiplayer session's processes for stats again and again until they resent at least that many reliable messages or the seconds run out",
                    &MultiplayerConditionsTabInternal::runWaitResent );
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class MultiplayerConditionsTab
     * @brief Conditions 탭 — 고른 대상 프로세스의 회선 흉내를 실행 중에 바꿉니다(서버 디버깅 S4, 언리얼 Net Emulation · 유니티 Network Simulator 에 해당합니다).
     * @details 프리셋, 올림 · 내림 슬라이더(지연 · 흔들림 · 손실 · 대역폭), 끊기 · 재접속 · 스파이크를 그 프로세스의 표준 입력에 개발 명령(`net.emulation` …)으로 보낸다.
     *          패널 값은 프로세스마다 따로 들고, 세션이 새로 서면(포트가 바뀌면) 지운다 — 자식은 `-gv_netEmulation*` 기본값(꺼짐)으로 다시 뜬다.
     */
    class MultiplayerConditionsTab final : public IMultiplayerTab
    {
    public:
        MultiplayerConditionsTab();

        void drawTab( const MultiplayerTabContext& context ) override;

    private:
        /** @brief 프로세스 하나의 패널 값입니다. */
        struct ProcessLink
        {
            EditorNetLinkSettings _up{};
            EditorNetLinkSettings _down{};
        };

        /** @brief 대상(또는 모두)에 명령 한 줄을 보냅니다. */
        void sendToTargets( EditorNetPlaySession& session, int32 targetIndex, const string& command );
        /** @brief 한 방향 슬라이더 그룹을 그립니다. 손을 뗐을 때 바뀌었으면 true 입니다. */
        static bool drawLinkSliders( const utf8* pID, EditorNetLinkSettings& inoutSettings );
        /** @brief 대상의 마지막 보고(`net.emulation stats` 의 답)를 한 줄로 그립니다. */
        static void drawReport( const EditorNetPlayProcessStatus& status );

        vector<ProcessLink>     _listLink;
        NetEmulationPresetTable _presetTable; ///< 프리셋 단추 — 탭을 처음 그릴 때 한 번 읽는다
        float32                 _spikeSeconds;
        uint16                  _sessionPort;           ///< `_listLink` 가 맞춰진 세션(포트) — 바뀌면 지운다
        bool                    _bApplyToAll;           ///< 대상 대신 모든 프로세스에 보낸다
        bool                    _bPresetTableAttempted; ///< 테이블을 읽어 봤다(실패도 한 번만 알린다)
        bool                    _bPresetTableFailed;    ///< 테이블을 읽지 못했다 — 단추 위치에 그렇다고 적는다
    };
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerConditionsTab::MultiplayerConditionsTab()
        : _listLink{}
        , _presetTable{}
        , _spikeSeconds{ EditorNetConditionsUtil::kDefaultSpikeSeconds }
        , _sessionPort{ 0 }
        , _bApplyToAll{ false }
        , _bPresetTableAttempted{ false }
        , _bPresetTableFailed{ false }
    {
    }

    void MultiplayerConditionsTab::sendToTargets( EditorNetPlaySession& session, int32 targetIndex, const string& command )
    {
        const int32 firstIndex = _bApplyToAll ? 0 : targetIndex;
        const int32 lastIndex  = _bApplyToAll ? session.getProcessCount() - 1 : targetIndex;
        for ( int32 processIndex = firstIndex; processIndex <= lastIndex; ++processIndex )
        {
            if ( session.sendProcessCommand( processIndex, command ) == false )
                SW_LOG_WARNING( "Could not send '%#' to process %# - it has exited", command, processIndex );
        }
    }

    bool MultiplayerConditionsTab::drawLinkSliders( const utf8* pID, EditorNetLinkSettings& inoutSettings )
    {
        bool bChanged = false;
        ImGui::PushID( pID );
        ImGui::SliderFloat( "Latency (ms)", &inoutSettings._latencyMilliseconds, 0.0f, EditorNetConditionsUtil::kMaxLatencyMilliseconds, "%.0f" );
        bChanged |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat( "Jitter (ms)", &inoutSettings._jitterMilliseconds, 0.0f, EditorNetConditionsUtil::kMaxJitterMilliseconds, "%.0f" );
        bChanged |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat( "Loss (%)", &inoutSettings._lossPercent, 0.0f, 100.0f, "%.1f" );
        bChanged |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderInt( "Bandwidth (KB/s)", &inoutSettings._bandwidthKilobytesPerSecond, 0, EditorNetConditionsUtil::kMaxBandwidthKilobytesPerSecond, "%d" );
        bChanged |= ImGui::IsItemDeactivatedAfterEdit();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsBandwidth" ) );
        ImGui::PopID();
        return bChanged;
    }

    void MultiplayerConditionsTab::drawReport( const EditorNetPlayProcessStatus& status )
    {
        if ( status._emulationReportCount == 0 )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ConditionsNoStats" ) );
            return;
        }
        // 보고 줄을 그대로 보인다 — 키 이름이 곧 필드 이름이다(connections · sent · dropped · resent · lossPercent · rttMs).
        ImGui::TextUnformatted( status._emulationReport.makeLine().c_str() );
    }

    void MultiplayerConditionsTab::drawTab( const MultiplayerTabContext& context )
    {
        EditorNetPlaySession& session = *context._pSession;
        if ( context._targetProcessIndex < 0 || session.isRunning() == false )
        {
            _listLink.clear();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorMultiplayer", "ConditionsNoSession" ) );
            return;
        }
        if ( _sessionPort != session.getPort() )
        {
            _sessionPort = session.getPort();
            _listLink.clear();
        }
        _listLink.resize( static_cast<size_t>( session.getProcessCount() ) );
        const int32                      targetIndex = context._targetProcessIndex;
        ProcessLink&                     link        = _listLink[static_cast<size_t>( targetIndex )];
        const EditorNetPlayProcessStatus status      = session.getProcessStatus( targetIndex );

        ImGui::Checkbox( "All processes", &_bApplyToAll );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsApplyToAll" ) );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", _bApplyToAll ? "every process" : status._label.c_str() );

        // 프리셋 — 두 방향을 같은 값으로(왕복이 그 회선이 된다). 단추는 엔진 프리셋 테이블의 항목이다.
        if ( _bPresetTableAttempted == false )
        {
            // 테이블 경로는 엔진 기본 에셋 테이블이 원본이다. 읽지 못하면(이유는 테이블이 알린다) 단추가 없을 뿐 슬라이더는 그대로 쓴다 — 실패 표시를 남긴다.
            _bPresetTableAttempted = true;
            _bPresetTableFailed    = _presetTable.loadFromAsset( AssetID{ editor::getSubsystem<const EngineDefaultAssets>()._netEmulationPresets.get() } ) == false;
        }
        if ( _bPresetTableFailed )
            ImGui::TextDisabled( "Network presets could not be read - see the log" );
        for ( const NetEmulationPresetDef& preset : _presetTable._listPreset )
        {
            if ( &preset != &_presetTable._listPreset.front() )
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( preset._label.c_str() ) );
            if ( ImGui::Button( preset._label.c_str() ) )
            {
                const NetEmulationConditions conditions = preset.makeConditions();
                const int32                  firstIndex = _bApplyToAll ? 0 : targetIndex;
                const int32                  lastIndex  = _bApplyToAll ? session.getProcessCount() - 1 : targetIndex;
                for ( int32 processIndex = firstIndex; processIndex <= lastIndex; ++processIndex )
                {
                    _listLink[static_cast<size_t>( processIndex )]._up   = EditorNetConditionsUtil::makeLinkSettings( conditions );
                    _listLink[static_cast<size_t>( processIndex )]._down = EditorNetConditionsUtil::makeLinkSettings( conditions );
                }
                sendToTargets( session, targetIndex, EditorNetConditionsUtil::makePresetCommand( preset._name ) );
            }
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsPreset" ) );

        ImGui::Separator();
        if ( ImGui::BeginTable( "##ConditionsLinks", 2, ImGuiTableFlags_SizingStretchSame ) )
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( "Up (send)" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsUp" ) );
            if ( drawLinkSliders( "##Up", link._up ) )
                sendToTargets( session, targetIndex, EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Up, link._up ) );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( "Down (receive)" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsDown" ) );
            if ( drawLinkSliders( "##Down", link._down ) )
                sendToTargets( session, targetIndex, EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Down, link._down ) );
            ImGui::EndTable();
        }

        ImGui::Separator();
        if ( ImGui::Button( "Disconnect" ) )
            sendToTargets( session, targetIndex, "net.disconnect" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsDisconnect" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reconnect" ) );
        if ( ImGui::Button( "Reconnect" ) )
            sendToTargets( session, targetIndex, "net.reconnect" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsReconnect" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
        ImGui::InputFloat( "##SpikeSeconds", &_spikeSeconds, 0.0f, 0.0f, "%.1f s" );
        _spikeSeconds = MathUtil::max( 0.0f, _spikeSeconds );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Spike" ) );
        if ( ImGui::Button( "Spike" ) )
            sendToTargets( session, targetIndex, EditorNetConditionsUtil::makeSpikeCommand( _spikeSeconds ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsSpike" ) );

        ImGui::Separator();
        if ( ImGui::Button( "Refresh stats" ) )
            sendToTargets( session, targetIndex, MultiplayerConditionsTabInternal::kStatsCommand );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorMultiplayer", "ConditionsRefreshStats" ) );
        ImGui::SameLine();
        drawReport( status );
    }

    SW_EDITOR_MULTIPLAYER_TAB( MultiplayerConditionsTab, "conditions", 200, "Conditions" );
} // namespace sw::editor
