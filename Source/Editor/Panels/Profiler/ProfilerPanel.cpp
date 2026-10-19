#include "pch.h"

#include "Editor/Panels/Profiler/ProfilerPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Commands/Dev/EditorTracyLauncher.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
#include "Engine/Profiling/ProfilerTraceExport.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ProfilerPanelInternal
        {
            static constexpr Color kTimelineLabel{ Color::fromBytes( 0, 0, 0, 255 ) };
            static constexpr Color kFrameBoundary{ Color::fromBytes( 255, 255, 255, 160 ) };

            static constexpr Color kTracyErrorText{ 1.0f, 0.6f, 0.3f, 1.0f };
            static constexpr Color kFrameOffsetText{ 1.0f, 0.8f, 0.25f, 1.0f };
            static constexpr Color kCallThreadText{ 0.6f, 0.8f, 1.0f, 1.0f };
            template <uint32 N>
            static void formatBytes( uint64 bytes, fixed_string<N>& out )
            {
                if ( bytes < constant::kBytesPerKilobyte )
                    formatstring( out.data(), out.capacity(), "%# B", bytes );

                else if ( bytes < constant::kBytesPerMegabyte )
                    formatstring( out.data(), out.capacity(), "%# KB", Fmt( static_cast<float64>( bytes ) / constant::kBytesPerKilobyte, Format().precision( 2 ) ) );
                else if ( bytes < constant::kBytesPerGigabyte )
                    formatstring( out.data(), out.capacity(), "%# MB", Fmt( static_cast<float64>( bytes ) / constant::kBytesPerMegabyte, Format().precision( 2 ) ) );
                else
                    formatstring( out.data(), out.capacity(), "%# GB", Fmt( static_cast<float64>( bytes ) / ( constant::kBytesPerGigabyte ), Format().precision( 2 ) ) );
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

            /** @brief Timeline 탭에서 선택할 수 있는 최근 프레임 수입니다. */
            static constexpr uint32 kArrTimelineFrameChoice[] = { 1, 4, 16 };
            /** @brief 휠 한 단계의 확대 배율입니다. 1 보다 작으면 확대입니다. */
            static constexpr float32 kTimelineWheelZoomFactor = 0.8f;
            /** @brief 불러온 트레이스 구간의 슬롯 번호가 시작하는 값입니다. 살아 있는 프로파일러 슬롯(`FrameProfiler::kMaxScope` 미만)과 겹치지 않습니다. */
            static constexpr uint32 kLoadedSlotBase = 1u << 20;
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

#if SW_PROFILER_TRACE_ENABLED
            /**
             * @brief 프레임 단계 — 멀티 프로세스 재생이 Stop 뒤에 합친 트레이스를 Profiler 창 Timeline 탭에 한 번 엽니다.
             * @details 멈추는 길(툴바 · Session 탭 · `netplay.stop`)이 셋이고 툴바(GUI 층)는 이 창(Panels 층)을 include 하지 못하므로, 창이 닫혀 있어도 도는 단계에서 묻는다.
             */
            static void openMergedNetPlayTrace( EditorContext& context )
            {
                const string mergedTracePath = context.getSubsystem<EditorNetPlaySession>().takeUnopenedMergedTrace();
                if ( mergedTracePath.empty() == false )
                    (void)ProfilerPanel::openTraceInTimeline( context.getSubsystem<EditorPanelManager>(), mergedTracePath ); // 실패 이유는 그 함수가 로그에 남긴다
            }
#endif
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ProfilerPanel, ProfilerPanel::kPanelID, EditorPanelCategory::Core, 500 );
#if SW_PROFILER_TRACE_ENABLED
    // 패널보다 앞 — 이번 프레임에 연 창이 그대로 그려진다(`EditorFrameStepOrder::kCommands` 와 같은 자리).
    SW_EDITOR_FRAME_STEP( ProfilerOpenMergedNetPlayTrace, "profiler.openMergedNetPlayTrace", EditorFrameStepOrder::kCommands + 10,
                          &ProfilerPanelInternal::openMergedNetPlayTrace );
#endif

    ProfilerPanel::ProfilerPanel()
        : IEditorPanel( false )
        , _arrFrameTimeHistory{}
        , _historyOffset{ 0 }
        , _catalogJob{}
        , _catalogCounts{}
        , _scopeHistory{}
        , _rowQuery{}
        , _listScratchRow{}
        , _listScratchSeries{}
        , _arrFilter{}
        , _lastTracyResult{ EditorTracyLaunchResult::Launched }
        , _listTimelineThread{}
        , _listTimelineRect{}
        , _listTimelineLaneCount{}
        , _listTimelineFrameBegin{}
        , _timelineWindowBegin{ 0 }
        , _timelineWindowEnd{ 0 }
        , _timelineViewBegin{ 0 }
        , _timelineViewEnd{ 0 }
        , _timelineFrameCount{ 4 }
        , _listCallNode{}
        , _listCallRoot{}
        , _listCallThread{}
        , _spikeThresholdMs{ 33.3f }
        , _bCatalogDirty{ SW_TRUE }
        , _bCollect{ SW_TRUE }
        , _bTracyTried{ SW_FALSE }
        , _bTimelineFrozen{ SW_FALSE }
        , _bTimelineZoomed{ SW_FALSE }
        , _bPauseOnSpike{ SW_FALSE }
        , _bCapturePaused{ SW_FALSE }
        , _bCallTreeFrozen{ SW_FALSE }
#if SW_PROFILER_TRACE_ENABLED
        , _loadedTrace{}
        , _listTimelineDraw{}
        , _listLoadedFrameDraw{}
        , _traceStatus{}
        , _arrTraceSearch{}
        , _traceSearchMatched{ 0 }
        , _bLoadedTrace{ SW_FALSE }
        , _bSelectTimelineTab{ SW_FALSE }
#endif
    {
    }

    void ProfilerPanel::drawContent()
    {
        // 패널이 열려 있는 동안 엔진 프로파일러를 켜 두고 프레임마다 한 칸씩 담는다. 탭과 무관하게 담으므로 탭을 바꿔도 그래프가 끊기지 않는다.
        FrameProfiler* pProfiler = editor::findSubsystem<FrameProfiler>();
        if ( pProfiler != nullptr )
        {
            if ( _bCollect == SW_TRUE && pProfiler->isEnabled() == false )
                pProfiler->setEnabled( true );
            if ( _bCapturePaused == SW_FALSE && _scopeHistory.capture( *pProfiler ) )
                pauseOnSpike();
        }

        if ( ImGui::BeginTabBar( "ProfilerTabs" ) )
        {
            if ( ImGui::BeginTabItem( "CPU Scopes" ) )
            {
                drawScopeTab( ProfilerScopeKind::CPU );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "GPU Passes" ) )
            {
                drawScopeTab( ProfilerScopeKind::GPU );
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Counters" ) )
            {
                drawScopeTab( ProfilerScopeKind::Counter );
                ImGui::EndTabItem();
            }
#if SW_PROFILER_TRACE_ENABLED
            const ImGuiTabItemFlags timelineFlags = _bSelectTimelineTab == SW_TRUE ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            _bSelectTimelineTab                   = SW_FALSE;
#else
            constexpr ImGuiTabItemFlags timelineFlags = ImGuiTabItemFlags_None;
#endif
            const bool bTimelineTab = ImGui::BeginTabItem( "Timeline", nullptr, timelineFlags );
            EditorSelfTestMarks::note( "profiler.timeline.tab" );
            if ( bTimelineTab )
            {
                drawTimelineTab();
                ImGui::EndTabItem();
            }
            const bool bCallTreeTab = ImGui::BeginTabItem( "Call Tree" );
            EditorSelfTestMarks::note( "profiler.callTree.tab" );
            if ( bCallTreeTab )
            {
                drawCallTreeTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Performance & Scene" ) )
            {
                drawPerformanceTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "Memory" ) )
            {
                drawMemoryTab();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "GPU Memory" ) )
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
        bool bCollect = _bCollect == SW_TRUE;
        if ( ImGui::Checkbox( "Collect", &bCollect ) )
        {
            _bCollect                = bCollect ? SW_TRUE : SW_FALSE;
            FrameProfiler* pProfiler = editor::findSubsystem<FrameProfiler>();
            if ( pProfiler != nullptr )
                pProfiler->setEnabled( bCollect );
        }
        ImGui::SameLine();
        const uint64 capturedFrames = MathUtil::min( _scopeHistory.getCapturedFrameCount(), static_cast<uint64>( UINT32_MAX ) );
        ImGui::TextDisabled( "%u frames window, %u captured", _scopeHistory.getWindowFrame(), static_cast<uint32>( capturedFrames ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Clear" ) )
        {
            _scopeHistory.reset();
            _rowQuery._frameOffset = 0;
        }
        EditorSelfTestMarks::note( "profiler.capture.clear" );

        // 정지, 스파이크, 캡처 파일을 다룬다. 유니티 Profiler 의 Record 끄기, 언리얼의 stat hitches, Save/Load 에 해당한다.
        ImGui::SameLine();
        if ( _bCapturePaused == SW_TRUE )
        {
            if ( ImGui::SmallButton( "Resume" ) )
            {
                _bCapturePaused        = SW_FALSE;
                _rowQuery._frameOffset = 0;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "ResumeCapture" ) );
        }
        else if ( ImGui::SmallButton( "Pause" ) )
            _bCapturePaused = SW_TRUE;
        EditorSelfTestMarks::note( "profiler.capture.pause" );
        ImGui::SameLine();
        bool bPauseOnSpike = _bPauseOnSpike == SW_TRUE;
        if ( ImGui::Checkbox( "Pause on spike", &bPauseOnSpike ) )
            _bPauseOnSpike = bPauseOnSpike ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "profiler.capture.pauseOnSpike" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "PauseOnSpike" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
        ImGui::DragFloat( "##spikeMs", &_spikeThresholdMs, 0.1f, 0.1f, 1000.0f, "%.1f ms" );
        EditorSelfTestMarks::note( "profiler.capture.spikeMs" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SpikeThreshold" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Save" ) )
        {
            const string path = getCaptureFilePath();
            if ( FileUtil::ensureParentDirectoryExists( path ) && _scopeHistory.saveToFile( path ) )
                SW_LOG_INFO( "Profiler capture saved: %#", path.c_str() );
            else
                SW_LOG_WARNING( "Profiler capture could not be saved: %#", path.c_str() );
        }
        EditorSelfTestMarks::note( "profiler.capture.save" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SaveCapture" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Load" ) )
        {
            const string path = getCaptureFilePath();
            if ( _scopeHistory.loadFromFile( path ) )
            {
                _bCapturePaused        = SW_TRUE; // 연 캡처를 보는 동안 새 프레임이 덮어쓰지 않게 한다
                _rowQuery._frameOffset = 0;
                SW_LOG_INFO( "Profiler capture loaded: %#", path.c_str() );
            }
            else
                SW_LOG_WARNING( "Profiler capture could not be loaded: %#", path.c_str() );
        }
        EditorSelfTestMarks::note( "profiler.capture.load" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "LoadCapture" ) );

        // Tracy. 시간축 분석은 외부 뷰어가 한다. 상태 한 줄과 여는 버튼을 둔다.
        ImGui::SameLine();
        ImGui::TextUnformatted( "|" );
        ImGui::SameLine();
        const IProfilerBackend* pBackend = ProfilerBackend::getActiveBackend();
        if ( ProfilerBackend::isTracyCompiled() == false )
            ImGui::TextDisabled( "Tracy: not in this build" );
        else if ( pBackend == nullptr )
            ImGui::TextDisabled( "Tracy: off" );
        else
            ImGui::Text( "Tracy: on (port %u, viewer %s)", static_cast<uint32>( ProfilerBackend::getTracyPort() ),
                         pBackend->isViewerConnected() ? "connected" : "waiting" );
        ImGui::SameLine();
        ImGui::BeginDisabled( ProfilerBackend::isTracyCompiled() == false );
        if ( ImGui::SmallButton( "Open Tracy" ) )
        {
            _lastTracyResult = EditorTracyLauncher::openViewer();
            _bTracyTried     = SW_TRUE;
        }
        ImGui::EndDisabled();
        if ( _bTracyTried == SW_TRUE && _lastTracyResult != EditorTracyLaunchResult::Launched )
            ImGui::TextColored( EditorColorUtil::toImVec4( ProfilerPanelInternal::kTracyErrorText ), "%s", EditorTracyLauncher::describeResult( _lastTracyResult ) );

        // RenderDoc 으로 GPU 프레임을 캡처한다(네 백엔드 모두). 붙어 있지 않으면 버튼이 회색이고 이유를 툴팁으로 보여 준다.
        ImGui::SameLine();
        ImGui::TextUnformatted( "|" );
        ImGui::SameLine();
        const bool bRenderDoc = RenderDocCapture::isAvailable();
        ImGui::BeginDisabled( bRenderDoc == false );
        if ( ImGui::SmallButton( "RenderDoc Capture" ) )
            RenderDocCapture::triggerCapture();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "RenderDocCapture" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Open UI" ) && RenderDocCapture::launchReplayUI() == false )
            SW_LOG_WARNING( "RenderDoc replay UI could not be started" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "RenderDocOpenUI" ) );
        ImGui::EndDisabled();
        if ( bRenderDoc == false && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", SW_LOCKEY( "EditorProfiler", "RenderDocNotAttached" ) );
    }

    void ProfilerPanel::drawTimelineTab()
    {
        FrameProfiler* pProfiler = editor::findSubsystem<FrameProfiler>();
        if ( pProfiler == nullptr )
        {
            ImGui::TextDisabled( "Engine profiler is not available" );
            return;
        }
        ProfilerTimeline& timeline   = pProfiler->getTimeline();
        const bool        bRecording = timeline.isRecording();
        if ( ImGui::Button( bRecording ? "Stop Recording" : "Record" ) )
        {
            // 녹화는 계측이 켜져 있어야 쌓인다. 스코프가 시계를 읽어야 하기 때문이다. 켤 때 지난 이벤트를 비운다.
            if ( bRecording == false )
            {
                timeline.clear();
                _bCollect = SW_TRUE;
                pProfiler->setEnabled( true );
                _bTimelineZoomed = SW_FALSE;
            }
            timeline.setRecording( bRecording == false );
        }
        EditorSelfTestMarks::note( "profiler.timeline.record" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "TimelineRecord" ) );

        ImGui::SameLine();
        ImGui::TextUnformatted( "Frames:" );
        for ( const uint32 frameChoice : ProfilerPanelInternal::kArrTimelineFrameChoice )
        {
            ImGui::SameLine();
            fixed_string<constant::kMaxBuffer16> label;
            formatstring( label.data(), label.capacity(), "%#", frameChoice );
            if ( ImGui::RadioButton( label.c_str(), _timelineFrameCount == frameChoice ) )
            {
                _timelineFrameCount = frameChoice;
                _bTimelineZoomed    = SW_FALSE;
            }
        }
        ImGui::SameLine();
        bool bFrozen = _bTimelineFrozen == SW_TRUE;
        if ( ImGui::Checkbox( "Freeze", &bFrozen ) )
            _bTimelineFrozen = bFrozen ? SW_TRUE : SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "Freeze" ) );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Fit" ) )
            _bTimelineZoomed = SW_FALSE;
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "Fit" ) );
#if SW_PROFILER_TRACE_ENABLED
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Save Trace" ) )
        {
            string     savedPath;
            string     reply;
            const bool bSaved = ProfilerTraceExport::saveRecentFrames( *pProfiler, _timelineFrameCount, {}, "Editor", savedPath, reply );
            if ( bSaved )
                SW_LOG_INFO( "Timeline trace: %#", reply );
            else
                SW_LOG_WARNING( "Timeline trace not saved: %#", reply );
        }
        EditorSelfTestMarks::note( "profiler.timeline.saveTrace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "SaveTrace" ) );
        ImGui::NewLine();
        drawTraceControls();
#endif
        ImGui::SameLine();
        ImGui::TextDisabled( "| wheel = zoom, drag = pan, deep analysis: Tracy" );

        if ( _bTimelineFrozen == SW_FALSE )
        {
            if ( timeline.collectRecentFrames( _timelineFrameCount, _listTimelineThread, _timelineWindowBegin, _timelineWindowEnd ) == false )
                _listTimelineThread.clear();
        }
        if ( _bTimelineZoomed == SW_FALSE || _timelineViewEnd <= _timelineViewBegin )
        {
            _timelineViewBegin = _timelineWindowBegin;
            _timelineViewEnd   = _timelineWindowEnd;
        }
#if SW_PROFILER_TRACE_ENABLED
        composeTimelineDraw();
        const vector<ProfilerTimelineThread>& listDraw = _bLoadedTrace == SW_TRUE ? _listTimelineDraw : _listTimelineThread;
        const vector<string>*                 pLoaded  = _bLoadedTrace == SW_TRUE ? &_loadedTrace._listSlotName : nullptr;
#else
        const vector<ProfilerTimelineThread>& listDraw = _listTimelineThread;
        const vector<string>*                 pLoaded  = nullptr;
#endif
        if ( listDraw.empty() )
        {
            ImGui::TextDisabled( "%s", bRecording ? "Waiting for frames..." : "Press Record to capture the last frames per thread, or Load Trace." );
            return;
        }
        timeline.collectFrameBegins( _timelineViewBegin, _timelineViewEnd, _listTimelineFrameBegin );
#if SW_PROFILER_TRACE_ENABLED
        if ( _bLoadedTrace == SW_TRUE )
        {
            for ( const uint64 frameBegin : _listLoadedFrameDraw )
            {
                if ( _timelineViewBegin <= frameBegin && frameBegin <= _timelineViewEnd )
                    _listTimelineFrameBegin.push_back( frameBegin );
            }
        }
#endif
        drawTimelineCanvas( listDraw, pLoaded );
    }

#if SW_PROFILER_TRACE_ENABLED
    void ProfilerPanel::setTraceSearch( const utf8* pText )
    {
        std::snprintf( _arrTraceSearch, constant::kMaxBuffer128, "%s", pText != nullptr ? pText : "" );
    }

    bool ProfilerPanel::loadTraceFile( string_view path )
    {
        string error;
        if ( ProfilerTraceExport::loadTraceFile( path, _loadedTrace, error ) == false )
        {
            _traceStatus = "Load failed: " + error;
            SW_LOG_WARNING( "Trace not loaded: %#", error );
            return false;
        }
        _bLoadedTrace    = SW_TRUE;
        _bTimelineZoomed = SW_FALSE;
        _traceStatus     = "Loaded " + FileUtil::getFileNamePart( path ) + " (" + to_string( _loadedTrace._listThread.size() ) + " threads, " +
                       to_string( _loadedTrace._listSlotName.size() ) + " scopes)";
        SW_LOG_INFO( "Trace loaded: %#", _traceStatus );
        return true;
    }

    bool ProfilerPanel::openTraceInTimeline( EditorPanelManager& panelManager, string_view path )
    {
        ProfilerPanel* pPanel = panelManager.findPanel<ProfilerPanel>();
        if ( pPanel == nullptr || panelManager.setPanelOpen<ProfilerPanel>( true ) == false )
        {
            SW_LOG_WARNING( "No Profiler window to open %# in", string( path ) );
            return false;
        }
        if ( pPanel->loadTraceFile( path ) == false )
            return false;
        pPanel->_bSelectTimelineTab = SW_TRUE;
        return true;
    }

    bool ProfilerPanel::loadLatestTrace()
    {
        const string path = ProfilerTraceExport::findLatestTraceFile();
        if ( path.empty() )
        {
            _traceStatus = "No trace in Saved/Traces - press Save Trace first";
            return false;
        }
        return loadTraceFile( path );
    }

    void ProfilerPanel::drawTraceControls()
    {
        if ( ImGui::SmallButton( "Load Trace" ) )
            if ( loadLatestTrace() == false )
                SW_LOG_WARNING( "%#", _traceStatus );
        EditorSelfTestMarks::note( "profiler.timeline.loadTrace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "LoadTrace" ) );
        if ( _bLoadedTrace == SW_TRUE )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Unload" ) )
            {
                _bLoadedTrace    = SW_FALSE;
                _loadedTrace     = ProfilerLoadedTrace{};
                _bTimelineZoomed = SW_FALSE;
            }
            EditorSelfTestMarks::note( "profiler.timeline.unloadTrace" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "UnloadTrace" ) );
        }
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##traceSearch", _arrTraceSearch, constant::kMaxBuffer128, SW_LOCKEY( "EditorProfiler", "FindScopeHint" ), 140.0f * EditorThemeUtil::getDpiScale(), false );
        EditorSelfTestMarks::note( "profiler.timeline.traceSearch" );
        if ( _traceStatus.empty() == false )
            ImGui::TextDisabled( "%s", _traceStatus.c_str() );
    }

    void ProfilerPanel::composeTimelineDraw()
    {
        _listTimelineDraw.clear();
        _listLoadedFrameDraw.clear();
        if ( _bLoadedTrace == SW_FALSE )
            return;
        // 살아 있는 기록이 있으면 불러온 트레이스의 첫 시각을 그 구간 시작에 맞춘다. 둘은 다른 프로세스의 단조 시계라 절대 시각으로는 겹칠 수 없다.
        const bool   bLive  = _listTimelineThread.empty() == false;
        const uint64 anchor = bLive ? _timelineWindowBegin : _loadedTrace._beginNanos;
        const int64  shift  = static_cast<int64>( anchor ) - static_cast<int64>( _loadedTrace._beginNanos );
        _listTimelineDraw   = _listTimelineThread;
        for ( const ProfilerTimelineThread& source : _loadedTrace._listThread )
        {
            ProfilerTimelineThread copy;
            copy._name = "[loaded] " + source._name;
            copy._listEvent.reserve( source._listEvent.size() );
            for ( ProfilerTimelineEvent event : source._listEvent )
            {
                event._beginNanos = static_cast<uint64>( static_cast<int64>( event._beginNanos ) + shift );
                event._endNanos   = static_cast<uint64>( static_cast<int64>( event._endNanos ) + shift );
                event._slot += ProfilerPanelInternal::kLoadedSlotBase;
                copy._listEvent.push_back( event );
            }
            _listTimelineDraw.push_back( std::move( copy ) );
        }
        for ( const uint64 frameBegin : _loadedTrace._listFrameBegin )
        {
            _listLoadedFrameDraw.push_back( static_cast<uint64>( static_cast<int64>( frameBegin ) + shift ) );
        }
        if ( bLive == false )
        {
            _timelineWindowBegin = anchor;
            _timelineWindowEnd   = static_cast<uint64>( static_cast<int64>( _loadedTrace._endNanos ) + shift );
            if ( _bTimelineZoomed == SW_FALSE || _timelineViewEnd <= _timelineViewBegin )
            {
                _timelineViewBegin = _timelineWindowBegin;
                _timelineViewEnd   = _timelineWindowEnd;
            }
        }
        else if ( _timelineWindowEnd < static_cast<uint64>( static_cast<int64>( _loadedTrace._endNanos ) + shift ) && _bTimelineZoomed == SW_FALSE )
        {
            // 불러온 쪽이 더 길면 보이는 범위를 넓혀 둘 다 보이게 한다.
            _timelineViewEnd = static_cast<uint64>( static_cast<int64>( _loadedTrace._endNanos ) + shift );
        }
    }
#endif

    void ProfilerPanel::drawTimelineCanvas( const vector<ProfilerTimelineThread>& listThread, const vector<string>* pLoadedSlotName )
    {
        const FrameProfiler* pProfiler  = editor::findSubsystem<FrameProfiler>();
        const float32        dpiScale   = EditorThemeUtil::getDpiScale();
        const float32        labelWidth = ProfilerPanelInternal::kTimelineLabelWidth * dpiScale;
        const float32        laneHeight = ImGui::GetTextLineHeight() + 4.0f * dpiScale;
        const float32        rowGap     = 2.0f * dpiScale;
        const ImVec2         origin     = ImGui::GetCursorScreenPos();
        const float32        totalWidth = MathUtil::max( ImGui::GetContentRegionAvail().x, labelWidth + 1.0f );
        const float32        trackWidth = totalWidth - labelWidth;
        const float32        trackLeft  = origin.x + labelWidth;

        ProfilerTimelineLayout::layoutRects( listThread, _timelineViewBegin, _timelineViewEnd, trackWidth, _listTimelineRect, _listTimelineLaneCount );
        float32 totalHeight{ 0.0f };
        for ( const uint16 laneCount : _listTimelineLaneCount )
        {
            totalHeight += static_cast<float32>( laneCount ) * laneHeight + rowGap;
        }

        ImGui::InvisibleButton( "##TimelineCanvas", ImVec2{ totalWidth, MathUtil::max( totalHeight, laneHeight ) } );
        const bool   bHovered = ImGui::IsItemHovered();
        const ImVec2 mouse    = ImGui::GetIO().MousePos;
        if ( bHovered && ImGui::GetIO().MouseWheel != 0.0f )
        {
            const float32 factor = ImGui::GetIO().MouseWheel > 0.0f ? ProfilerPanelInternal::kTimelineWheelZoomFactor
                                                                    : 1.0f / ProfilerPanelInternal::kTimelineWheelZoomFactor;
            ProfilerTimelineLayout::zoom( _timelineViewBegin, _timelineViewEnd, mouse.x - trackLeft, trackWidth, factor, _timelineWindowBegin, _timelineWindowEnd );
            _bTimelineZoomed = SW_TRUE;
        }
        if ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
        {
            ProfilerTimelineLayout::pan( _timelineViewBegin, _timelineViewEnd, ImGui::GetIO().MouseDelta.x, trackWidth, _timelineWindowBegin, _timelineWindowEnd );
            _bTimelineZoomed = SW_TRUE;
        }

        ImDrawList*     pDrawList = ImGui::GetWindowDrawList();
        vector<float32> listRowTop( listThread.size(), 0.0f );
        float32         rowTop = origin.y;
        for ( size_t threadIndex = 0; threadIndex < listThread.size(); ++threadIndex )
        {
            listRowTop[threadIndex] = rowTop;
            const float32 rowHeight = static_cast<float32>( _listTimelineLaneCount[threadIndex] ) * laneHeight;
            pDrawList->AddRectFilled( ImVec2{ origin.x, rowTop }, ImVec2{ origin.x + totalWidth, rowTop + rowHeight },
                                      ImGui::GetColorU32( ImGuiCol_FrameBg, ( threadIndex % 2 == 0 ) ? 0.6f : 0.3f ) );
            pDrawList->AddText( ImVec2{ origin.x + rowGap * 2.0f, rowTop + rowGap }, ImGui::GetColorU32( ImGuiCol_Text ),
                                listThread[threadIndex]._name.c_str() );
            rowTop += rowHeight + rowGap;
        }

        pDrawList->PushClipRect( ImVec2{ trackLeft, origin.y }, ImVec2{ origin.x + totalWidth, origin.y + totalHeight }, true );
        const ProfilerTimelineRect* pHoveredRect = nullptr;
        // 슬롯에서 이름을 찾는다. 불러온 트레이스의 슬롯은 `kLoadedSlotBase` 이상이므로 그 테이블을 본다.
        const auto findName = [&]( uint32 slot ) -> const utf8*
        {
            if ( slot >= ProfilerPanelInternal::kLoadedSlotBase )
            {
                const size_t loadedIndex = slot - ProfilerPanelInternal::kLoadedSlotBase;
                return ( pLoadedSlotName != nullptr && loadedIndex < pLoadedSlotName->size() ) ? ( *pLoadedSlotName )[loadedIndex].c_str() : nullptr;
            }
            return pProfiler != nullptr ? pProfiler->findScopeName( slot ) : nullptr;
        };
#if SW_PROFILER_TRACE_ENABLED
        const bool bSearching = _arrTraceSearch[0] != '\0';
        _traceSearchMatched   = 0;
#endif
        for ( const ProfilerTimelineRect& rect : _listTimelineRect )
        {
            const ProfilerTimelineEvent& event = listThread[rect._threadIndex]._listEvent[rect._eventIndex];
            const ImVec2                 minPoint{ trackLeft + rect._x0, listRowTop[rect._threadIndex] + static_cast<float32>( rect._depth ) * laneHeight };
            const ImVec2                 maxPoint{ trackLeft + rect._x1, minPoint.y + laneHeight - 1.0f };
            const utf8*                  pName     = findName( event._slot );
            ImU32                        slotColor = ProfilerPanelInternal::computeSlotColor( event._slot );
#if SW_PROFILER_TRACE_ENABLED
            // 검색어가 있으면 일치하는 구간만 또렷하게 두고 나머지는 흐리게 한다.
            if ( bSearching )
            {
                if ( pName != nullptr && StringUtil::contains( pName, _arrTraceSearch, true ) )
                    ++_traceSearchMatched;
                else
                    slotColor = ( slotColor & 0x00FFFFFFu ) | ( 50u << IM_COL32_A_SHIFT );
            }
            else
            {
                ++_traceSearchMatched;
            }
#endif
            pDrawList->AddRectFilled( minPoint, maxPoint, slotColor );
            if ( rect._bShowsLabel == SW_TRUE && pName != nullptr )
            {
                pDrawList->PushClipRect( minPoint, maxPoint, true );
                pDrawList->AddText( ImVec2{ minPoint.x + rowGap, minPoint.y + rowGap }, EditorColorUtil::toImU32( ProfilerPanelInternal::kTimelineLabel ), pName );
                pDrawList->PopClipRect();
            }
            const bool bUnderMouse = bHovered && minPoint.x <= mouse.x && mouse.x <= maxPoint.x && minPoint.y <= mouse.y && mouse.y <= maxPoint.y;
            if ( bUnderMouse )
                pHoveredRect = &rect;
        }
        // 프레임 경계는 세로선으로 그린다.
        for ( const uint64 frameBegin : _listTimelineFrameBegin )
        {
            const float32 lineX = trackLeft + ProfilerTimelineLayout::computeX( frameBegin, _timelineViewBegin, _timelineViewEnd, trackWidth );
            pDrawList->AddLine( ImVec2{ lineX, origin.y }, ImVec2{ lineX, origin.y + totalHeight }, EditorColorUtil::toImU32( ProfilerPanelInternal::kFrameBoundary ), 1.0f );
        }
        pDrawList->PopClipRect();

        if ( pHoveredRect != nullptr )
        {
            const ProfilerTimelineEvent& event = listThread[pHoveredRect->_threadIndex]._listEvent[pHoveredRect->_eventIndex];
            const utf8*                  pName = findName( event._slot );
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( pName != nullptr ? pName : "?" );
            ImGui::Text( "%.1f us, depth %u", static_cast<float64>( event._endNanos - event._beginNanos ) / constant::kNanosecondsPerMicrosecond, static_cast<uint32>( event._depth ) );
            ImGui::TextDisabled( "%s", listThread[pHoveredRect->_threadIndex]._name.c_str() );
            // 같은 구간(같은 스레드, 같은 슬롯)의 횟수, 합계, 최대를 구한다. 어느 구간이 늘었는지 한눈에 볼 수 있다.
            uint32  sameCount{ 0 };
            float64 sameTotalUs{ 0.0 };
            float64 sameMaxUs{ 0.0 };
            for ( const ProfilerTimelineEvent& other : listThread[pHoveredRect->_threadIndex]._listEvent )
            {
                if ( other._slot != event._slot )
                    continue;
                const float64 micros = static_cast<float64>( other._endNanos - other._beginNanos ) / constant::kNanosecondsPerMicrosecond;
                ++sameCount;
                sameTotalUs += micros;
                sameMaxUs = MathUtil::max( sameMaxUs, micros );
            }
            ImGui::TextDisabled( "same scope here: %u x, total %.1f us, max %.1f us", sameCount, sameTotalUs, sameMaxUs );
            ImGui::EndTooltip();
        }
    }

    void ProfilerPanel::drawFrameGraph()
    {
        // 게임 스레드, 렌더 스레드, GPU 세 줄을 그린다. 같은 축(ms)으로 그려야 어느 쪽이 프레임 시간을 정하는지 보인다.
        static constexpr const utf8* kArrSeriesName[] = { "GT.Frame", "RT.Frame", "GPU.Frame" };
        float32                      maxMicro{ 1000.0f };
        for ( const utf8* pSeriesName : kArrSeriesName )
        {
            if ( _scopeHistory.copySeries( pSeriesName, _listScratchSeries ) == false )
                continue;
            for ( const float32 value : _listScratchSeries )
            {
                maxMicro = MathUtil::max( maxMicro, value );
            }
        }
        const float32 maxMs = maxMicro / constant::kMicrosecondsPerMillisecond;
        for ( const utf8* pSeriesName : kArrSeriesName )
        {
            if ( _scopeHistory.copySeries( pSeriesName, _listScratchSeries ) == false || _listScratchSeries.empty() )
            {
                ImGui::TextDisabled( "%s: no samples", pSeriesName );
                continue;
            }
            for ( float32& value : _listScratchSeries )
            {
                value /= 1000.0f;
            }
            fixed_string<constant::kMaxBuffer64> overlay;
            formatstring( overlay.data(), overlay.capacity(), "%# %# ms", pSeriesName,
                          Fmt( static_cast<float64>( _listScratchSeries.back() ), Format().precision( 2 ) ) );
            ImGui::PlotLines( pSeriesName, _listScratchSeries.data(), static_cast<int32>( _listScratchSeries.size() ), 0, overlay.c_str(), 0.0f,
                              maxMs, ImVec2{ 0.0f, 48.0f } );
            // 그래프의 한 프레임을 누르면 테이블의 "Last" 가 그 프레임 값이 된다. 유니티 Profiler 의 프레임 선택과 같다. 선택한 프레임에는 세로줄을 그린다.
            const ImVec2  plotMin    = ImGui::GetItemRectMin();
            const ImVec2  plotMax    = ImGui::GetItemRectMax();
            const float32 frameWidth = ( plotMax.x - plotMin.x ) / static_cast<float32>( _listScratchSeries.size() );
            if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) && frameWidth > 0.0f )
            {
                const int32 clicked    = static_cast<int32>( ( ImGui::GetIO().MousePos.x - plotMin.x ) / frameWidth );
                const int32 lastIndex  = static_cast<int32>( _listScratchSeries.size() ) - 1;
                _rowQuery._frameOffset = static_cast<uint32>( lastIndex - MathUtil::clamp( clicked, 0, lastIndex ) );
            }
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( string{ "profiler.graph." } + pSeriesName ).c_str() );
            if ( _rowQuery._frameOffset > 0 )
            {
                const float32 lineX = plotMax.x - ( static_cast<float32>( _rowQuery._frameOffset ) + 0.5f ) * frameWidth;
                ImGui::GetWindowDrawList()->AddLine( ImVec2{ lineX, plotMin.y }, ImVec2{ lineX, plotMax.y }, EditorColorUtil::toImU32( EditorThemeUtil::getHighlightColor() ) );
            }
        }
        if ( _rowQuery._frameOffset > 0 )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( ProfilerPanelInternal::kFrameOffsetText ), "Last column shows the frame %u frames ago", _rowQuery._frameOffset );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Latest" ) )
                _rowQuery._frameOffset = 0;
        }
    }

    void ProfilerPanel::drawScopeTable( ProfilerScopeKind kind )
    {
        EditorWidgets::drawSearchField( "##profilerScopeFilter", _arrFilter, constant::kMaxBuffer128, SW_LOCKEY( "EditorProfiler", "FilterScopesHint" ) );
        _rowQuery._kind       = kind;
        _rowQuery._filterText = _arrFilter;

        const bool            bCounter = kind == ProfilerScopeKind::Counter;
        const utf8* const     pUnit    = bCounter ? "" : " (us)";
        const ImGuiTableFlags flags    = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
        if ( ImGui::BeginTable( "##profilerScopes", 7, flags, ImGui::GetContentRegionAvail() ) == false )
            return;

        fixed_string<constant::kMaxBuffer32> header;
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
            _rowQuery._sortColumn  = static_cast<ProfilerSortColumn>( pSortSpecs->Specs[0].ColumnUserID );
            _rowQuery._bDescending = pSortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
        }

        _scopeHistory.makeRows( _rowQuery, _listScratchRow );
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
        // ImGui 경과는 엔진 경과(자동화에서는 고정 프레임 시간)라 프레임 비용을 재려면 벽시계를 따로 읽는다.
        const float32 dt = static_cast<float32>( _drawStopwatch.getElapsedNanoseconds() ) / static_cast<float32>( constant::kNanosecondsPerSecond );
        _drawStopwatch.restart();
        const float32 frameTimeMs = dt * constant::kMillisecondsPerSecond;
        const float32 fps         = ( dt > 0.0f ) ? ( 1.0f / dt ) : 0.0f;

        _arrFrameTimeHistory[_historyOffset] = frameTimeMs;
        _historyOffset                       = ( _historyOffset + 1 ) % 120;

        float32 minMs{ MathUtil::kMaxFloat };
        float32 maxMs{ 0.0f };
        float32 sumMs{ 0.0f };
        for ( size_t historyIndex = 0; historyIndex < 120; ++historyIndex )
        {
            const float32 val = _arrFrameTimeHistory[historyIndex];
            if ( val > 0.0f )
            {
                minMs = MathUtil::min( minMs, val );
                maxMs = MathUtil::max( maxMs, val );
                sumMs += val;
            }
        }
        const float32 avgMs  = sumMs / 120.0f;
        const float32 avgFps = ( avgMs > 0.0f ) ? ( 1000.0f / avgMs ) : 0.0f;

        ImGui::Text( "Current FPS: %.1f (%.2f ms)", static_cast<float64>( fps ), static_cast<float64>( frameTimeMs ) );
        ImGui::Text( "Average FPS: %.1f (Avg: %.2f ms, Min: %.2f ms, Max: %.2f ms)",
                     static_cast<float64>( avgFps ), static_cast<float64>( avgMs ),
                     static_cast<float64>( minMs ), static_cast<float64>( maxMs ) );

        ImGui::PlotLines( "Frame Time (ms)", _arrFrameTimeHistory, 120, static_cast<int32>( _historyOffset ),
                          nullptr, 0.0f, 33.3f, ImVec2{ 0.0f, 80.0f } );

        ImGui::Separator();

        drawSceneDistributionSection();

        if ( ImGui::CollapsingHeader( "Resource Catalog Summary", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            EditorResourceCatalogCounts counts{};
            const EditorTaskPoll        catalogPoll = _catalogJob.take( counts );
            if ( catalogPoll == EditorTaskPoll::Ready )
                _catalogCounts = counts;
            if ( _bCatalogDirty == SW_TRUE && catalogPoll != EditorTaskPoll::Running )
            {
                _catalogJob.request();
                _bCatalogDirty = SW_FALSE;
            }

            if ( ImGui::Button( "Scan Resources" ) )
                _bCatalogDirty = SW_TRUE;

            for ( const EditorResourceCatalogCount& row : _catalogCounts._listKindCount )
            {
                ImGui::BulletText( "%s: %zu", row._pLabel, row._count );
            }
        }

        ImGui::Separator();

        if ( ImGui::CollapsingHeader( "Task Manager & Concurrency", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            TaskManager* pTaskManager = editor::findSubsystem<TaskManager>();
            if ( pTaskManager != nullptr )
            {
                ImGui::BulletText( "Worker Threads: %u", pTaskManager->getWorkerCount() );
                ImGui::BulletText( "Task System: Active" );
            }
            else
            {
                EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "TaskManagerInactive" ) );
            }
        }
    }

    void ProfilerPanel::drawSceneDistributionSection()
    {
        if ( ImGui::CollapsingHeader( "Active Scene & Component Distribution", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            Scene* pScene = editor::getActiveScene();
            if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
            {
                // 집계는 ImGui 에 의존하지 않는 EditorSceneCommands 가 한다. 테스트가 있다.
                // 이 패널은 그리기만 하며 타입 이름을 알 필요가 없다.
                const EditorSceneCommands::SceneStatistics stats =
                    EditorSceneCommands::collectSceneStatistics( pScene->getObjectManager() );

                ImGui::BulletText( "Total GameObjects: %u (Roots: %u)", stats._objectCount, stats._rootCount );
                ImGui::BulletText( "Total Attached Components: %u", stats._componentCount );

                if ( stats._listDistribution.empty() )
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "NoComponents" ) );
                else if ( ImGui::BeginTable( "CompDistributionTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg ) )
                {
                    ImGui::TableSetupColumn( "Component Type" );
                    ImGui::TableSetupColumn( "Active Instances" );
                    ImGui::TableHeadersRow();

                    for ( const EditorSceneCommands::ComponentDistributionRow& row : stats._listDistribution )
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
        EditorContext* pContext = EditorContext::get();
        IRHIDevice*    pDevice  = ( pContext != nullptr ) ? pContext->getSubsystem<EditorRenderViews>().getRHIDevice() : nullptr;
        if ( pDevice == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "NoRHIDevice" ) );
            return;
        }
        const RHIMemoryLedger& ledger  = pDevice->getMemoryLedger();
        const RHIMemorySummary summary = ledger.makeSummary();

        fixed_string<constant::kMaxBuffer32> arrBytesBuf;
        ProfilerPanelInternal::formatBytes( summary._trackedBytes, arrBytesBuf );
        ImGui::Text( "%s - %s size", pDevice->getBackendName(), RHIMemoryLedger::getSizeBasisName( summary._sizeBasis ) );
        ImGui::Text( "Tracked: %s  (+ %u of unknown size)", arrBytesBuf.c_str(), summary._unknownSizeCount );

        if ( ImGui::CollapsingHeader( "Engine Ledger By Kind", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "GpuMemoryTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
            {
                ImGui::TableSetupColumn( "Kind" );
                ImGui::TableSetupColumn( "Live Bytes" );
                ImGui::TableSetupColumn( "Share" );
                ImGui::TableSetupColumn( "Count" );
                ImGui::TableSetupColumn( "Unknown Size" );
                ImGui::TableHeadersRow();

                // 로그 보고와 같은 순서(바이트가 큰 항목부터)와 같은 이름을 쓴다. 크기 모름은 바이트 합에 넣지 않고 따로 센다.
                for ( const RHIMemoryKind kind : ledger.makeKindOrderByLiveBytes() )
                {
                    const RHIMemoryKindStats stats = ledger.getStats( kind );
                    ProfilerPanelInternal::formatBytes( stats._liveBytes, arrBytesBuf );
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( RHIMemoryLedger::getKindName( kind ) );
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

        if ( ImGui::CollapsingHeader( "Driver", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            // 드라이버가 응답하지 않은 항목은 "Unknown" 으로 표시하고, 0 으로 대체하지 않는다.
            const RHIMemoryBudget&               budget = summary._budget;
            fixed_string<constant::kMaxBuffer32> arrUsageBuf;
            fixed_string<constant::kMaxBuffer32> arrBudgetBuf;
            fixed_string<constant::kMaxBuffer32> arrAvailableBuf;
            ProfilerPanelInternal::formatKnownBytes( budget._usageBytes, budget._bUsageKnown != SW_FALSE, arrUsageBuf );
            ProfilerPanelInternal::formatKnownBytes( budget._budgetBytes, budget._bBudgetKnown != SW_FALSE, arrBudgetBuf );
            ProfilerPanelInternal::formatKnownBytes( budget._availableBytes, budget._bAvailableKnown != SW_FALSE, arrAvailableBuf );
            const utf8* pScope = budget._bUsageKnown == SW_FALSE          ? ""
                               : budget._scope == RHIMemoryScope::Process ? " (this process)"
                                                                          : " (whole device, other processes included)";
            ImGui::Text( "Usage: %s%s", arrUsageBuf.c_str(), pScope );
            ImGui::Text( "Budget: %s", arrBudgetBuf.c_str() );
            ImGui::Text( "Available: %s", arrAvailableBuf.c_str() );
            if ( summary._bOutsideKnown != SW_FALSE )
            {
                const bool bNegative = summary._outsideBytes < 0;
                ProfilerPanelInternal::formatBytes( static_cast<uint64>( bNegative ? -summary._outsideBytes : summary._outsideBytes ), arrBytesBuf );
                ImGui::Text( "Outside ledger (swap chain, driver, untracked): %s%s", bNegative ? "-" : "", arrBytesBuf.c_str() );
            }
            else
                ImGui::TextUnformatted( "Outside ledger: Unknown (no per-process driver usage)" );
        }
    }

    void ProfilerPanel::drawMemoryTab()
    {
        MemoryProfiler* pProfiler = editor::findSubsystem<MemoryProfiler>();
        if ( pProfiler == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorProfiler", "MemoryProfilerInactive" ) );
            return;
        }
        MemoryProfiler& profiler = *pProfiler;

        bool bTracking = profiler.isTrackingEnabled();
        if ( ImGui::Checkbox( "Enable Memory Tracking", &bTracking ) )
            profiler.setTrackingEnabled( bTracking );

        bool bDetailed = profiler.isDetailedTrackingEnabled();
        if ( ImGui::Checkbox( "Enable Detailed CallStack Tracking (High Overhead)", &bDetailed ) )
            profiler.setDetailedTrackingEnabled( bDetailed );

        ImGui::Separator();

        if ( ImGui::CollapsingHeader( "Global Statistics By Tag", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            if ( ImGui::BeginTable( "MemoryStatsTable", 5,
                                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
            {
                ImGui::TableSetupColumn( "Tag" );
                ImGui::TableSetupColumn( "Current Bytes" );
                ImGui::TableSetupColumn( "Share" );
                ImGui::TableSetupColumn( "Current Count" );
                ImGui::TableSetupColumn( "Total Allocated" );
                ImGui::TableHeadersRow();

                fixed_string<constant::kMaxBuffer32> arrBytesBuf;
                fixed_string<constant::kMaxBuffer32> arrTotalBuf;

                // 현재 살아 있는 바이트가 큰 태그부터 표시한다. 한 번도 할당하지 않은 태그는 행을 만들지 않는다. Unknown 은 항상 만든다. 태그가 지정되지 않은 진입점의 할당량이기 때문이다.
                const uint64 liveBytes = profiler.getLiveAllocatedBytes();
                for ( const MemoryTag tag : profiler.makeTagOrderByLiveBytes() )
                {
                    const auto& stats = profiler.getStats( tag );
                    if ( tag != MemoryTag::Unknown && stats._totalAllocatedBytes == 0 )
                        continue;

                    const uint64 currentBytes = stats._currentAllocatedBytes;
                    ProfilerPanelInternal::formatBytes( currentBytes, arrBytesBuf );
                    ProfilerPanelInternal::formatBytes( stats._totalAllocatedBytes, arrTotalBuf );

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
        }

        ImGui::Separator();

        if ( bDetailed && ImGui::CollapsingHeader( "Top Memory Allocations By Call Stack", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            vector<CallStackAllocInfo> listTopStack = profiler.getTopCallStacks();

            if ( listTopStack.empty() )
                ImGui::Text( "No detailed call stack data available or all freed." );
            else
            {
                if ( ImGui::BeginTable( "CallStackTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable ) )
                {
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
                        fixed_string<constant::kMaxBuffer64> allocBuf;
                        formatstring( allocBuf.data(), allocBuf.capacity(), "%# B (%# allocs)", info._currentBytes, info._currentCount );
                        ImGui::TextUnformatted( allocBuf.c_str() );

                        ImGui::TableNextColumn();

                        // 호출 스택을 펼쳐 볼 수 있도록 트리로 만든다
                        string treeLabel = "Stack Hash: " + to_string( info._stack._hash );
                        if ( ImGui::TreeNode( treeLabel.c_str() ) )
                        {
                            string stackStr = CallStackCapture::symbolize( info._stack );
                            ImGui::TextUnformatted( stackStr.c_str() );
                            ImGui::TreePop();
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }
        else if ( bDetailed == false )
            ImGui::Text( "Detailed CallStack tracking is disabled." );
    }

    void ProfilerPanel::pauseOnSpike()
    {
        float32    frameMicro{ 0.0f };
        const bool bHasFrame = _scopeHistory.readValue( "GT.Frame", 0, frameMicro );
        const bool bSpike    = bHasFrame && frameMicro >= _spikeThresholdMs * constant::kMicrosecondsPerMillisecond;
        if ( _bPauseOnSpike == SW_FALSE || bSpike == false )
            return;
        _bCapturePaused        = SW_TRUE;
        _rowQuery._frameOffset = 0;
        if ( EditorPlaySession::isPlaying() )
            EditorPlaySession::pause();
        SW_LOG_INFO( "Profiler paused on a spike: GT.Frame %# ms (threshold %# ms)", Fmt( static_cast<float64>( frameMicro ) / constant::kMicrosecondsPerMillisecond, Format().precision( 2 ) ),
                     Fmt( static_cast<float64>( _spikeThresholdMs ), Format().precision( 1 ) ) );
    }

    string ProfilerPanel::getCaptureFilePath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( path::kSavedFolder, "Profiler" ), "ProfilerCapture.txt" );
    }

    void ProfilerPanel::drawCallTreeTab()
    {
        FrameProfiler* pProfiler = editor::findSubsystem<FrameProfiler>();
        if ( pProfiler == nullptr )
        {
            ImGui::TextDisabled( "Engine profiler is not available" );
            return;
        }
        ProfilerTimeline& timeline   = pProfiler->getTimeline();
        const bool        bRecording = timeline.isRecording();
        if ( ImGui::Button( bRecording ? "Stop Recording" : "Record" ) )
        {
            if ( bRecording == false )
            {
                timeline.clear();
                _bCollect = SW_TRUE;
                pProfiler->setEnabled( true );
            }
            timeline.setRecording( bRecording == false );
        }
        EditorSelfTestMarks::note( "profiler.callTree.record" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorProfiler", "CallTreeRecord" ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( "Frames:" );
        for ( const uint32 frameChoice : ProfilerPanelInternal::kArrTimelineFrameChoice )
        {
            ImGui::SameLine();
            fixed_string<constant::kMaxBuffer16> label;
            formatstring( label.data(), label.capacity(), "%#", frameChoice );
            ImGui::PushID( "callTreeFrames" );
            if ( ImGui::RadioButton( label.c_str(), _timelineFrameCount == frameChoice ) )
                _timelineFrameCount = frameChoice;
            ImGui::PopID();
        }
        ImGui::SameLine();
        bool bFrozen = _bCallTreeFrozen == SW_TRUE;
        if ( ImGui::Checkbox( "Freeze##callTree", &bFrozen ) )
            _bCallTreeFrozen = bFrozen ? SW_TRUE : SW_FALSE;

        if ( _bCallTreeFrozen == SW_FALSE )
        {
            uint64 windowBegin{ 0 };
            uint64 windowEnd{ 0 };
            if ( timeline.collectRecentFrames( _timelineFrameCount, _listCallThread, windowBegin, windowEnd ) == false )
                _listCallThread.clear();
            ProfilerCallTree::compute( _listCallThread, _listCallNode );
            ProfilerCallTree::sortChildrenByTotal( _listCallNode );
            ProfilerCallTree::collectRoots( _listCallNode, _listCallRoot );
        }
        if ( _listCallRoot.empty() )
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
        uint32 shownThread = ProfilerCallTree::kNoParent;
        for ( const uint32 root : _listCallRoot )
        {
            const uint32 threadIndex = _listCallNode[root]._threadIndex;
            if ( threadIndex != shownThread && threadIndex < _listCallThread.size() )
            {
                shownThread = threadIndex;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored( EditorColorUtil::toImVec4( ProfilerPanelInternal::kCallThreadText ), "%s", _listCallThread[threadIndex]._name.c_str() );
            }
            drawCallTreeNode( root );
        }
        ImGui::EndTable();
    }

    void ProfilerPanel::drawCallTreeNode( uint32 nodeIndex )
    {
        const FrameProfiler*    pProfiler = editor::findSubsystem<FrameProfiler>();
        const ProfilerCallNode& node      = _listCallNode[nodeIndex];
        const utf8*             pName     = pProfiler != nullptr ? pProfiler->findScopeName( node._slot ) : nullptr;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID( static_cast<int32>( nodeIndex ) );
        const bool         bLeaf     = node._firstChild == ProfilerCallTree::kNoParent;
        ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanFullWidth | ( bLeaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0 );
        // 바깥쪽 두 단계는 펼쳐 둔다. 프레임과 큰 단계가 바로 보이게 하기 위해서다.
        if ( node._depth < 2 )
            treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool    bOpen      = ImGui::TreeNodeEx( pName != nullptr ? pName : "?", treeFlags );
        const float64 frameCount = static_cast<float64>( MathUtil::max( _timelineFrameCount, 1u ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._totalNanos ) / constant::kNanosecondsPerMillisecond / frameCount );
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._selfNanos ) / constant::kNanosecondsPerMillisecond / frameCount );
        ImGui::TableNextColumn();
        ImGui::Text( "%u", node._callCount );
        if ( bOpen && bLeaf == false )
        {
            for ( uint32 child = node._firstChild; child != ProfilerCallTree::kNoParent; child = _listCallNode[child]._nextSibling )
            {
                drawCallTreeNode( child );
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
} // namespace sw::editor
