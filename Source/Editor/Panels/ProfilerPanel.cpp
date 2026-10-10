#include "pch.h"

#include "Editor/Panels/ProfilerPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Commands/EditorSceneCommands.h"
#include "Editor/Common/Commands/EditorTracyLauncher.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"

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
        , _bCatalogDirty{ SW_TRUE }
        , _bCollect{ SW_TRUE }
        , _bTracyTried{ SW_FALSE }
        , _reserved{ 0 }
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
            std::ignore = _scopeHistory.capture( *pProfiler );
        }

        if ( ImGui::BeginTabBar( "ProfilerTabs" ) )
        {
            if ( ImGui::BeginTabItem( "CPU Scopes" ) )
            {
                drawScopeTab( ProfilerScopeKind::Cpu );
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
            _scopeHistory.reset();

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
        if ( ImGui::SmallButton( "Open UI" ) && RenderDocCapture::launchReplayUi() == false )
            SW_LOG_WARNING( "RenderDoc replay UI could not be started" );
        ImGui::EndDisabled();
        if ( bRenderDoc == false && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
            ImGui::SetTooltip( "%s", "Start with -renderdoc or from RenderDoc — RenderDoc 이 이 프로세스에 붙어 있지 않습니다" );
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
        IRHIDevice*    pDevice  = ( pContext != nullptr ) ? pContext->getRhiDevice() : nullptr;
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
} // namespace sw::editor
