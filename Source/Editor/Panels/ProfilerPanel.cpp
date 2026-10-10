#include "pch.h"

#include "Editor/Panels/ProfilerPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Commands/EditorTracyLauncher.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Profiling/ProfilerBackend.h"
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
            template <uint32 N>
            static void formatBytes( uint64 bytes, fixed_string<N>& out )
            {
                if ( bytes < 1024 )
                    formatstring( out.data(), out.capacity(), "%# B", bytes );

                else if ( bytes < uint64{ 1024 } * 1024 )
                    formatstring( out.data(), out.capacity(), "%# KB", Fmt( static_cast<float64>( bytes ) / 1024.0, Format().precision( 2 ) ) );
                else if ( bytes < uint64{ 1024 } * 1024 * 1024 )
                    formatstring( out.data(), out.capacity(), "%# MB", Fmt( static_cast<float64>( bytes ) / ( 1024.0 * 1024.0 ), Format().precision( 2 ) ) );
                else
                    formatstring( out.data(), out.capacity(), "%# GB", Fmt( static_cast<float64>( bytes ) / ( 1024.0 * 1024.0 * 1024.0 ), Format().precision( 2 ) ) );
            }

            /** @brief 아는 값은 `formatBytes` 로, 드라이버가 답하지 않은 값은 "Unknown" 으로 적습니다. */
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

            /** @brief Timeline 탭이 고를 수 있는 최근 프레임 수입니다. */
            static constexpr uint32 kArrTimelineFrameChoice[] = { 1, 4, 16 };
            /** @brief 휠 한 칸의 확대 배율입니다(1 보다 작으면 확대). */
            static constexpr float32 kTimelineWheelZoomFactor = 0.8f;
            /** @brief 스레드 이름 칸의 폭입니다(UI 단위, DPI 배율을 곱한다). */
            static constexpr float32 kTimelineLabelWidth = 120.0f;

            /** @brief 슬롯마다 다른 색입니다(색상환을 슬롯 번호로 돌린다). */
            static ImU32 computeSlotColor( uint32 slot )
            {
                constexpr uint32 kHueStepDegrees = 47;
                constexpr uint32 kDegreesPerTurn = 360;
                const float32    hue             = static_cast<float32>( ( slot * kHueStepDegrees ) % kDegreesPerTurn ) / static_cast<float32>( kDegreesPerTurn );
                float32          red{ 0.0f };
                float32          green{ 0.0f };
                float32          blue{ 0.0f };
                ImGui::ColorConvertHSVtoRGB( hue, 0.55f, 0.85f, red, green, blue );
                return ImGui::GetColorU32( ImVec4{ red, green, blue, 1.0f } );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( ProfilerPanel, "profiler", EditorPanelCategory::Core, 500 );

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
    {
    }

    void ProfilerPanel::drawContent()
    {
        // 패널이 열려 있는 동안 엔진 프로파일러를 켜 두고 프레임마다 한 칸 담는다(탭과 무관 — 탭을 바꿔도 그래프가 끊기지 않게).
        FrameProfiler* pProfiler = editor::getService<FrameProfiler>();
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
            const bool bTimelineTab = ImGui::BeginTabItem( "Timeline" );
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
            FrameProfiler* pProfiler = editor::getService<FrameProfiler>();
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

        // 멈춤 · 스파이크 · 캡처 파일(유니티 Profiler 의 Record 끄기 · 언리얼 stat hitches · Save/Load).
        ImGui::SameLine();
        if ( _bCapturePaused == SW_TRUE )
        {
            if ( ImGui::SmallButton( "Resume" ) )
            {
                _bCapturePaused        = SW_FALSE;
                _rowQuery._frameOffset = 0;
            }
            EditorWidgets::drawTooltip( "Capturing is paused (a spike or an opened capture) - resume live capture" );
        }
        else if ( ImGui::SmallButton( "Pause" ) )
            _bCapturePaused = SW_TRUE;
        EditorSelfTestMarks::note( "profiler.capture.pause" );
        ImGui::SameLine();
        bool bPauseOnSpike = _bPauseOnSpike == SW_TRUE;
        if ( ImGui::Checkbox( "Pause on spike", &bPauseOnSpike ) )
            _bPauseOnSpike = bPauseOnSpike ? SW_TRUE : SW_FALSE;
        EditorSelfTestMarks::note( "profiler.capture.pauseOnSpike" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 4.0f );
        ImGui::DragFloat( "##spikeMs", &_spikeThresholdMs, 0.1f, 0.1f, 1000.0f, "%.1f ms" );
        EditorSelfTestMarks::note( "profiler.capture.spikeMs" );
        EditorWidgets::drawTooltip( "GT.Frame above this pauses capturing (and a running play session) on that frame" );
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
        EditorWidgets::drawTooltip( "Save the captured window to Saved/Profiler/ProfilerCapture.txt (long captures: Tracy)" );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Load" ) )
        {
            const string path = getCaptureFilePath();
            if ( _scopeHistory.loadFromFile( path ) )
            {
                _bCapturePaused        = SW_TRUE; // 연 캡처를 보는 동안 새 프레임이 덮지 않게
                _rowQuery._frameOffset = 0;
                SW_LOG_INFO( "Profiler capture loaded: %#", path.c_str() );
            }
            else
                SW_LOG_WARNING( "Profiler capture could not be loaded: %#", path.c_str() );
        }
        EditorSelfTestMarks::note( "profiler.capture.load" );
        EditorWidgets::drawTooltip( "Open Saved/Profiler/ProfilerCapture.txt (pauses live capture)" );

        // Tracy — 시간축 분석은 외부 뷰어가 한다. 상태 한 줄과 여는 버튼.
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
            ImGui::TextColored( ImVec4{ 1.0f, 0.6f, 0.3f, 1.0f }, "%s", EditorTracyLauncher::describeResult( _lastTracyResult ) );

        // RenderDoc — GPU 프레임 캡처(네 백엔드). 붙어 있지 않으면 단추가 회색이고 이유를 툴팁으로 보인다.
        ImGui::SameLine();
        ImGui::TextUnformatted( "|" );
        ImGui::SameLine();
        const bool bRenderDoc = RenderDocCapture::isAvailable();
        ImGui::BeginDisabled( bRenderDoc == false );
        if ( ImGui::SmallButton( "RenderDoc Capture" ) )
            RenderDocCapture::triggerCapture();
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Open UI" ) && RenderDocCapture::launchReplayUI() == false )
            SW_LOG_WARNING( "RenderDoc replay UI could not be started" );
        ImGui::EndDisabled();
        if ( bRenderDoc == false && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", "Start with -renderdoc or from RenderDoc — RenderDoc 이 이 프로세스에 붙어 있지 않습니다" );
    }

    void ProfilerPanel::drawTimelineTab()
    {
        FrameProfiler* pProfiler = editor::getService<FrameProfiler>();
        if ( pProfiler == nullptr )
        {
            ImGui::TextDisabled( "Engine profiler is not available" );
            return;
        }
        ProfilerTimeline& timeline   = pProfiler->getTimeline();
        const bool        bRecording = timeline.isRecording();
        if ( ImGui::Button( bRecording ? "Stop Recording" : "Record" ) )
        {
            // 녹화는 계측이 켜져 있어야 쌓인다(스코프가 시계를 읽어야 한다). 켤 때 지난 사건을 비운다.
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
        EditorWidgets::drawTooltip( "Record profile scopes per thread into a ring (8192 events per thread) — only while recording" );

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
        EditorWidgets::drawTooltip( "Keep recording but stop updating the view" );
        ImGui::SameLine();
        if ( ImGui::SmallButton( "Fit" ) )
            _bTimelineZoomed = SW_FALSE;
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
        if ( _listTimelineThread.empty() )
        {
            ImGui::TextDisabled( "%s", bRecording ? "Waiting for frames..." : "Press Record to capture the last frames per thread." );
            return;
        }
        timeline.collectFrameBegins( _timelineViewBegin, _timelineViewEnd, _listTimelineFrameBegin );
        drawTimelineCanvas();
    }

    void ProfilerPanel::drawTimelineCanvas()
    {
        const FrameProfiler* pProfiler  = editor::getService<FrameProfiler>();
        const float32        dpiScale   = EditorThemeUtil::getDpiScale();
        const float32        labelWidth = ProfilerPanelInternal::kTimelineLabelWidth * dpiScale;
        const float32        laneHeight = ImGui::GetTextLineHeight() + 4.0f * dpiScale;
        const float32        rowGap     = 2.0f * dpiScale;
        const ImVec2         origin     = ImGui::GetCursorScreenPos();
        const float32        totalWidth = MathUtil::max( ImGui::GetContentRegionAvail().x, labelWidth + 1.0f );
        const float32        trackWidth = totalWidth - labelWidth;
        const float32        trackLeft  = origin.x + labelWidth;

        ProfilerTimelineLayout::layoutRects( _listTimelineThread, _timelineViewBegin, _timelineViewEnd, trackWidth, _listTimelineRect, _listTimelineLaneCount );
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
        vector<float32> listRowTop( _listTimelineThread.size(), 0.0f );
        float32         rowTop = origin.y;
        for ( size_t threadIndex = 0; threadIndex < _listTimelineThread.size(); ++threadIndex )
        {
            listRowTop[threadIndex] = rowTop;
            const float32 rowHeight = static_cast<float32>( _listTimelineLaneCount[threadIndex] ) * laneHeight;
            pDrawList->AddRectFilled( ImVec2{ origin.x, rowTop }, ImVec2{ origin.x + totalWidth, rowTop + rowHeight },
                                      ImGui::GetColorU32( ImGuiCol_FrameBg, ( threadIndex % 2 == 0 ) ? 0.6f : 0.3f ) );
            pDrawList->AddText( ImVec2{ origin.x + rowGap * 2.0f, rowTop + rowGap }, ImGui::GetColorU32( ImGuiCol_Text ),
                                _listTimelineThread[threadIndex]._name.c_str() );
            rowTop += rowHeight + rowGap;
        }

        pDrawList->PushClipRect( ImVec2{ trackLeft, origin.y }, ImVec2{ origin.x + totalWidth, origin.y + totalHeight }, true );
        const ProfilerTimelineRect* pHoveredRect = nullptr;
        for ( const ProfilerTimelineRect& rect : _listTimelineRect )
        {
            const ProfilerTimelineEvent& event = _listTimelineThread[rect._threadIndex]._listEvent[rect._eventIndex];
            const ImVec2                 minPoint{ trackLeft + rect._x0, listRowTop[rect._threadIndex] + static_cast<float32>( rect._depth ) * laneHeight };
            const ImVec2                 maxPoint{ trackLeft + rect._x1, minPoint.y + laneHeight - 1.0f };
            pDrawList->AddRectFilled( minPoint, maxPoint, ProfilerPanelInternal::computeSlotColor( event._slot ) );
            const utf8* pName = pProfiler != nullptr ? pProfiler->findScopeName( event._slot ) : nullptr;
            if ( rect._bShowsLabel == SW_TRUE && pName != nullptr )
            {
                pDrawList->PushClipRect( minPoint, maxPoint, true );
                pDrawList->AddText( ImVec2{ minPoint.x + rowGap, minPoint.y + rowGap }, IM_COL32( 0, 0, 0, 255 ), pName );
                pDrawList->PopClipRect();
            }
            const bool bUnderMouse = bHovered && minPoint.x <= mouse.x && mouse.x <= maxPoint.x && minPoint.y <= mouse.y && mouse.y <= maxPoint.y;
            if ( bUnderMouse )
                pHoveredRect = &rect;
        }
        // 프레임 경계 — 세로선.
        for ( const uint64 frameBegin : _listTimelineFrameBegin )
        {
            const float32 lineX = trackLeft + ProfilerTimelineLayout::computeX( frameBegin, _timelineViewBegin, _timelineViewEnd, trackWidth );
            pDrawList->AddLine( ImVec2{ lineX, origin.y }, ImVec2{ lineX, origin.y + totalHeight }, IM_COL32( 255, 255, 255, 160 ), 1.0f );
        }
        pDrawList->PopClipRect();

        if ( pHoveredRect != nullptr )
        {
            const ProfilerTimelineEvent& event = _listTimelineThread[pHoveredRect->_threadIndex]._listEvent[pHoveredRect->_eventIndex];
            const utf8*                  pName = pProfiler != nullptr ? pProfiler->findScopeName( event._slot ) : nullptr;
            ImGui::BeginTooltip();
            ImGui::TextUnformatted( pName != nullptr ? pName : "?" );
            ImGui::Text( "%.1f us, depth %u", static_cast<float64>( event._endNanos - event._beginNanos ) / 1000.0, static_cast<uint32>( event._depth ) );
            ImGui::TextDisabled( "%s", _listTimelineThread[pHoveredRect->_threadIndex]._name.c_str() );
            ImGui::EndTooltip();
        }
    }

    void ProfilerPanel::drawFrameGraph()
    {
        // 세 줄 — 게임 스레드 · 렌더 스레드 · GPU. 같은 축(ms)으로 그려야 누가 프레임을 정하는지 보인다.
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
        const float32 maxMs = maxMicro / 1000.0f;
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
            // 그래프의 한 프레임을 누르면 표의 "Last" 가 그 프레임 값이 된다(유니티 Profiler 의 프레임 고르기). 고른 프레임에 세로줄.
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
                ImGui::GetWindowDrawList()->AddLine( ImVec2{ lineX, plotMin.y }, ImVec2{ lineX, plotMax.y }, IM_COL32( 255, 200, 60, 255 ) );
            }
        }
        if ( _rowQuery._frameOffset > 0 )
        {
            ImGui::TextColored( ImVec4{ 1.0f, 0.8f, 0.25f, 1.0f }, "Last column shows the frame %u frames ago", _rowQuery._frameOffset );
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Latest" ) )
                _rowQuery._frameOffset = 0;
        }
    }

    void ProfilerPanel::drawScopeTable( ProfilerScopeKind kind )
    {
        EditorWidgets::drawSearchField( "##profilerScopeFilter", _arrFilter, constant::kMaxBuffer128, "Filter scopes..." );
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

        // 머리줄을 누르면 정렬이 바뀐다. 집계 쪽 정렬을 그대로 쓴다(ImGui 는 열 번호만 넘긴다).
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
        const float32 dt          = ImGui::GetIO().DeltaTime;
        const float32 frameTimeMs = dt * 1000.0f;
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
            if ( _bCatalogDirty == SW_TRUE && _catalogJob.isPending() == false )
            {
                _catalogJob.request();
                _bCatalogDirty = SW_FALSE;
            }

            EditorResourceCatalogCounts counts{};
            if ( _catalogJob.take( counts ) )
                _catalogCounts = counts;

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
            TaskManager* pTaskManager = editor::getService<TaskManager>();
            if ( pTaskManager != nullptr )
            {
                ImGui::BulletText( "Worker Threads: %u", pTaskManager->getWorkerCount() );
                ImGui::BulletText( "Task System: Active" );
            }
            else
            {
                EditorWidgets::drawEmptyHint( "TaskManager is not active." );
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
                // 집계는 ImGui 를 모르는 EditorSceneCommands 가 한다(테스트가 붙어 있다).
                // 이 패널은 그려 주기만 한다. 타입 이름을 여기서 알 필요가 없다.
                const EditorSceneCommands::SceneStatistics stats =
                    EditorSceneCommands::collectSceneStatistics( pScene->getObjectManager() );

                ImGui::BulletText( "Total GameObjects: %u (Roots: %u)", stats._objectCount, stats._rootCount );
                ImGui::BulletText( "Total Attached Components: %u", stats._componentCount );

                if ( stats._listDistribution.empty() )
                    EditorWidgets::drawEmptyHint( "No components in the active scene." );
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
                EditorWidgets::drawEmptyHint( "No active scene loaded." );
            }
        }

        ImGui::Separator();
    }

    void ProfilerPanel::drawGPUMemoryTab()
    {
        EditorContext* pContext = EditorContext::get();
        IRHIDevice*    pDevice  = ( pContext != nullptr ) ? pContext->getRHIDevice() : nullptr;
        if ( pDevice == nullptr )
        {
            EditorWidgets::drawEmptyHint( "No RHI device." );
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

                // 로그 보고와 같은 순서(바이트가 큰 줄부터)와 같은 이름이다. 크기 모름은 바이트 합에 넣지 않고 따로 센다.
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
            // 드라이버가 답하지 않은 칸은 "Unknown" 이다 — 0 으로 지어내지 않는다.
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
        MemoryProfiler* pProfiler = editor::getService<MemoryProfiler>();
        if ( pProfiler == nullptr )
        {
            EditorWidgets::drawEmptyHint( "MemoryProfiler is not active." );
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

                // 지금 살아 있는 바이트가 큰 태그부터. 한 번도 할당하지 않은 태그는 줄을 내지 않는다(Unknown 은 늘 낸다 — 진입점이 빠진 몫이다).
                const uint64 liveBytes = profiler.getLiveAllocatedBytes();
                for ( const MemoryTag tag : profiler.makeTagOrderByLiveBytes() )
                {
                    const auto& stats = profiler.getStats( tag );
                    if ( tag != MemoryTag::Unknown && stats._totalAllocatedBytes.load() == 0 )
                        continue;

                    const uint64 currentBytes = stats._currentAllocatedBytes.load();
                    ProfilerPanelInternal::formatBytes( currentBytes, arrBytesBuf );
                    ProfilerPanelInternal::formatBytes( stats._totalAllocatedBytes.load(), arrTotalBuf );

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", MemoryProfiler::getMemoryTagName( tag ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", arrBytesBuf.c_str() );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.1f%%", liveBytes == 0 ? 0.0 : static_cast<float64>( currentBytes ) * 100.0 / static_cast<float64>( liveBytes ) );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%u", static_cast<uint32>( stats._currentAllocationCount.load() ) );
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
                            break; // 최대 100개만 표시

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
        float32 frameMicro{ 0.0f };
        if ( _bPauseOnSpike == SW_FALSE || _scopeHistory.readValue( "GT.Frame", 0, frameMicro ) == false || frameMicro < _spikeThresholdMs * 1000.0f )
            return;
        _bCapturePaused        = SW_TRUE;
        _rowQuery._frameOffset = 0;
        if ( EditorPlaySession::isPlaying() )
            EditorPlaySession::pause();
        SW_LOG_INFO( "Profiler paused on a spike: GT.Frame %# ms (threshold %# ms)", Fmt( static_cast<float64>( frameMicro ) / 1000.0, Format().precision( 2 ) ),
                     Fmt( static_cast<float64>( _spikeThresholdMs ), Format().precision( 1 ) ) );
    }

    string ProfilerPanel::getCaptureFilePath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( path::kSavedFolder, "Profiler" ), "ProfilerCapture.txt" );
    }

    void ProfilerPanel::drawCallTreeTab()
    {
        FrameProfiler* pProfiler = editor::getService<FrameProfiler>();
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
        EditorWidgets::drawTooltip( "The call tree folds the timeline recording (same recording as the Timeline tab)" );
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
                ImGui::TextColored( ImVec4{ 0.6f, 0.8f, 1.0f, 1.0f }, "%s", _listCallThread[threadIndex]._name.c_str() );
            }
            drawCallTreeNode( root );
        }
        ImGui::EndTable();
    }

    void ProfilerPanel::drawCallTreeNode( uint32 nodeIndex )
    {
        const FrameProfiler*    pProfiler = editor::getService<FrameProfiler>();
        const ProfilerCallNode& node      = _listCallNode[nodeIndex];
        const utf8*             pName     = pProfiler != nullptr ? pProfiler->findScopeName( node._slot ) : nullptr;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID( static_cast<int32>( nodeIndex ) );
        const bool         bLeaf     = node._firstChild == ProfilerCallTree::kNoParent;
        ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_SpanFullWidth | ( bLeaf ? ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen : 0 );
        // 바깥 두 겹은 펼쳐 둔다(프레임 → 큰 단계가 바로 보이게).
        if ( node._depth < 2 )
            treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
        const bool    bOpen       = ImGui::TreeNodeEx( pName != nullptr ? pName : "?", treeFlags );
        const float64 kNanosPerMs = 1000000.0;
        const float64 frameCount  = static_cast<float64>( MathUtil::max( _timelineFrameCount, 1u ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._totalNanos ) / kNanosPerMs / frameCount );
        ImGui::TableNextColumn();
        ImGui::Text( "%.3f", static_cast<float64>( node._selfNanos ) / kNanosPerMs / frameCount );
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
