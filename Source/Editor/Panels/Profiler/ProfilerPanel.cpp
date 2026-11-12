#include "pch.h"

#include "Editor/Panels/Profiler/ProfilerPanel.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "DevTools/Profiling/Tracy/TracyProfilerBackend.h"
#include "DevTools/RenderDoc/RenderDocCapture.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ProfilerPanelInternal
        {
            static constexpr Color kTimelineLabel{ Color::fromBytes( 0, 0, 0, 255 ) };
            static constexpr Color kFrameBoundary{ Color::fromBytes( 255, 255, 255, 160 ) };

            static constexpr Color kFrameOffsetText{ 1.0f, 0.8f, 0.25f, 1.0f };
            static constexpr Color kCallThreadText{ 0.6f, 0.8f, 1.0f, 1.0f };
            template <uint32 N>
            static void formatBytes( uint64 bytes, fixed_string<N>& out )
            {
                if ( bytes < constant::memory::kBytesPerKilobyte )
                    formatstring( out.data(), out.capacity(), "%# B", bytes );

                else if ( bytes < constant::memory::kBytesPerMegabyte )
                    formatstring( out.data(), out.capacity(), "%# KB", Fmt( static_cast<float64>( bytes ) / constant::memory::kBytesPerKilobyte, Format().precision( 2 ) ) );
                else if ( bytes < constant::memory::kBytesPerGigabyte )
                    formatstring( out.data(), out.capacity(), "%# MB", Fmt( static_cast<float64>( bytes ) / constant::memory::kBytesPerMegabyte, Format().precision( 2 ) ) );
                else
                    formatstring( out.data(), out.capacity(), "%# GB", Fmt( static_cast<float64>( bytes ) / ( constant::memory::kBytesPerGigabyte ), Format().precision( 2 ) ) );
            }

            /** @brief 아는 값은 `formatBytes` 로, 드라이버가 응답하지 않은 값은 "Unknown" 으로 표시합니다. */
            template <uint32 N>
            static void formatKnownBytes( uint64 bytes, bool bKnown, fixed_string<N>& out )
            {
                if ( bKnown == false )
                {
                    formatstring( out.data(), out.capacity(), "Unknown" );
                    return;
                }
                formatBytes( bytes, out );
            }

            /** @brief 휠 한 단계의 확대 배율입니다. 1 보다 작으면 확대입니다. */
            static constexpr float32 kTimelineWheelZoomFactor = 0.8f;
            /** @brief 스레드 이름 영역의 폭입니다. UI 단위이며 DPI 배율을 곱합니다. */
            static constexpr float32 kTimelineLabelWidth = 120.0f;

            /** @brief 슬롯마다 다른 색을 냅니다. 슬롯 번호로 색상환을 돌려 고릅니다. */
            static ImU32 computeSlotColor( uint32 slot )
            {
                constexpr uint32 kHueStepDegrees = 47;
                constexpr uint32 kDegreesPerTurn = 360;
                const float32    hue             = static_cast<float32>( ( slot * kHueStepDegrees ) % kDegreesPerTurn ) / static_cast<float32>( kDegreesPerTurn );
                float32          red{ 0.0f };
                float32          green{ 0.0f };
                float32          blue{ 0.0f };
                ImGui::ColorConvertHSVtoRGB( hue, 0.55f, 0.85f, red, green, blue );
                return ImGui::GetColorU32( EditorColorUtil::toImVec4( Color{ red, green, blue, 1.0f } ) );
            }

#if !defined( SW_SHIPPING )
            /**
             * @brief 프레임 단계 — 멀티 프로세스 재생이 Stop 뒤에 합친 트레이스를 Profiler 창 Timeline 탭에 한 번 엽니다.
             * @details 멈추는 길(툴바 · Session 탭 · `netplay.stop`)이 셋이고 툴바(GUI 층)는 이 창(Panels 층)을 include 하지 못하므로, 창이 닫혀 있어도 도는 단계에서 묻는다.
             */
            static void openMergedNetPlayTrace( EditorContext& /*context*/ )
            {
                const string mergedTracePath = ProfilerViewModel::takeMergedNetPlayTrace();
                if ( mergedTracePath.empty() == false )
                    (void)ProfilerPanel::openTraceInTimeline( EditorPanelManager::getInEditorContext(), mergedTracePath ); // 실패 이유는 그 함수가 로그에 남긴다
            }
#endif

            /** @brief 태그별 메모리 통계 표입니다. */
            static void drawMemoryTagTable( MemoryProfiler& profiler )
            {
                if ( ImGui::BeginTable( "MemoryStatsTable", 5,
                                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) == false )
                    return;
                ImGui::TableSetupColumn( "Tag" );
                ImGui::TableSetupColumn( "Current Bytes" );
                ImGui::TableSetupColumn( "Share" );
                ImGui::TableSetupColumn( "Current Count" );
                ImGui::TableSetupColumn( "Total Allocated" );
                ImGui::TableHeadersRow();

                fixed_string<constant::buffer::kMax32> arrBytesBuf;
                fixed_string<constant::buffer::kMax32> arrTotalBuf;

                // 현재 살아 있는 바이트가 큰 태그부터 표시한다. 한 번도 할당하지 않은 태그는 행을 만들지 않는다. Unknown 은 항상 만든다. 태그가 지정되지 않은 진입점의 할당량이기 때문이다.
                const uint64 liveBytes = profiler.getLiveAllocatedBytes();
                for ( const MemoryTag tag : profiler.makeTagOrderByLiveBytes() )
                {
                    const auto& stats = profiler.getStats( tag );
                    if ( tag != MemoryTag::Unknown && stats._totalAllocatedBytes == 0 )
                        continue;

                    const uint64 currentBytes = stats._currentAllocatedBytes;
                    formatBytes( currentBytes, arrBytesBuf );
                    formatBytes( stats._totalAllocatedBytes, arrTotalBuf );

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", MemoryProfiler::getMemoryTagName( tag ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", arrBytesBuf.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.1f%%", liveBytes == 0 ? 0.0 : static_cast<float64>( currentBytes ) * 100.0 / static_cast<float64>( liveBytes ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%u", static_cast<uint32>( stats._currentAllocationCount ) );
                    ImGui::TableNextColumn();

                    ImGui::Text( "%s", arrTotalBuf.c_str() );
                }
                ImGui::EndTable();
            }

            /**
             * @brief 누수 측정 창(`MemoryLeakWindow`)을 열고 닫아 `FullStacks` 보고서를 보입니다.
             * @details 기동 뒤 기준선 창은 보통 열려 있어(`TagTotals`) 두 번째 창을 열 수 없다. 그래서 단추 하나가 "열려 있으면 닫아 보고서를 받고, 곧바로 `FullStacks` 로 다시 연다"를
             *          한다 — 종료 보고(`DiagnosticsSubsystem`)가 늘 열린 창을 닫으므로 창이 없는 채로 남지 않는다. `FullStacks` 는 할당마다 스택을 잡아 느려진다(켠 뒤부터 새로 태어난 블록만 잰다).
             */
            static void drawLeakWindow( MemoryLeakWindow& leakWindow, LeakWindowReport& inoutReport )
            {
                const bool bOpen = leakWindow.isOpen();
                ImGui::TextUnformatted( bOpen ? "Leak window: open" : "Leak window: closed" );
                ImGui::SameLine();
                if ( ImGui::Button( bOpen ? "Report And Restart As FullStacks" : "Open FullStacks Window" ) )
                {
                    if ( bOpen )
                        inoutReport = leakWindow.close();
                    leakWindow.open( LeakWindowDetail::FullStacks );
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdTip", "FullStacksRecordsTheCallStackOfEvery" ) );
                if ( inoutReport._bValid == false )
                {
                    ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "NoLeakReportYet" ) );
                    return;
                }

                fixed_string<constant::buffer::kMax128> summary;
                formatstring( summary.data(), summary.capacity(), "%# B in %# blocks, %# rounds (%# blocks from other threads)", inoutReport._totalBytes, inoutReport._totalBlockCount,
                              inoutReport._roundCount, inoutReport._foreignThreadBlockCount );
                ImGui::TextUnformatted( summary.c_str() );
                if ( ImGui::BeginTable( "LeakWindowTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) == false )
                    return;
                ImGui::TableSetupColumn( "Bytes (Blocks)" );
                ImGui::TableSetupColumn( "Round" );
                ImGui::TableSetupColumn( "Tag" );
                ImGui::TableSetupColumn( "First Frame" );
                ImGui::TableHeadersRow();
                uint32 displayCount = 0;
                for ( const LeakWindowEntry& entry : inoutReport._listEntry )
                {
                    if ( displayCount++ >= LeakWindowReport::kDefaultMaxPrintedEntry )
                        break;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    fixed_string<constant::buffer::kMax64> sizeBuf;
                    formatstring( sizeBuf.data(), sizeBuf.capacity(), "%# B (%# blocks)", entry._bytes, entry._blockCount );
                    ImGui::TextUnformatted( sizeBuf.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%u", static_cast<uint32>( entry._bornRound ) );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( MemoryProfiler::getMemoryTagName( entry._tag ) );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( entry._firstFrame.empty() ? "(TagTotals window: no stack)" : entry._firstFrame.c_str() );
                }
                ImGui::EndTable();
            }

            /** @brief 살아 있는 할당이 큰 호출 스택 표입니다(최대 100 개). */
            static void drawTopCallStacks( MemoryProfiler& profiler )
            {
                vector<CallStackAllocInfo> listTopStack = profiler.getTopCallStacks();

                if ( listTopStack.empty() )
                {
                    ImGui::TextUnformatted( SW_LOCKEY( "EdText", "NoDetailedCallStackDataAvailableOrAll" ) );
                    return;
                }
                if ( ImGui::BeginTable( "CallStackTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) == false )
                    return;
                ImGui::TableSetupColumn( "Bytes (Count)" );
                ImGui::TableSetupColumn( "Call Stack" );
                ImGui::TableHeadersRow();

                int32 displayCount{ 0 };
                for ( const auto& info : listTopStack )
                {
                    if ( displayCount++ > 100 )
                        break; // 최대 100개만 표시한다

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    fixed_string<constant::buffer::kMax64> allocBuf;
                    formatstring( allocBuf.data(), allocBuf.capacity(), "%# B (%# allocs)", info._currentBytes, info._currentCount );
                    ImGui::TextUnformatted( allocBuf.c_str() );

                    ImGui::TableNextColumn();

                    // 호출 스택을 펼쳐 볼 수 있도록 트리로 만든다
                    StringBuilder<constant::buffer::kMax256> treeLabelBuilder;
                    treeLabelBuilder.appendFormat( "Stack Hash: %#", info._stack._hash );
                    string treeLabel{ treeLabelBuilder.view() };
                    if ( ImGui::TreeNode( treeLabel.c_str() ) )
                    {
                        string stackStr = CallStackCapture::symbolize( info._stack );
                        ImGui::TextUnformatted( stackStr.c_str() );
                        ImGui::TreePop();
                    }
                }
                ImGui::EndTable();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ProfilerPanel, ProfilerPanel::kPanelID, EditorPanelCategory::Core, 500 );
#if !defined( SW_SHIPPING )
    // 패널보다 앞 — 이번 프레임에 연 창이 그대로 그려진다(`EditorFrameStepOrder::kCommands` 와 같은 위치).
    SW_EDITOR_FRAME_STEP( ProfilerOpenMergedNetPlayTrace, "profiler.openMergedNetPlayTrace", EditorFrameStepOrder::kCommands + 10,
                          &ProfilerPanelInternal::openMergedNetPlayTrace );
#endif

    ProfilerPanel::ProfilerPanel()
        : IEditorPanel( false )
        , _capture{}
        , _timeline{}
        , _performance{}
        , _viewModel{}
        , _sceneState{}
        , _listScratchRow{}
        , _listScratchSeries{}
        , _arrFilter{}
        , _leakReport{}
    {
    }

    void ProfilerPanel::drawContent()
    {
        _capture.tick( ProfilerViewModel::getFrameProfiler() );

        if ( ImGui::BeginTabBar( "ProfilerTabs" ) )
        {
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "CPUScopes" ) ) )
            {
                drawScopeTab( ProfilerScopeKind::CPU );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "GPUPasses" ) ) )
            {
                drawScopeTab( ProfilerScopeKind::GPU );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Counters" ) ) )
            {
                drawScopeTab( ProfilerScopeKind::Counter );
                ImGui::EndTabItem();
            }
#if !defined( SW_SHIPPING )
            const ImGuiTabItemFlags timelineFlags = _timeline.consumeTimelineTabRequest() ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
#else
            constexpr ImGuiTabItemFlags timelineFlags = ImGuiTabItemFlags_None;
#endif
            const bool bTimelineTab = ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Timeline" ), nullptr, timelineFlags );
            EditorSelfTestMarks::record( "profiler.timeline.tab" );
            if ( bTimelineTab )
            {
                drawTimelineTab();
                ImGui::EndTabItem();
            }
            const bool bCallTreeTab = ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "CallTree" ) );
            EditorSelfTestMarks::record( "profiler.callTree.tab" );
            if ( bCallTreeTab )
            {
                drawCallTreeTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "PerformanceScene" ) ) )
            {
                drawPerformanceTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Memory" ) ) )
            {
                drawMemoryTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "GPUMemory" ) ) )
            {
                drawGPUMemoryTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void ProfilerPanel::drawScopeTab( ProfilerScopeKind kind )
    {
        drawCaptureControls();
        drawFrameGraph();
        ImGui::Separator();
        drawScopeTable( kind );
    }

    void ProfilerPanel::drawCaptureControls()
    {
        drawCaptureHistoryControls();
        drawExternalProfilerControls();
    }

    void ProfilerPanel::drawCaptureHistoryControls()
    {
        bool bCollect = _capture.isCollecting();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Collect" ), &bCollect ) )
            _capture.setCollect( ProfilerViewModel::getFrameProfiler(), bCollect );
        // 창 크기 · 모은 프레임 수는 도구줄에 늘 펼치지 않고 Collect 툴팁에 둔다 — 도구줄이 두 줄로 넘쳤다(유니티 Profiler 도 프레임 수는 호버로 본다).
        const ProfilerScopeHistory& history        = _capture.getHistory();
        const uint64                capturedFrames = MathUtil::min( history.getCapturedFrameCount(), static_cast<uint64>( UINT32_MAX ) );
        EditorWidgets::drawTooltip( SW_LOCKEYFORMAT( "EdFormat", "FramesWindowCaptured", TextArgumentList().addInteger( "windowFrame", static_cast<int64>( history.getWindowFrame() ) ).addInteger( "capturedFrames", static_cast<int64>( capturedFrames ) ) ).c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Clear" ) ) )
            _capture.clear();
        EditorSelfTestMarks::record( "profiler.capture.clear" );

        // 정지, 스파이크, 캡처 파일을 다룬다. 유니티 Profiler 의 Record 끄기, 언리얼의 stat hitches, Save/Load 에 해당한다.
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( _capture.isPaused() ? "Resume" : "Pause" ) );
        if ( _capture.isPaused() )
        {
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Resume" ) ) )
                _capture.resume();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "ResumeCapture" ) );
        }
        else if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Pause" ) ) )
            _capture.pause();
        EditorSelfTestMarks::record( "profiler.capture.pause" );
        EditorChrome::sameLineIfFits( ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize( "Pause on spike" ).x );
        bool bPauseOnSpike = _capture.isPauseOnSpike();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "PauseOnSpike" ), &bPauseOnSpike ) )
            _capture.setPauseOnSpike( bPauseOnSpike );
        EditorSelfTestMarks::record( "profiler.capture.pauseOnSpike" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "PauseOnSpike" ) );
        EditorChrome::sameLineIfFits( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldNumberEm" ) );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldNumberEm" ) );
        EditorWidgets::dragFloat( "##spikeMs", &_capture.editSpikeThresholdMs(), 0.1f, 0.1f, 1000.0f, "%.1f ms" );
        EditorSelfTestMarks::record( "profiler.capture.spikeMs" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SpikeThreshold" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) )
            (void)_capture.saveCapture(); // 결과는 모델이 로그에 남긴다
        EditorSelfTestMarks::record( "profiler.capture.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SaveCapture" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Load" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Load" ) ) )
            (void)_capture.loadCapture(); // 결과는 모델이 로그에 남긴다
        EditorSelfTestMarks::record( "profiler.capture.load" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "LoadCapture" ) );
    }

    void ProfilerPanel::drawExternalProfilerControls()
    {
        // Tracy. 시간축 분석은 외부 뷰어가 한다. 상태 한 줄과 여는 버튼을 둔다.
        EditorWidgets::drawToolbarSeparator();
        const IProfilerBackend* pBackend = ProfilerBackend::getActiveBackend();
        if ( TracyProfilerBackend::isCompiled() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "TracyNotInThisBuild" ) );
        else if ( pBackend == nullptr )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "TracyOff" ) );
        else
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "TracyOnPortViewer", TextArgumentList().addInteger( "tracyPort", static_cast<int64>( TracyProfilerBackend::getDataPort() ) ).addText( "viewerConnected", pBackend->isViewerConnected() ? "connected" : "waiting" ) ).c_str() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "OpenTracy" ) ) );
        ImGui::BeginDisabled( TracyProfilerBackend::isCompiled() == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "OpenTracy" ) ) )
        {
            _viewModel.openTracyViewer();
        }
        ImGui::EndDisabled();
        if ( _viewModel.getTracyError().empty() == false )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", _viewModel.getTracyError().c_str() );

        // RenderDoc 으로 GPU 프레임을 캡처한다(네 백엔드 모두). 붙어 있지 않으면 버튼이 회색이고 이유를 툴팁으로 보여 준다.
        EditorWidgets::drawToolbarSeparator();
        const bool bRenderDoc = RenderDocCapture::isAvailable();
        ImGui::BeginDisabled( bRenderDoc == false );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "RenderDocCapture" ) ) )
            RenderDocCapture::triggerCapture();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "RenderDocCapture" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "OpenUI" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "OpenUI" ) ) && RenderDocCapture::launchReplayUI() == false )
            SW_LOG_WARNING_ID( "ProfilerPanel.RenderDocReplayUICould", "RenderDoc replay UI could not be started" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "RenderDocOpenUI" ) );
        ImGui::EndDisabled();
        if ( bRenderDoc == false && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", SW_LOCKEY( "EditorProfiler", "RenderDocNotAttached" ) );
    }

    void ProfilerPanel::drawTimelineToolbar( FrameProfiler& profiler, bool bRecording )
    {
        if ( ImGui::Button( bRecording ? "Stop Recording" : "Record" ) )
            _timeline.toggleRecording( profiler, _capture, ProfilerRecordingView::Timeline );
        EditorSelfTestMarks::record( "profiler.timeline.record" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "TimelineRecord" ) );

        ImGui::SameLine();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Frames" ) );
        for ( const uint32 frameChoice : ProfilerTimelineModel::kArrFrameChoice )
        {
            fixed_string<constant::buffer::kMax16> label;
            formatstring( label.data(), label.capacity(), "%#", frameChoice );
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( label.c_str() ) );
            if ( ImGui::RadioButton( label.c_str(), _timeline.getFrameCount() == frameChoice ) )
                _timeline.setFrameCount( frameChoice );
        }
        bool bFrozen = _timeline.isTimelineFrozen();
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Freeze" ) ) );
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Freeze" ), &bFrozen ) )
            _timeline.setTimelineFrozen( bFrozen );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "Freeze" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Fit" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Fit" ) ) )
            _timeline.fit();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "Fit" ) );
#if !defined( SW_SHIPPING )
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "SaveTrace" ) ) );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "SaveTrace" ) ) )
            _timeline.saveTrace( profiler );
        EditorSelfTestMarks::record( "profiler.timeline.saveTrace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SaveTrace" ) );
        ImGui::NewLine();
        drawTraceControls();
#endif
        ImGui::SameLine();
        ImGui::TextDisabled( "| wheel = zoom, drag = pan, deep analysis: Tracy" );
    }

    void ProfilerPanel::drawTimelineTab()
    {
        FrameProfiler&    profiler   = ProfilerViewModel::getFrameProfiler();
        ProfilerTimeline& timeline   = profiler.getTimeline();
        const bool        bRecording = timeline.isRecording();
        drawTimelineToolbar( profiler, bRecording );

        _timeline.collect( timeline );
        const vector<ProfilerTimelineThread>& listDraw = _timeline.getDrawThreads();
        if ( listDraw.empty() )
        {
            ImGui::TextDisabled( "%s", bRecording ? "Waiting for frames..." : "Press Record to capture the last frames per thread, or Load Trace." );
            return;
        }
        drawTimelineCanvas( listDraw, _timeline.getLoadedSlotNames() );
    }

#if !defined( SW_SHIPPING )
    bool ProfilerPanel::openTraceInTimeline( EditorPanelManager& panelManager, string_view path )
    {
        ProfilerPanel* pPanel = panelManager.findPanel<ProfilerPanel>();
        if ( pPanel == nullptr || panelManager.setPanelOpen<ProfilerPanel>( true ) == false )
        {
            SW_LOG_WARNING_ID( "ProfilerPanel.NoProfilerWindowOpen", "No Profiler window to open %# in", string( path ) );
            return false;
        }
        if ( pPanel->_timeline.loadTraceFile( path ) == false )
            return false;
        pPanel->_timeline.requestTimelineTab();
        return true;
    }

    void ProfilerPanel::drawTraceControls()
    {
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "LoadTrace" ) ) && _timeline.loadLatestTrace() == false )
            SW_LOG_WARNING_ID( "ProfilerPanel.TraceStatus", "%#", _timeline.getTraceStatus() );
        EditorSelfTestMarks::record( "profiler.timeline.loadTrace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "LoadTrace" ) );
        if ( _timeline.hasLoadedTrace() )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Unload" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Unload" ) ) )
                _timeline.unloadTrace();
            EditorSelfTestMarks::record( "profiler.timeline.unloadTrace" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "UnloadTrace" ) );
        }
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##traceSearch", _timeline.editTraceSearch(), constant::buffer::kMax128, SW_LOCKEY( "EditorProfiler", "FindScopeHint" ), 140.0f * EditorThemeUtil::getDPIScale(), false, "profiler.timeline.traceSearch" );
        if ( _timeline.getTraceStatus().empty() == false )
            ImGui::TextDisabled( "%s", _timeline.getTraceStatus().c_str() );
    }

#endif

    struct ProfilerPanel::TimelineGeometry
    {
        ImVec2  _origin{}; ///< 캔버스 왼쪽 위(화면 좌표)
        float32 _labelWidth{ 0.0f };
        float32 _laneHeight{ 0.0f };
        float32 _rowGap{ 0.0f };
        float32 _totalWidth{ 0.0f };
        float32 _totalHeight{ 0.0f };
        float32 _trackWidth{ 0.0f };
        float32 _trackLeft{ 0.0f }; ///< 이벤트가 놓이는 칸의 왼쪽(화면 좌표)
    };

    ProfilerPanel::TimelineGeometry ProfilerPanel::layoutTimeline()
    {
        const float32    dpiScale = EditorThemeUtil::getDPIScale();
        TimelineGeometry geometry;
        geometry._labelWidth = ProfilerPanelInternal::kTimelineLabelWidth * dpiScale;
        geometry._laneHeight = ImGui::GetTextLineHeight() + 4.0f * dpiScale;
        geometry._rowGap     = 2.0f * dpiScale;
        geometry._origin     = ImGui::GetCursorScreenPos();
        geometry._totalWidth = MathUtil::max( ImGui::GetContentRegionAvail().x, geometry._labelWidth + 1.0f );
        geometry._trackWidth = geometry._totalWidth - geometry._labelWidth;
        geometry._trackLeft  = geometry._origin.x + geometry._labelWidth;

        _timeline.layout( geometry._trackWidth );
        for ( const uint16 laneCount : _timeline.getLaneCounts() )
        {
            geometry._totalHeight += static_cast<float32>( laneCount ) * geometry._laneHeight + geometry._rowGap;
        }
        return geometry;
    }

    bool ProfilerPanel::handleTimelineInput( const TimelineGeometry& geometry )
    {
        ImGui::InvisibleButton( "##TimelineCanvas", ImVec2{ geometry._totalWidth, MathUtil::max( geometry._totalHeight, geometry._laneHeight ) } );
        const bool   bHovered = ImGui::IsItemHovered();
        const ImVec2 mouse    = ImGui::GetMousePos();
        if ( bHovered && EditorInputGUI::readPointer()._wheel != 0.0f )
        {
            const float32 factor = EditorInputGUI::readPointer()._wheel > 0.0f ? ProfilerPanelInternal::kTimelineWheelZoomFactor
                                                                               : 1.0f / ProfilerPanelInternal::kTimelineWheelZoomFactor;
            _timeline.zoom( mouse.x - geometry._trackLeft, geometry._trackWidth, factor );
        }
        if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            _timeline.pan( EditorInputGUI::readPointer()._delta._x, geometry._trackWidth );
        }
        return bHovered;
    }

    vector<float32> ProfilerPanel::drawTimelineRows( const vector<ProfilerTimelineThread>& listThread, const TimelineGeometry& geometry )
    {
        ImDrawList*     pDrawList = ImGui::GetWindowDrawList();
        vector<float32> listRowTop( listThread.size(), 0.0f );
        float32         rowTop = geometry._origin.y;
        for ( size_t threadIndex = 0; threadIndex < listThread.size(); ++threadIndex )
        {
            listRowTop[threadIndex] = rowTop;
            const float32 rowHeight = static_cast<float32>( _timeline.getLaneCounts()[threadIndex] ) * geometry._laneHeight;
            pDrawList->AddRectFilled( ImVec2{ geometry._origin.x, rowTop }, ImVec2{ geometry._origin.x + geometry._totalWidth, rowTop + rowHeight },
                                      ImGui::GetColorU32( ImGuiCol_FrameBg, ( threadIndex % 2 == 0 ) ? 0.6f : 0.3f ) );
            pDrawList->AddText( ImVec2{ geometry._origin.x + geometry._rowGap * 2.0f, rowTop + geometry._rowGap }, ImGui::GetColorU32( ImGuiCol_Text ),
                                listThread[threadIndex]._name.c_str() );
            rowTop += rowHeight + geometry._rowGap;
        }
        return listRowTop;
    }

    const ProfilerTimelineRect* ProfilerPanel::drawTimelineEvents( const vector<ProfilerTimelineThread>& listThread, const TimelineGeometry& geometry, const vector<float32>& listRowTop,
                                                                   const vector<string>* pLoadedSlotName, bool bHovered )
    {
        const FrameProfiler* pProfiler = &ProfilerViewModel::getFrameProfiler(); // 필수 서비스: 없으면 버그
        const ImVec2         mouse     = ImGui::GetMousePos();
        ImDrawList*          pDrawList = ImGui::GetWindowDrawList();
        pDrawList->PushClipRect( ImVec2{ geometry._trackLeft, geometry._origin.y }, ImVec2{ geometry._origin.x + geometry._totalWidth, geometry._origin.y + geometry._totalHeight }, true );
        const ProfilerTimelineRect* pHoveredRect = nullptr;
#if !defined( SW_SHIPPING )
        uint32 searchMatched{ 0 };
#endif
        for ( const ProfilerTimelineRect& rect : _timeline.getRects() )
        {
            const ProfilerTimelineEvent& event = listThread[rect._threadIndex]._listEvent[rect._eventIndex];
            const ImVec2                 minPoint{ geometry._trackLeft + rect._x0, listRowTop[rect._threadIndex] + static_cast<float32>( rect._depth ) * geometry._laneHeight };
            const ImVec2                 maxPoint{ geometry._trackLeft + rect._x1, minPoint.y + geometry._laneHeight - 1.0f };
            const utf8*                  pName     = ProfilerTimelineModel::findSlotName( pProfiler, pLoadedSlotName, event._slot );
            ImU32                        slotColor = ProfilerPanelInternal::computeSlotColor( event._slot );
#if !defined( SW_SHIPPING )
            // 검색어가 있으면 일치하는 구간만 또렷하게 두고 나머지는 흐리게 한다.
            if ( _timeline.matchesTraceSearch( pName ) )
                ++searchMatched;
            else
                slotColor = ( slotColor & 0x00FFFFFFu ) | ( 50u << IM_COL32_A_SHIFT );
#endif
            pDrawList->AddRectFilled( minPoint, maxPoint, slotColor );
            if ( rect._bShowsLabel == SW_TRUE && pName != nullptr )
            {
                pDrawList->PushClipRect( minPoint, maxPoint, true );
                pDrawList->AddText( ImVec2{ minPoint.x + geometry._rowGap, minPoint.y + geometry._rowGap }, EditorColorUtil::toImU32( ProfilerPanelInternal::kTimelineLabel ), pName );
                pDrawList->PopClipRect();
            }
            const bool bUnderMouse = bHovered && minPoint.x <= mouse.x && mouse.x <= maxPoint.x && minPoint.y <= mouse.y && mouse.y <= maxPoint.y;
            if ( bUnderMouse )
                pHoveredRect = &rect;
        }
#if !defined( SW_SHIPPING )
        _timeline.setTraceSearchMatchCount( searchMatched );
#endif
        // 프레임 경계는 세로선으로 그린다.
        for ( const uint64 frameBegin : _timeline.getFrameBegins() )
        {
            const float32 lineX = geometry._trackLeft + ProfilerTimelineLayout::computeX( frameBegin, _timeline.getViewBegin(), _timeline.getViewEnd(), geometry._trackWidth );
            pDrawList->AddLine( ImVec2{ lineX, geometry._origin.y }, ImVec2{ lineX, geometry._origin.y + geometry._totalHeight }, EditorColorUtil::toImU32( ProfilerPanelInternal::kFrameBoundary ), 1.0f );
        }
        pDrawList->PopClipRect();
        return pHoveredRect;
    }

    void ProfilerPanel::drawTimelineTooltip( const vector<ProfilerTimelineThread>& listThread, const ProfilerTimelineRect& hoveredRect, const vector<string>* pLoadedSlotName )
    {
        const FrameProfiler*         pProfiler = &ProfilerViewModel::getFrameProfiler(); // 필수 서비스: 없으면 버그
        const ProfilerTimelineEvent& event     = listThread[hoveredRect._threadIndex]._listEvent[hoveredRect._eventIndex];
        const utf8*                  pName     = ProfilerTimelineModel::findSlotName( pProfiler, pLoadedSlotName, event._slot );
        ImGui::BeginTooltip();
        ImGui::TextUnformatted( pName != nullptr ? pName : "?" );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Depth", TextArgumentList().addDecimal( "endNanos", static_cast<float64>( static_cast<float64>( event._endNanos - event._beginNanos ) / constant::time::kNanosecondsPerMicrosecond ), 1 ).addInteger( "depth", static_cast<int64>( event._depth ) ) ).c_str() );
        ImGui::TextDisabled( "%s", listThread[hoveredRect._threadIndex]._name.c_str() );
        // 같은 구간(같은 스레드, 같은 슬롯)의 횟수, 합계, 최대를 구한다. 어느 구간이 늘었는지 한눈에 볼 수 있다.
        uint32  sameCount{ 0 };
        float64 sameTotalUs{ 0.0 };
        float64 sameMaxUs{ 0.0 };
        for ( const ProfilerTimelineEvent& other : listThread[hoveredRect._threadIndex]._listEvent )
        {
            if ( other._slot != event._slot )
                continue;
            const float64 micros = static_cast<float64>( other._endNanos - other._beginNanos ) / constant::time::kNanosecondsPerMicrosecond;
            ++sameCount;
            sameTotalUs += micros;
            sameMaxUs = MathUtil::max( sameMaxUs, micros );
        }
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "SameScopeHereX", TextArgumentList().addInteger( "sameCount", static_cast<int64>( sameCount ) ).addDecimal( "sameTotalUs", static_cast<float64>( sameTotalUs ), 1 ).addDecimal( "sameMaxUs", static_cast<float64>( sameMaxUs ), 1 ) ).c_str() );
        ImGui::EndTooltip();
    }

    void ProfilerPanel::drawTimelineCanvas( const vector<ProfilerTimelineThread>& listThread, const vector<string>* pLoadedSlotName )
    {
        const TimelineGeometry geometry = layoutTimeline();
        const bool             bHovered = handleTimelineInput( geometry );

        const vector<float32>       listRowTop   = drawTimelineRows( listThread, geometry );
        const ProfilerTimelineRect* pHoveredRect = drawTimelineEvents( listThread, geometry, listRowTop, pLoadedSlotName, bHovered );
        if ( pHoveredRect != nullptr )
            drawTimelineTooltip( listThread, *pHoveredRect, pLoadedSlotName );
    }
    void ProfilerPanel::drawFrameGraph()
    {
        // 게임 스레드, 렌더 스레드, GPU 세 줄을 그린다. 같은 축(ms)으로 그려야 어느 쪽이 프레임 시간을 정하는지 보인다.
        static constexpr const utf8* kArrSeriesName[] = { "GT.Frame", "RT.Frame", "GPU.Frame" };
        float32                      maxMicro{ 1000.0f };
        for ( const utf8* pSeriesName : kArrSeriesName )
        {
            if ( _capture.getHistory().copySeries( pSeriesName, _listScratchSeries ) == false )
                continue;
            for ( const float32 value : _listScratchSeries )
            {
                maxMicro = MathUtil::max( maxMicro, value );
            }
        }
        const float32 maxMs = maxMicro / constant::time::kMicrosecondsPerMillisecond;
        for ( const utf8* pSeriesName : kArrSeriesName )
        {
            if ( _capture.getHistory().copySeries( pSeriesName, _listScratchSeries ) == false || _listScratchSeries.empty() )
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "NoSamples", TextArgumentList().addText( "seriesName", pSeriesName ) ).c_str() );
                continue;
            }
            for ( float32& value : _listScratchSeries )
            {
                value /= 1000.0f;
            }
            fixed_string<constant::buffer::kMax64> overlay;
            formatstring( overlay.data(), overlay.capacity(), "%# %# ms", pSeriesName,
                          Fmt( static_cast<float64>( _listScratchSeries.back() ), Format().precision( 2 ) ) );
            // 이름은 그래프 안 머리글(overlay)에만 둔다 — ImGui 이름표를 주면 오른쪽에 같은 이름이 한 번 더 붙고 그래프가 창 폭의 2/3 에서 멈췄다.
            fixed_string<constant::buffer::kMax64> plotID{ "##" };
            plotID.append( pSeriesName );
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::PlotLines( plotID.c_str(), _listScratchSeries.data(), static_cast<int32>( _listScratchSeries.size() ), 0, overlay.c_str(), 0.0f,
                              maxMs, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 2.5f ) ) );
            // 그래프의 한 프레임을 누르면 테이블의 "Last" 가 그 프레임 값이 된다. 유니티 Profiler 의 프레임 선택과 같다. 선택한 프레임에는 세로줄을 그린다.
            const ImVec2  plotMin    = ImGui::GetItemRectMin();
            const ImVec2  plotMax    = ImGui::GetItemRectMax();
            const float32 frameWidth = ( plotMax.x - plotMin.x ) / static_cast<float32>( _listScratchSeries.size() );
            if ( EditorInputGUI::wasItemClicked() && frameWidth > 0.0f )
            {
                const int32 clicked   = static_cast<int32>( ( EditorInputGUI::readPointer()._position._x - plotMin.x ) / frameWidth );
                const int32 lastIndex = static_cast<int32>( _listScratchSeries.size() ) - 1;
                _capture.selectFrameOffset( static_cast<uint32>( lastIndex - MathUtil::clamp( clicked, 0, lastIndex ) ) );
            }
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string{ "profiler.graph." } + pSeriesName ).c_str() );
            const uint32 frameOffset = _capture.getSelectedFrameOffset();
            if ( frameOffset > 0 )
            {
                const float32 lineX = plotMax.x - ( static_cast<float32>( frameOffset ) + 0.5f ) * frameWidth;
                ImGui::GetWindowDrawList()->AddLine( ImVec2{ lineX, plotMin.y }, ImVec2{ lineX, plotMax.y }, EditorColorUtil::toImU32( SW_THEME_COLOR( "Base", "Highlight" ) ) );
            }
        }
        if ( _capture.getSelectedFrameOffset() > 0 )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( ProfilerPanelInternal::kFrameOffsetText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "LastColumnShowsThe", TextArgumentList().addInteger( "selectedFrameOffset", static_cast<int64>( _capture.getSelectedFrameOffset() ) ) ).c_str() );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Latest" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Latest" ) ) )
                _capture.selectFrameOffset( 0 );
        }
    }

    void ProfilerPanel::drawScopeTable( ProfilerScopeKind kind )
    {
        EditorWidgets::drawSearchField( "##profilerScopeFilter", _arrFilter, constant::buffer::kMax128, SW_LOCKEY( "EditorProfiler", "FilterScopesHint" ) );
        const bool            bCounter = kind == ProfilerScopeKind::Counter;
        const utf8* const     pUnit    = bCounter ? "" : " (us)";
        const ImGuiTableFlags flags    = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##profilerScopes", 7, flags, ImGui::GetContentRegionAvail() ) == false )
            return;

        fixed_string<constant::buffer::kMax32> header;
        ImGui::TableSetupScrollFreeze( 0, 1 );
        ImGui::TableSetupColumn( "Scope", ImGuiTableColumnFlags_WidthStretch, 3.0f, static_cast<ImGuiID>( ProfilerSortColumn::Name ) );
        formatstring( header.data(), header.capacity(), "Last%#", pUnit );
        ImGui::TableSetupColumn( header.c_str(), 0, 1.0f, static_cast<ImGuiID>( ProfilerSortColumn::Last ) );
        formatstring( header.data(), header.capacity(), "Avg%#", pUnit );
        ImGui::TableSetupColumn( header.c_str(), 0, 1.0f, static_cast<ImGuiID>( ProfilerSortColumn::Average ) );
        formatstring( header.data(), header.capacity(), "p50%#", pUnit );
        ImGui::TableSetupColumn( header.c_str(), 0, 1.0f, static_cast<ImGuiID>( ProfilerSortColumn::P50 ) );
        formatstring( header.data(), header.capacity(), "p99%#", pUnit );
        ImGui::TableSetupColumn( header.c_str(), ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.0f,
                                 static_cast<ImGuiID>( ProfilerSortColumn::P99 ) );
        formatstring( header.data(), header.capacity(), "Max%#", pUnit );
        ImGui::TableSetupColumn( header.c_str(), 0, 1.0f, static_cast<ImGuiID>( ProfilerSortColumn::Max ) );
        ImGui::TableSetupColumn( "Frames", ImGuiTableColumnFlags_NoSort, 0.8f );
        ImGui::TableHeadersRow();

        // 헤더를 누르면 정렬이 바뀐다. 집계 쪽의 정렬을 그대로 쓴다. ImGui 는 열 번호만 넘긴다.
        ImGuiTableSortSpecs* pSortSpecs = ImGui::TableGetSortSpecs();
        if ( pSortSpecs != nullptr && pSortSpecs->SpecsCount > 0 )
        {
            _capture.setSort( static_cast<ProfilerSortColumn>( pSortSpecs->Specs[0].ColumnUserID ), pSortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending );
        }

        _capture.makeRows( kind, _arrFilter, _listScratchRow );
        for ( const ProfilerScopeRow& row : _listScratchRow )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( row._name.c_str() );
            const float64 arrValue[] = { row._last, row._average, row._p50, row._p99, row._max };
            for ( const float64 value : arrValue )
            {
                ImGui::TableNextColumn();
                if ( value < 0.0 )
                    ImGui::TextDisabled( "-" );
                else if ( bCounter )
                    ImGui::Text( "%.0f", value );
                else
                    ImGui::Text( "%.1f", value );
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%u", row._sampleCount );
        }
        if ( _listScratchRow.empty() )
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled( kind == ProfilerScopeKind::GPU ? "No GPU timestamps yet (this backend may not support them)" : "No samples yet" );
        }
        ImGui::EndTable();
    }

    void ProfilerPanel::drawPerformanceTab()
    {
        const ProfilerFrameTimeStats stats = _performance.sampleFrame();
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Current", TextArgumentList().addDecimal( "currentFPS", static_cast<float64>( stats._currentFPS ), 1 ).addDecimal( "currentMs", static_cast<float64>( stats._currentMs ), 2 ) ).c_str() );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "AverageAvgMinMax", TextArgumentList().addDecimal( "averageFPS", static_cast<float64>( stats._averageFPS ), 1 ).addDecimal( "averageMs", static_cast<float64>( stats._averageMs ), 2 ).addDecimal( "minMs", static_cast<float64>( stats._minMs ), 2 ).addDecimal( "maxMs", static_cast<float64>( stats._maxMs ), 2 ) ).c_str() );

        ImGui::SetNextItemWidth( -1.0f );
        ImGui::PlotLines( "##frameTime", _performance.getHistory(), static_cast<int32>( ProfilerPerformanceModel::kHistoryCount ), static_cast<int32>( _performance.getHistoryOffset() ),
                          "Frame Time (ms)", 0.0f, 33.3f, EditorChrome::measureTextExtent( EditorTextExtent::ofText( 0.0f, 4.0f ) ) );

        ImGui::Separator();

        drawSceneDistributionSection();

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "ResourceCatalogSummary" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            _performance.pollCatalog();
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ScanResources" ) ) )
                _performance.requestCatalogScan();

            for ( const EditorResourceCatalogCount& row : _performance.getCatalogCounts()._listKindCount )
            {
                ImGui::BulletText( "%s: %zu", row._pLabel, row._count );
            }
        }

        ImGui::Separator();

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "TaskManagerConcurrency" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "WorkerThreads", TextArgumentList().addInteger( "workerCount", static_cast<int64>( ProfilerViewModel::getWorkerCount() ) ) ).c_str() );
            ImGui::BulletText( "Task System: Active" );
        }
    }

    void ProfilerPanel::drawSceneDistributionSection()
    {
        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "ActiveSceneComponentDistribution" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            _viewModel.gatherScene( _sceneState ); // 접힌 섹션은 씬을 훑지 않는다 — 펼쳤을 때만 모은다
            const ProfilerSceneViewState& stats = _sceneState;
            if ( stats._bHasScene != SW_FALSE )
            {

                ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "TotalGameObjectsRoots", TextArgumentList().addInteger( "objectCount", static_cast<int64>( stats._objectCount ) ).addInteger( "rootCount", static_cast<int64>( stats._rootCount ) ) ).c_str() );
                ImGui::BulletText( "%s", SW_LOCKEYFORMAT( "EdFormat", "TotalAttachedComponents", TextArgumentList().addInteger( "componentCount", static_cast<int64>( stats._componentCount ) ) ).c_str() );

                if ( stats._listRow.empty() )
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "NoComponents" ) );
                else if ( ImGui::BeginTable( "CompDistributionTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
                {
                    ImGui::TableSetupColumn( "Component Type" );
                    ImGui::TableSetupColumn( "Active Instances" );
                    ImGui::TableHeadersRow();

                    for ( const ProfilerComponentRow& row : stats._listRow )
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted( row._typeName.c_str() );
                        ImGui::TableNextColumn();
                        ImGui::Text( "%u", row._instanceCount );
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "NoActiveScene" ) );
            }
        }

        ImGui::Separator();
    }

    void ProfilerPanel::drawGPUMemoryTab()
    {
        IRHIDevice* pDevice = ProfilerViewModel::findRHIDevice();
        if ( pDevice == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "NoRHIDevice" ) );
            return;
        }
        const RHIMemoryTracker& tracker = pDevice->getMemoryTracker();
        const RHIMemorySummary  summary = tracker.makeSummary();

        fixed_string<constant::buffer::kMax32> arrBytesBuf;
        ProfilerPanelInternal::formatBytes( summary._trackedBytes, arrBytesBuf );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Size", TextArgumentList().addText( "backendName", pDevice->getBackendName() ).addText( "sizeBasis", RHIMemoryTracker::getSizeBasisName( summary._sizeBasis ) ) ).c_str() );
        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "TrackedOfUnknownSize", TextArgumentList().addText( "bytesBuf", arrBytesBuf.c_str() ).addInteger( "unknownSizeCount", static_cast<int64>( summary._unknownSizeCount ) ) ).c_str() );

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "TrackedMemoryByKind" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "GPUMemoryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
            {
                ImGui::TableSetupColumn( "Kind" );
                ImGui::TableSetupColumn( "Live Bytes" );
                ImGui::TableSetupColumn( "Share" );
                ImGui::TableSetupColumn( "Count" );
                ImGui::TableSetupColumn( "Unknown Size" );
                ImGui::TableHeadersRow();

                // 로그 보고와 같은 순서(바이트가 큰 항목부터)와 같은 이름을 쓴다. 크기 모름은 바이트 합에 넣지 않고 따로 센다.
                for ( const RHIMemoryKind kind : tracker.makeKindOrderByLiveBytes() )
                {
                    const RHIMemoryKindStats stats = tracker.getStats( kind );
                    ProfilerPanelInternal::formatBytes( stats._liveBytes, arrBytesBuf );
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( RHIMemoryTracker::getKindName( kind ) );
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( arrBytesBuf.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.1f%%", summary._trackedBytes == 0 ? 0.0
                                                                      : static_cast<float64>( stats._liveBytes ) * 100.0 / static_cast<float64>( summary._trackedBytes ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%u", stats._liveCount );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%u", stats._unknownSizeCount );
                }
                ImGui::EndTable();
            }
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "Driver" ), ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            // 드라이버가 응답하지 않은 항목은 "Unknown" 으로 표시하고, 0 으로 대체하지 않는다.
            const RHIMemoryBudget&                 budget = summary._budget;
            fixed_string<constant::buffer::kMax32> arrUsageBuf;
            fixed_string<constant::buffer::kMax32> arrBudgetBuf;
            fixed_string<constant::buffer::kMax32> arrAvailableBuf;
            ProfilerPanelInternal::formatKnownBytes( budget._usageBytes, budget._bUsageKnown != SW_FALSE, arrUsageBuf );
            ProfilerPanelInternal::formatKnownBytes( budget._budgetBytes, budget._bBudgetKnown != SW_FALSE, arrBudgetBuf );
            ProfilerPanelInternal::formatKnownBytes( budget._availableBytes, budget._bAvailableKnown != SW_FALSE, arrAvailableBuf );
            const utf8* pScope = budget._bUsageKnown == SW_FALSE          ? ""
                               : budget._scope == RHIMemoryScope::Process ? " (this process)"
                                                                          : " (whole device, other processes included)";
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Usage", TextArgumentList().addText( "usageBuf", arrUsageBuf.c_str() ).addText( "scope", pScope ) ).c_str() );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Budget", TextArgumentList().addText( "budgetBuf", arrBudgetBuf.c_str() ) ).c_str() );
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Available", TextArgumentList().addText( "availableBuf", arrAvailableBuf.c_str() ) ).c_str() );
            if ( summary._bOutsideKnown != SW_FALSE )
            {
                const bool bNegative = summary._outsideBytes < 0;
                ProfilerPanelInternal::formatBytes( static_cast<uint64>( bNegative ? -summary._outsideBytes : summary._outsideBytes ), arrBytesBuf );
                ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "OutsideTrackerSwapChain", TextArgumentList().addText( "negative", bNegative ? "-" : "" ).addText( "bytesBuf", arrBytesBuf.c_str() ) ).c_str() );
            }
            else
                ImGui::TextUnformatted( SW_LOCKEY( "EdText", "OutsideTrackerUnknownNoPerProcessDriverUsage" ) );
        }
    }

    void ProfilerPanel::drawMemoryTab()
    {
        // 없을 수 있다: Shipping 은 진단을 켠 때만 메모리 프로파일러를 만든다(DiagnosticsSubsystem) — 안내 문구가 맞다.
        MemoryProfiler* pProfiler = ProfilerViewModel::findMemoryProfiler();
        if ( pProfiler == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "MemoryProfilerInactive" ) );
            return;
        }
        MemoryProfiler& profiler = *pProfiler;

        bool bTracking = profiler.isTrackingEnabled();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "EnableMemoryTracking" ), &bTracking ) )
            profiler.setTrackingEnabled( bTracking );

        bool bDetailed = profiler.isDetailedTrackingEnabled();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "EnableDetailedCallStackTrackingHighOverhead" ), &bDetailed ) )
            profiler.setDetailedTrackingEnabled( bDetailed );

        ImGui::Separator();

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "GlobalStatisticsByTag" ), ImGuiTreeNodeFlags_DefaultOpen ) )
            ProfilerPanelInternal::drawMemoryTagTable( profiler );

        ImGui::Separator();

        if ( bDetailed && ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "TopMemoryAllocationsByCallStack" ), ImGuiTreeNodeFlags_DefaultOpen ) )
            ProfilerPanelInternal::drawTopCallStacks( profiler );
        else if ( bDetailed == false )
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "DetailedCallStackTrackingIsDisabled" ) );

        ImGui::Separator();

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EdSection", "LeakWindowBlocksBornInTheWindow" ) ) )
            ProfilerPanelInternal::drawLeakWindow( profiler.getLeakWindow(), _leakReport );
    }
    void ProfilerPanel::drawCallTreeTab()
    {
        FrameProfiler&    profiler   = ProfilerViewModel::getFrameProfiler();
        ProfilerTimeline& timeline   = profiler.getTimeline();
        const bool        bRecording = timeline.isRecording();
        if ( ImGui::Button( bRecording ? "Stop Recording" : "Record" ) )
            _timeline.toggleRecording( profiler, _capture, ProfilerRecordingView::CallTree );
        EditorSelfTestMarks::record( "profiler.callTree.record" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "CallTreeRecord" ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Frames" ) );
        for ( const uint32 frameChoice : ProfilerTimelineModel::kArrFrameChoice )
        {
            ImGui::SameLine();
            fixed_string<constant::buffer::kMax16> label;
            formatstring( label.data(), label.capacity(), "%#", frameChoice );
            ImGui::PushID( "callTreeFrames" );
            if ( ImGui::RadioButton( label.c_str(), _timeline.getFrameCount() == frameChoice ) )
                _timeline.setFrameCount( frameChoice );
            ImGui::PopID();
        }
        bool bFrozen = _timeline.isCallTreeFrozen();
        EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Freeze##callTree" ) );
        if ( ImGui::Checkbox( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Freeze" ), "Freeze##callTree" ), &bFrozen ) )
            _timeline.setCallTreeFrozen( bFrozen );

        _timeline.updateCallTree( timeline, ImGui::GetTime() );
        const ProfilerCallTreeWindow& callTree = _timeline.getCallTree();
        if ( callTree.getRoots().empty() )
        {
            ImGui::TextDisabled( "%s", bRecording ? "Waiting for frames..." : "Press Record to fold the last frames into a call tree." );
            return;
        }

        const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##profilerCallTree", 4, flags, ImGui::GetContentRegionAvail() ) == false )
            return;
        ImGui::TableSetupScrollFreeze( 0, 1 );
        ImGui::TableSetupColumn( "Scope", ImGuiTableColumnFlags_WidthStretch, 4.0f );
        ImGui::TableSetupColumn( "Total (ms)", 0, 1.0f );
        ImGui::TableSetupColumn( "Self (ms)", 0, 1.0f );
        ImGui::TableSetupColumn( "Calls", 0, 0.8f );
        ImGui::TableHeadersRow();
        const vector<ProfilerCallNode>& listNode       = callTree.getNodes();
        const vector<string>&           listThreadName = callTree.getThreadNames();
        uint32                          shownThread    = ProfilerCallNode::kNoParent;
        for ( const uint32 root : callTree.getRoots() )
        {
            const uint32 threadIndex = listNode[root]._threadIndex;
            if ( threadIndex != shownThread && threadIndex < listThreadName.size() )
            {
                shownThread = threadIndex;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored( EditorColorUtil::toImVec4( ProfilerPanelInternal::kCallThreadText ), "%s", listThreadName[threadIndex].c_str() );
            }
            drawCallTreeNode( root );
        }
        ImGui::EndTable();
    }

    void ProfilerPanel::drawCallTreeNode( uint32 nodeIndex )
    {
        const FrameProfiler*            pProfiler = &ProfilerViewModel::getFrameProfiler(); // 필수 서비스: 없으면 버그
        const ProfilerCallTreeWindow&   callTree  = _timeline.getCallTree();
        const vector<ProfilerCallNode>& listNode  = callTree.getNodes();
        const ProfilerCallNode&         node      = listNode[nodeIndex];
        if ( node._callCount == 0 ) // 창을 벗어난 프레임에서만 불린 경로 — 노드는 남지만 보이지 않는다
            return;
        const utf8* pName = pProfiler->findScopeName( node._slot );
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID( static_cast<int32>( nodeIndex ) );
        const bool         bLeaf     = node._firstChild == ProfilerCallNode::kNoParent;
        ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanFullWidth | ( bLeaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0 );
        // 바깥쪽 두 단계는 펼쳐 둔다. 프레임과 큰 단계가 바로 보이게 하기 위해서다.
        if ( node._depth < 2 )
            treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool    bOpen      = ImGui::TreeNodeEx( pName != nullptr ? pName : "?", treeFlags );
        const float64 frameCount = static_cast<float64>( MathUtil::max( callTree.getFrameCount(), 1u ) ); // 채워지는 동안은 창 크기보다 적다
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._totalNanos ) / constant::time::kNanosecondsPerMillisecond / frameCount );
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._selfNanos ) / constant::time::kNanosecondsPerMillisecond / frameCount );
        ImGui::TableNextColumn();
        ImGui::Text( "%u", node._callCount );
        if ( bOpen && bLeaf == false )
        {
            for ( uint32 child = node._firstChild; child != ProfilerCallNode::kNoParent; child = listNode[child]._nextSibling )
            {
                drawCallTreeNode( child );
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
} // namespace sw::editor
