#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerConditionsViewModel.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct MultiplayerConditionsViewModelInternal
        {
            /** @brief `netplay.stats` 의 기본 시한(초)입니다. */
            static constexpr int32 kDefaultStatsWaitSeconds = 30;
            /** @brief `netplay.waitResent` 가 보고를 다시 묻는 간격(밀리초)입니다. 재전송은 벽시계 시한으로 일어나므로 시간 간격으로 묻습니다. */
            static constexpr int64 kResentPollMilliseconds = 200;
            /** @brief 자식 프로세스에 보고를 묻는 명령입니다(`GameNetSession` 의 개발 명령). */
            static constexpr const utf8* kStatsCommand = "net.emulation stats";

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
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._resentMessageCount ) : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayEmulationDroppedPackets` — 회선 흉내가 버린 패킷의 합입니다. */
            [[nodiscard]] static bool readDroppedPackets( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._droppedPacketCount ) : 0.0;
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayMeasuredLossPercent` — 연결이 측정한 손실(%) 가운데 가장 큰 값입니다(연결 쪽 추정이 흉내를 따라가는지 본다). */
            [[nodiscard]] static bool readMeasuredLoss( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                outValue                             = pSession != nullptr ? static_cast<float64>( sumReports( *pSession )._measuredLossPercent ) : 0.0;
                return true;
            }

            /** @brief `netplay.command <server|client<N>|clients|all|<번호>> <명령>` — 그 프로세스들의 표준 입력에 한 줄을 보냅니다(회선 조절 · 끊기 · 개발 명령). */
            static bool runCommand( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 2 )
                    return false;
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                int32 firstIndex = 0;
                int32 lastIndex  = 0;
                if ( EditorNetLinkSettings::tryParseTarget( listArgument[0], pSession->getProcessCount(), firstIndex, lastIndex ) == false )
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
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "sent to %# process(es): %#", sentCount, command );
                outReply = string( replyBuilder.view() );
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
                const NetEmulationReport                 total = sumReports( session );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%#%%", static_cast<int32>( total._measuredLossPercent ) );
                return "netplay stats from " + to_string( answeredCount ) + " of " + to_string( askedCount ) + " process(es): resent " +
                       to_string( total._resentMessageCount ) + ", dropped " + to_string( total._droppedPacketCount ) + ", measured loss " +
                       string( textBuilder.view() );
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
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                int32       askedCount    = 0;
                const int32 answeredCount = askStats( *pSession, static_cast<int64>( seconds ) * constant::time::kMillisecondsPerSecond, askedCount );
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
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                const Deadline deadline      = Deadline::afterMilliseconds( static_cast<int64>( seconds ) * constant::time::kMillisecondsPerSecond );
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
                {
                    StringBuilder<constant::buffer::kMax256> replyPart2;
                    replyPart2.appendFormat( " (resent reached %#)", wantedCount );
                    outReply += replyPart2.c_str();
                }
                else
                {
                    StringBuilder<constant::buffer::kMax256> replyPart;
                    replyPart.appendFormat( " (resent stayed below %# for %# s)", wantedCount, seconds );
                    outReply += replyPart.c_str();
                }
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayResentMessages, "Editor.NetPlayResentMessages",
                         "Reliable messages the multiplayer session's processes resent, as last reported (netplay.stats asks again)",
                         &MultiplayerConditionsViewModelInternal::readResentMessages );
    SW_AUTOMATION_WATCH( editorNetPlayEmulationDroppedPackets, "Editor.NetPlayEmulationDroppedPackets",
                         "Packets the multiplayer session's line emulation dropped, as last reported", &MultiplayerConditionsViewModelInternal::readDroppedPackets );
    SW_AUTOMATION_WATCH( editorNetPlayMeasuredLossPercent, "Editor.NetPlayMeasuredLossPercent",
                         "Highest packet loss (%) a connection of the multiplayer session measured, as last reported", &MultiplayerConditionsViewModelInternal::readMeasuredLoss );
    SW_DEV_COMMAND( NetPlayCommand, "netplay.command", "netplay.command <server|client<N>|clients|all> <command>",
                    "Send a console line (net.emulation, net.disconnect, dev commands) to processes of the multiplayer session", &MultiplayerConditionsViewModelInternal::runCommand );
    SW_DEV_COMMAND( NetPlayStats, "netplay.stats", "netplay.stats [<seconds>]", "Ask every process of the multiplayer session for its net emulation stats and wait for the answers",
                    &MultiplayerConditionsViewModelInternal::runStats );
    SW_DEV_COMMAND( NetPlayWaitResent, "netplay.waitResent", "netplay.waitResent <count> <seconds>",
                    "Ask the multiplayer session's processes for stats again and again until they resent at least that many reliable messages or the seconds run out",
                    &MultiplayerConditionsViewModelInternal::runWaitResent );
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerConditionsViewModel::MultiplayerConditionsViewModel()
        : _listLink{}
        , _presetTable{}
        , _sessionPort{ 0 }
        , _bPresetTableAttempted{ SW_FALSE }
        , _bPresetTableFailed{ SW_FALSE }
    {
    }

    void MultiplayerConditionsViewModel::gather( optional<int32> targetIndex, MultiplayerConditionsViewState& outState )
    {
        const EditorNetPlaySession& session = MultiplayerSessionViewModel::getSession();
        outState._bActive                   = targetIndex.has_value() && *targetIndex >= 0 && session.isRunning() ? SW_TRUE : SW_FALSE;
        if ( outState._bActive == SW_FALSE )
        {
            _listLink.clear();
            return;
        }
        if ( _sessionPort != session.getPort() )
        {
            _sessionPort = session.getPort();
            _listLink.clear();
        }
        _listLink.resize( static_cast<size_t>( session.getProcessCount() ) );
        const ProcessLink* pLink = findLink( *targetIndex );
        outState._up             = pLink != nullptr ? pLink->_up : EditorNetLinkSettings{};
        outState._down           = pLink != nullptr ? pLink->_down : EditorNetLinkSettings{};

        // 프리셋 — 표 경로는 엔진 기본 에셋 표가 원본이다. 읽지 못하면(이유는 표가 알린다) 단추가 없을 뿐 슬라이더는 그대로 쓴다 — 실패 표시를 남긴다.
        if ( _bPresetTableAttempted == SW_FALSE )
        {
            _bPresetTableAttempted = SW_TRUE;
            _bPresetTableFailed    = _presetTable.loadFromAsset( AssetID{ editor::getSubsystem<const EngineDefaultAssets>()._netEmulationPresets.get() } ) ? SW_FALSE : SW_TRUE;
        }
        outState._bPresetTableFailed = _bPresetTableFailed;
        outState._listPresetLabel.resize( _presetTable._listPreset.size() );
        for ( size_t index = 0; index < _presetTable._listPreset.size(); ++index )
        {
            outState._listPresetLabel[index] = _presetTable._listPreset[index]._label;
        }
    }

    MultiplayerConditionsViewModel::ProcessLink* MultiplayerConditionsViewModel::findLink( int32 targetIndex )
    {
        return targetIndex >= 0 && static_cast<size_t>( targetIndex ) < _listLink.size() ? &_listLink[static_cast<size_t>( targetIndex )] : nullptr;
    }

    void MultiplayerConditionsViewModel::setLink( int32 targetIndex, const EditorNetLinkSettings& up, const EditorNetLinkSettings& down )
    {
        ProcessLink* pLink = findLink( targetIndex );
        if ( pLink == nullptr )
            return;
        pLink->_up   = up;
        pLink->_down = down;
    }

    void MultiplayerConditionsViewModel::sendLink( EditorNetLinkDirection direction, int32 targetIndex, bool bApplyToAll )
    {
        const ProcessLink* pLink = findLink( targetIndex );
        if ( pLink == nullptr )
            return;
        sendToTargets( targetIndex, bApplyToAll, EditorNetLinkSettings::makeLinkCommand( direction, direction == EditorNetLinkDirection::Down ? pLink->_down : pLink->_up ) );
    }

    void MultiplayerConditionsViewModel::applyPreset( size_t presetIndex, int32 targetIndex, bool bApplyToAll )
    {
        if ( presetIndex >= _presetTable._listPreset.size() )
            return;
        const NetEmulationPresetDef& preset     = _presetTable._listPreset[presetIndex];
        const NetEmulationConditions conditions = preset.makeConditions();
        const int32                  firstIndex = bApplyToAll ? 0 : targetIndex;
        const int32                  lastIndex  = bApplyToAll ? static_cast<int32>( _listLink.size() ) - 1 : targetIndex;
        for ( int32 processIndex = firstIndex; processIndex <= lastIndex; ++processIndex )
        {
            ProcessLink* pLink = findLink( processIndex );
            if ( pLink == nullptr )
                continue;
            pLink->_up   = EditorNetLinkSettings::makeLinkSettings( conditions );
            pLink->_down = EditorNetLinkSettings::makeLinkSettings( conditions );
        }
        sendToTargets( targetIndex, bApplyToAll, EditorNetLinkSettings::makePresetCommand( preset._name ) );
    }

    void MultiplayerConditionsViewModel::disconnect( int32 targetIndex, bool bApplyToAll )
    {
        sendToTargets( targetIndex, bApplyToAll, "net.disconnect" );
    }

    void MultiplayerConditionsViewModel::reconnect( int32 targetIndex, bool bApplyToAll )
    {
        sendToTargets( targetIndex, bApplyToAll, "net.reconnect" );
    }

    void MultiplayerConditionsViewModel::spike( float32 seconds, int32 targetIndex, bool bApplyToAll )
    {
        sendToTargets( targetIndex, bApplyToAll, EditorNetLinkSettings::makeSpikeCommand( seconds ) );
    }

    void MultiplayerConditionsViewModel::refreshStats( int32 targetIndex, bool bApplyToAll )
    {
        sendToTargets( targetIndex, bApplyToAll, MultiplayerConditionsViewModelInternal::kStatsCommand );
    }

    void MultiplayerConditionsViewModel::sendToTargets( int32 targetIndex, bool bApplyToAll, const string& command )
    {
        EditorNetPlaySession& session    = MultiplayerSessionViewModel::getSession();
        const int32           firstIndex = bApplyToAll ? 0 : targetIndex;
        const int32           lastIndex  = bApplyToAll ? session.getProcessCount() - 1 : targetIndex;
        for ( int32 processIndex = firstIndex; processIndex <= lastIndex; ++processIndex )
        {
            if ( session.sendProcessCommand( processIndex, command ) == false )
                SW_LOG_WARNING_ID( "MultiplayerConditions.CouldNotSendProcess", "Could not send '%#' to process %# - it has exited", command, processIndex );
        }
    }
} // namespace sw::editor
