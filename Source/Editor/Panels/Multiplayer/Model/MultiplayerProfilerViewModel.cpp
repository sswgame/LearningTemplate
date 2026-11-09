#include "pch.h"

#include "Editor/Panels/Multiplayer/Model/MultiplayerProfilerViewModel.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetProfilerHistory.h"
#include "Editor/Panels/Multiplayer/Model/MultiplayerSessionViewModel.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"

namespace sw::editor
{
    namespace
    {
        struct MultiplayerProfilerViewModelInternal
        {
            /** @brief `netplay.profile` 의 기본 시간(초)입니다. */
            static constexpr int32 kDefaultProfileSeconds = 3;

            /** @brief 탭과 `netplay.profile` 이 같이 쓰는 시계열입니다(창을 닫았다 열어도 남는다 — 에디터 컨텍스트가 사라지면 사라진다). 에디터 범위의 상태 저장소가 소유한다. */
            static EditorNetProfilerHistory& getHistory() { return getEditorState<EditorNetProfilerHistory>(); }

            static float64 nowSeconds()
            {
                return static_cast<float64>( MonotonicClock::nowNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerSecond );
            }

            /** @brief 감시 값 `Editor.NetPlayProfilerSamples` — Profiler 시계열에 쌓인 표본 수(모든 프로세스의 합)입니다. */
            [[nodiscard]] static bool readSampleCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getHistory().computeSampleCount() );
                return true;
            }

            /** @brief 감시 값 `Editor.NetPlayProfilerPeakSentBytesPerSecond` — 시계열에서 한 프로세스가 보인 가장 큰 초당 보낸 바이트입니다. */
            [[nodiscard]] static bool readPeakSentBytes( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getHistory().computePeak( EditorNetProfilerChannel::SentBytesPerSecond ) );
                return true;
            }

            /**
             * @brief `netplay.profile [<초>]` — 그 시간 동안 Profiler 탭처럼 모든 프로세스에 `net.stats` 를 묻고 답을 시계열에 쌓으며 이 프레임을 붙잡습니다.
             * @details 탭은 보일 때만 묻는다. 시나리오는 탭을 띄우지 않으므로 이 명령으로 같은 일을 한다(`netplay.stats` 와 같은 모양). 감시 값 `Editor.NetPlayProfiler*` 가 결과를 읽는다.
             */
            static bool runProfile( const vector<string>& listArgument, string& outReply )
            {
                int32      seconds                  = kDefaultProfileSeconds;
                const bool bInvalidProfileArguments = listArgument.size() > 1 || ( listArgument.size() == 1 && StringUtil::parseInt( listArgument[0], seconds ) == false ) || seconds < 0;
                if ( bInvalidProfileArguments )
                    return false;
                EditorNetPlaySession* pSession = MultiplayerSessionViewModel::findSession();
                if ( pSession == nullptr || pSession->isRunning() == false )
                {
                    outReply = "no multiplayer session is running";
                    return true;
                }
                EditorNetProfilerHistory& history = getHistory();
                pSession->updateUntil( static_cast<int64>( seconds ) * constant::time::kMillisecondsPerSecond, [&]()
                {
                    history.update( *pSession, nowSeconds() );
                    return false; // 시한까지 모은다
                } );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( " process(es), peak sent %# B/s", static_cast<int64>( history.computePeak( EditorNetProfilerChannel::SentBytesPerSecond ) ) );
                outReply = "netplay profile has " + to_string( history.computeSampleCount() ) + " sample(s) from " + to_string( history.getTrackCount() ) +
                           string( textBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( editorNetPlayProfilerSamples, "Editor.NetPlayProfilerSamples",
                         "Samples the multiplayer Profiler tab collected from net.stats reports (every process, netplay.profile collects too)",
                         &MultiplayerProfilerViewModelInternal::readSampleCount );
    SW_AUTOMATION_WATCH( editorNetPlayProfilerPeakSentBytesPerSecond, "Editor.NetPlayProfilerPeakSentBytesPerSecond",
                         "Highest bytes per second one process of the multiplayer session sent, over the Profiler tab's history",
                         &MultiplayerProfilerViewModelInternal::readPeakSentBytes );
    SW_DEV_COMMAND( NetPlayProfile, "netplay.profile", "netplay.profile [<seconds>]",
                    "Collect the multiplayer Profiler tab's net.stats history for that many seconds (what the tab does while it is shown)",
                    &MultiplayerProfilerViewModelInternal::runProfile );
} // namespace sw::editor

namespace sw::editor
{
    MultiplayerProfilerViewModel::MultiplayerProfilerViewModel()
        : _listKindEntryScratch{}
    {
    }

    void MultiplayerProfilerViewModel::update()
    {
        MultiplayerProfilerViewModelInternal::getHistory().update( MultiplayerSessionViewModel::getSession(), MultiplayerProfilerViewModelInternal::nowSeconds() );
    }

    void MultiplayerProfilerViewModel::gather( int32 targetIndex, bool bAllProcessKinds, bool bSentKinds, MultiplayerProfilerViewState& outState )
    {
        const EditorNetProfilerHistory& history = MultiplayerProfilerViewModelInternal::getHistory();
        const EditorNetProfilerTrack*   pTrack  = targetIndex >= 0 ? history.findTrack( targetIndex ) : nullptr;
        outState._bHasData                      = pTrack != nullptr && pTrack->_series.getCount() > 0 ? SW_TRUE : SW_FALSE;
        if ( outState._bHasData == SW_FALSE )
            return;
        const EditorNetProfilerTrack&  track   = *pTrack;
        const EditorNetProfilerSample* pLatest = track._series.findLatest();
        for ( int32 channelIndex = 0; channelIndex < static_cast<int32>( EditorNetProfilerChannel::Count ); ++channelIndex )
        {
            const EditorNetProfilerChannel channel = static_cast<EditorNetProfilerChannel>( channelIndex );
            MultiplayerProfilerGraph&      graph   = outState._arrGraph[static_cast<size_t>( channelIndex )];
            track._series.copyChannel( channel, graph._listValue );
            graph._latest = pLatest != nullptr ? pLatest->_arrValue[static_cast<size_t>( channelIndex )] : 0.0f;
            graph._peak   = track._series.computePeak( channel );
        }
        _listKindEntryScratch.clear();
        const NetStatsKindDirection direction = bSentKinds ? NetStatsKindDirection::Sent : NetStatsKindDirection::Received;
        if ( bAllProcessKinds )
            history.collectKindEntries( direction, _listKindEntryScratch );
        else
            _listKindEntryScratch = track._lastReport.getKindEntries( direction );
        EditorNetProfilerSample::computeKindShares( _listKindEntryScratch, outState._listKindShare );
        outState._listReplicaRate = track._listReplicaRate;
        outState._connectionCount = track._lastReport._connectionCount;
        outState._sampleCount     = track._series.getCount();
    }

    void MultiplayerProfilerViewModel::clear()
    {
        MultiplayerProfilerViewModelInternal::getHistory().clear();
    }
} // namespace sw::editor
