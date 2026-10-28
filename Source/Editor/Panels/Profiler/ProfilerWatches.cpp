/**
 * @file ProfilerWatchs.cpp
 * @brief 프로파일러 패널의 자동화 감시 값입니다 — `Editor.Profiler*`.
 * @details 감시 값 레지스트리(`Engine/Automation/Watch`)가 기능 층보다 아래라 기능이 자기 감시 값을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorWatchSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Profiler/ProfilerPanel.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 프로파일러 패널 값 하나를 읽습니다. 열려 있지 않으면 값을 내지 않습니다. */
        [[nodiscard]] bool readProfilerPanelValue( uint32 which, float64& outValue )
        {
            EditorContext*       pContext = getWatchContext( __func__ );
            const ProfilerPanel* pPanel   = pContext != nullptr ? pContext->getSubsystem<EditorPanelManager>().findPanel<ProfilerPanel>() : nullptr;
            if ( pPanel == nullptr || pPanel->isOpen() == false )
                return false;
            switch ( which )
            {
                case 0:
                {
                    outValue = static_cast<float64>( pPanel->getCallTreeNodeCount() );
                    break;
                }
                case 1:
                {
                    outValue = static_cast<float64>( pPanel->getSelectedFrameOffset() );
                    break;
                }
                case 2:
                {
                    outValue = pPanel->isCapturePaused() ? 1.0 : 0.0;
                    break;
                }
                case 4:
                {
#if SW_PROFILER_TRACE_ENABLED
                    outValue = static_cast<float64>( pPanel->getLoadedTraceThreadCount() );
#else
                    outValue = 0.0;
#endif
                    break;
                }
                case 5:
                {
#if SW_PROFILER_TRACE_ENABLED
                    outValue = static_cast<float64>( pPanel->getTraceSearchMatchCount() );
#else
                    outValue = 0.0;
#endif
                    break;
                }
                default:
                {
                    outValue = static_cast<float64>( pPanel->getCapturedFrameCount() );
                    break;
                }
            }
            return true;
        }

        [[nodiscard]] bool readProfilerCallTreeNodes( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 0, outValue );
        }

        [[nodiscard]] bool readProfilerSelectedFrame( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 1, outValue );
        }

        [[nodiscard]] bool readProfilerCapturePaused( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 2, outValue );
        }

        [[nodiscard]] bool readProfilerCapturedFrames( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 3, outValue );
        }

        [[nodiscard]] bool readProfilerLoadedTraceThreads( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 4, outValue );
        }

        [[nodiscard]] bool readProfilerTraceSearchMatches( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            return readProfilerPanelValue( 5, outValue );
        }

        /** @brief 프로파일러 타임라인이 최근 4프레임에 이벤트를 남긴 스레드 수입니다. 녹화를 켜지 않았으면 0 입니다. */
        [[nodiscard]] bool readProfilerTimelineThreadCount( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            constexpr uint32     kRecentFrameCount = 4;
            const FrameProfiler* pProfiler         = editor::findSubsystem<FrameProfiler>();
            if ( pProfiler == nullptr ) // 필수 서비스: 없으면 버그
            {
                SW_LOG_ERROR( "[Scenario] watch ProfilerTimelineThreadCount: FrameProfiler is missing" );
                return false;
            }
            outValue = static_cast<float64>( pProfiler->getTimeline().countActiveThreads( kRecentFrameCount ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( editorProfilerCallTreeNodes, "Editor.ProfilerCallTreeNodes", "Nodes of the profiler call tree drawn last", &readProfilerCallTreeNodes );
    SW_AUTOMATION_WATCH( editorProfilerSelectedFrame, "Editor.ProfilerSelectedFrame", "Frame the profiler table shows: 0 latest, n frames ago",
                         &readProfilerSelectedFrame );
    SW_AUTOMATION_WATCH( editorProfilerCapturePaused, "Editor.ProfilerCapturePaused", "1 while the profiler panel does not capture (spike, opened capture, Pause)",
                         &readProfilerCapturePaused );
    SW_AUTOMATION_WATCH( editorProfilerCapturedFrames, "Editor.ProfilerCapturedFrames", "Frames the profiler panel captured (or the opened capture holds)",
                         &readProfilerCapturedFrames );
    SW_AUTOMATION_WATCH( editorProfilerLoadedTraceThreads, "Editor.ProfilerLoadedTraceThreads", "Threads of the trace file loaded into the profiler Timeline tab (0 when none)",
                         &readProfilerLoadedTraceThreads );
    SW_AUTOMATION_WATCH( editorProfilerTraceSearchMatches, "Editor.ProfilerTraceSearchMatches",
                         "Timeline events whose scope name matches the search text, counted over the last drawn timeline (all events when the text is empty)",
                         &readProfilerTraceSearchMatches );
    SW_AUTOMATION_WATCH( editorProfilerTimelineThreadCount, "Editor.ProfilerTimelineThreadCount",
                         "Threads with profile scopes in the profiler timeline over the last 4 frames (0 while not recording)",
                         &readProfilerTimelineThreadCount );
} // namespace sw::editor
