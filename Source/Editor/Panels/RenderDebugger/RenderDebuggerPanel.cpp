#include "pch.h"

#include "Editor/Panels/RenderDebugger/RenderDebuggerPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorVirtualList.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( RenderDebuggerPanel, RenderDebuggerPanel::kPanelID, EditorPanelCategory::Tool, RenderDebuggerPanel::kMenuOrder );

    namespace ed = ax::NodeEditor;

    namespace
    {
        struct RenderDebuggerPanelInternal
        {
            static constexpr float32 kEventListColumns  = 46.0f; ///< 기록 목록 폭(글자 칸 수) — 패스 이름 · 번호 · 인덱스 수
            static constexpr float32 kBufferListColumns = 36.0f; ///< 버퍼 목록 폭(글자 칸 수)
            static constexpr float32 kTileCellMax       = 12.0f; ///< 가림 타일 칸 한 변의 위 한계(px)
            static constexpr int32   kTableFlags        = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
            static constexpr float64 kBytesPerMegabyte  = static_cast<float64>( constant::memory::kBytesPerMegabyte );
            static constexpr int32   kDepthDecimals     = 4;  ///< 깊이 값의 소수 자리(가림 타일 요약)
            static constexpr int32   kStepForward       = 1;  ///< 다음 단추 — 기록 한 칸 앞
            static constexpr int32   kStepBackward      = -1; ///< 이전 단추 — 기록 한 칸 뒤
        };
    } // namespace

    RenderDebuggerPanel::RenderDebuggerPanel()
        : EditorPanel( false )
        , _viewModel{}
        , _pState{ nullptr }
        , _nodeGraph{}
        , _placedLayoutSerial{ 0 }
        , _drawnGraphNodeCount{ 0 }
        , _selectedEvent{ 0 }
    {
        _nodeGraph.setPinTypeColors( { SW_THEME_COLOR( "Graph", "FlowPin" ) } );
    }

    void RenderDebuggerPanel::shutdown()
    {
        _viewModel.setFrameDebuggerEnabled( false );
        _nodeGraph.shutdown();
    }

    int32 RenderDebuggerPanel::getStopEventIndex() const
    {
        return _pState != nullptr && _pState->_stopEvent.has_value() ? static_cast<int32>( *_pState->_stopEvent ) : kNoStopEvent;
    }

    uint32 RenderDebuggerPanel::getGraphPassCount() const
    {
        if ( _pState == nullptr || _pState->_graphIndex >= _pState->_listGraph.size() )
            return 0;
        return static_cast<uint32>( _pState->_listGraph[_pState->_graphIndex]._listPass.size() );
    }

    uint32 RenderDebuggerPanel::getBufferRowCount() const
    {
        if ( _pState == nullptr || _pState->_selectedBuffer.has_value() == false )
            return 0;
        return _pState->_bufferLayout.getRowCount( _pState->_bufferContent._byte.size() );
    }

    bool RenderDebuggerPanel::beginTab( const utf8* pLabel, const utf8* pMark, RenderDebuggerTab tab )
    {
        const bool bOpen = ImGui::BeginTabItem( pLabel );
        EditorSelfTestMarks::record( pMark );
        if ( bOpen && _pState->_tab != tab )
            _viewModel.selectTab( tab );
        return bOpen;
    }

    void RenderDebuggerPanel::drawContent()
    {
        const RenderDebuggerSources sources = RenderDebuggerViewModel::findEditorSources();
        _pState                             = &_viewModel.gather( sources );
        if ( sources._pRegistry == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRenderDebug", "NoRenderer" ) );
            return;
        }

        if ( ImGui::BeginTabBar( "RenderDebuggerTabs" ) )
        {
            if ( beginTab( SW_LOCKEY( "EditorRenderDebug", "FrameDebuggerTab" ), "renderDebugger.tab.frame", RenderDebuggerTab::FrameDebugger ) )
            {
                drawFrameDebuggerTab();
                ImGui::EndTabItem();
            }
            if ( beginTab( SW_LOCKEY( "EditorRenderDebug", "RenderGraphTab" ), "renderDebugger.tab.graph", RenderDebuggerTab::RenderGraph ) )
            {
                drawRenderGraphTab();
                ImGui::EndTabItem();
            }
            if ( beginTab( SW_LOCKEY( "EditorRenderDebug", "BuffersTab" ), "renderDebugger.tab.buffers", RenderDebuggerTab::Buffers ) )
            {
                drawBuffersTab();
                ImGui::EndTabItem();
            }
            if ( beginTab( SW_LOCKEY( "EditorRenderDebug", "OcclusionTab" ), "renderDebugger.tab.occlusion", RenderDebuggerTab::Occlusion ) )
            {
                drawOcclusionTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    // ------------------------------------------------------------------------------
    // 프레임 디버거
    // ------------------------------------------------------------------------------
    void RenderDebuggerPanel::drawFrameDebuggerTab()
    {
        const RenderDebuggerState& state    = *_pState;
        bool                       bEnabled = state._frameDebugger._bEnabled != SW_FALSE;
        if ( ImGui::Checkbox( SW_LOCKEY( "EditorRenderDebug", "Enable" ), &bEnabled ) )
            _viewModel.setFrameDebuggerEnabled( bEnabled );
        EditorSelfTestMarks::record( "renderDebugger.enable" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderDebug", "EnableTip" ) );
        ImGui::BeginDisabled( bEnabled == false || state._listEvent.empty() );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EditorRenderDebug", "Previous" ) ) )
            _viewModel.stepFrameDebugger( RenderDebuggerPanelInternal::kStepBackward );
        EditorSelfTestMarks::record( "renderDebugger.prev" );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EditorRenderDebug", "Next" ) ) )
            _viewModel.stepFrameDebugger( RenderDebuggerPanelInternal::kStepForward );
        EditorSelfTestMarks::record( "renderDebugger.next" );
        ImGui::EndDisabled();
        ImGui::SameLine();
        if ( state._stopEvent.has_value() )
            ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EditorRenderDebug", "StopAt",
                                                     TextArgumentList()
                                                         .addInteger( "event", static_cast<int64>( *state._stopEvent ) + 1 )
                                                         .addInteger( "eventCount", static_cast<int64>( state._listEvent.size() ) )
                                                         .addText( "pass", state._frameDebugger._stopPass.c_str() )
                                                         .addInteger( "draw", static_cast<int64>( state._frameDebugger._stopDraw ) ) )
                                        .c_str() );
        else if ( bEnabled )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderDebug", "WholeFrame", TextArgumentList().addInteger( "eventCount", static_cast<int64>( state._listEvent.size() ) ) ).c_str() );
        if ( bEnabled == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRenderDebug", "FrameDebuggerHint" ) );
            return;
        }

        // 왼쪽: 기록(패스 실행 순서 · 패스 안 번호). 멈춤 뒤 기록은 흐리게 — 그리지 않았다.
        ImGui::BeginChild( "##events", EditorChrome::measureTextExtent( EditorTextExtent::ofText( RenderDebuggerPanelInternal::kEventListColumns, 0.0f ) ), ImGuiChildFlags_Borders );
        for ( uint32 index = 0; index < static_cast<uint32>( state._listEvent.size() ); ++index )
        {
            const FrameDebugDrawEvent& event = state._listEvent[index];
            ImGui::PushID( static_cast<int32>( index ) );
            const bool bStop = state._stopEvent.has_value() && *state._stopEvent == index;
            if ( event._bSkipped != SW_FALSE )
                ImGui::BeginDisabled();
            if ( ImGui::Selectable( event._passName.c_str(), bStop || _selectedEvent == index ) )
            {
                _selectedEvent = index;
                _viewModel.selectEvent( index );
            }
            EditorSelfTestMarks::recordFormat( "renderDebugger.event.%#", index );
            ImGui::SameLine();
            ImGui::TextDisabled( "#%u  %u idx  x%u", event._drawIndex, event._indexCount, event._groupCount );
            if ( event._bSkipped != SW_FALSE )
                ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // 오른쪽: 고른 드로우(없으면 멈춤 지점)의 PSO · 바인딩.
        ImGui::BeginChild( "##eventDetail", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const uint32 detailIndex = state._stopEvent.value_or( _selectedEvent );
        if ( detailIndex < state._listEvent.size() )
            drawEventDetail( state._listEvent[detailIndex] );
        ImGui::EndChild();
    }

    void RenderDebuggerPanel::drawEventDetail( const FrameDebugDrawEvent& event )
    {
        ImGui::Text( "%s  #%u", event._passName.c_str(), event._drawIndex );
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderDebug", "DrawSummary",
                                                    TextArgumentList()
                                                        .addInteger( "pso", static_cast<int64>( event._pso ) )
                                                        .addInteger( "indexCount", static_cast<int64>( event._indexCount ) )
                                                        .addInteger( "groupCount", static_cast<int64>( event._groupCount ) )
                                                        .addInteger( "triangleCount", static_cast<int64>( event._triangleCount ) ) )
                                       .c_str() );
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderDebug", "TargetsHint" ) );
        ImGui::Separator();
        if ( event._listBinding.empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderDebug", "NoBindings" ) );
            return;
        }
        for ( const string& binding : event._listBinding )
        {
            // 멤버 줄(들여 쓴 줄)은 흐리게 — 슬롯 줄이 눈에 먼저 들어오게.
            if ( binding.empty() == false && binding[0] == ' ' )
                ImGui::TextDisabled( "%s", binding.c_str() );
            else
                ImGui::TextUnformatted( binding.c_str() );
        }
    }

    // ------------------------------------------------------------------------------
    // 렌더 그래프
    // ------------------------------------------------------------------------------
    void RenderDebuggerPanel::drawRenderGraphTab()
    {
        const RenderDebuggerState& state = *_pState;
        if ( ImGui::Button( SW_LOCKEY( "EditorRenderDebug", "Capture" ) ) )
            _viewModel.requestGraphCapture();
        EditorSelfTestMarks::record( "renderDebugger.capture" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorRenderDebug", "CaptureTip" ) );
        for ( uint32 graphIndex = 0; graphIndex < static_cast<uint32>( state._listGraph.size() ); ++graphIndex )
        {
            ImGui::SameLine();
            if ( ImGui::RadioButton( state._listGraph[graphIndex]._graphName.c_str(), state._graphIndex == graphIndex ) )
                _viewModel.selectGraph( graphIndex );
            EditorSelfTestMarks::recordFormat( "renderDebugger.graph.%#", state._listGraph[graphIndex]._graphName );
        }
        if ( state._graphIndex >= state._listGraph.size() )
        {
            _drawnGraphNodeCount = 0;
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRenderDebug", "NoGraph" ) );
            return;
        }
        const RenderGraphSnapshot& graph = state._listGraph[state._graphIndex];
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EditorRenderDebug", "GraphSummary",
                                                    TextArgumentList()
                                                        .addInteger( "passCount", static_cast<int64>( graph._listPass.size() ) )
                                                        .addInteger( "levelCount", static_cast<int64>( graph._levelCount ) )
                                                        .addInteger( "resourceCount", static_cast<int64>( graph._listResource.size() ) )
                                                        .addInteger( "transitionCount", static_cast<int64>( graph._transitionCount ) ) )
                                       .c_str() );
        drawGraphCanvas( graph );
    }

    void RenderDebuggerPanel::drawGraphCanvas( const RenderGraphSnapshot& graph )
    {
        const RenderDebuggerState& state  = *_pState;
        const RenderGraphLayout&   layout = state._graphLayout;
        if ( _nodeGraph.beginCanvas( "RenderGraphCanvas", "", "renderDebugger.graph" ) == false )
            return;

        // 배치가 바뀌면(새 사본 · 다른 그래프) 노드를 다시 놓고 화면에 맞춘다. 사본은 읽기 전용이라 사람이 옮긴 자리는 다음 사본까지만 남는다.
        const bool bPlace = state._layoutSerial != _placedLayoutSerial;
        uint32     drawn  = 0;
        for ( const RenderGraphLayoutNode& node : layout._listNode )
        {
            const RenderGraphSnapshotPass& pass = graph._listPass[node._pass];
            _nodeGraph.beginNode( RenderGraphLayout::getNodeID( node._pass ) );
            ImGui::TextUnformatted( pass._name.c_str() );
            _nodeGraph.beginPin( RenderGraphLayout::getInputPinID( node._pass ), true );
            _nodeGraph.drawPinIcon( 0, true );
            _nodeGraph.endPin();
            ImGui::SameLine();
            const bool bAsync = pass._queue == RHIQueueKind::AsyncCompute;
            if ( bAsync )
                ImGui::TextDisabled( "L%u  async #%u", pass._level, pass._asyncBatch );
            else
                ImGui::TextDisabled( "L%u", pass._level );
            ImGui::SameLine();
            _nodeGraph.beginPin( RenderGraphLayout::getOutputPinID( node._pass ), false );
            _nodeGraph.drawPinIcon( 0, true );
            _nodeGraph.endPin();
            // 전이 수 · GPU 시간 겹쳐 보기(언리얼 RDG Insights 의 패스 줄) — 재지 않은 패스는 줄을 비운다.
            const bool bMeasured = node._pass < state._listPassMilliseconds.size() && state._listPassMilliseconds[node._pass] >= 0.0f;
            if ( bMeasured )
                ImGui::Text( "%u transitions  %.3f ms", pass._transitionCount, static_cast<float64>( state._listPassMilliseconds[node._pass] ) );
            else
                ImGui::Text( "%u transitions", pass._transitionCount );
            _nodeGraph.endNode();
            if ( bPlace )
                ed::SetNodePosition( EditorNodeGraphID::toNodeID( RenderGraphLayout::getNodeID( node._pass ) ), ImVec2( node._position._x, node._position._y ) );
            ++drawn;
        }
        for ( uint32 linkIndex = 0; linkIndex < static_cast<uint32>( layout._listLink.size() ); ++linkIndex )
        {
            const RenderGraphLayoutLink& link = layout._listLink[linkIndex];
            ed::Link( EditorNodeGraphID::toLinkID( RenderGraphLayout::getLinkID( linkIndex ) ), EditorNodeGraphID::toPinID( RenderGraphLayout::getOutputPinID( link._fromPass ) ),
                      EditorNodeGraphID::toPinID( RenderGraphLayout::getInputPinID( link._toPass ) ) );
        }
        if ( bPlace )
        {
            _placedLayoutSerial = state._layoutSerial;
            _nodeGraph.requestContentFit();
        }
        _drawnGraphNodeCount = drawn;
        _nodeGraph.finishCanvas( drawn );
    }

    // ------------------------------------------------------------------------------
    // 버퍼
    // ------------------------------------------------------------------------------
    void RenderDebuggerPanel::drawBuffersTab()
    {
        const RenderDebuggerState& state = *_pState;
        if ( ImGui::Button( SW_LOCKEY( "EditorRenderDebug", "RefreshList" ) ) )
            _viewModel.requestBufferList();
        EditorSelfTestMarks::record( "renderDebugger.bufferList" );
        ImGui::SameLine();
        ImGui::BeginDisabled( state._selectedBuffer.has_value() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorRenderDebug", "ReadBuffer" ) ) )
            _viewModel.refreshBuffer();
        EditorSelfTestMarks::record( "renderDebugger.readBuffer" );
        ImGui::EndDisabled();
        for ( uint32 scalar = 0; scalar < static_cast<uint32>( BufferViewScalar::Count ); ++scalar )
        {
            ImGui::SameLine();
            const BufferViewScalar value = static_cast<BufferViewScalar>( scalar );
            if ( ImGui::RadioButton( BufferViewLayout::getScalarLabel( value ), state._bufferLayout._scalar == value ) )
                _viewModel.setBufferScalar( value );
        }
        if ( state._listBuffer.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRenderDebug", "NoBuffers" ) );
            return;
        }

        ImGui::BeginChild( "##buffers", EditorChrome::measureTextExtent( EditorTextExtent::ofText( RenderDebuggerPanelInternal::kBufferListColumns, 0.0f ) ), ImGuiChildFlags_Borders );
        for ( uint32 index = 0; index < static_cast<uint32>( state._listBuffer.size() ); ++index )
        {
            const RenderBufferEntry& entry = state._listBuffer[index];
            ImGui::PushID( static_cast<int32>( index ) );
            if ( ImGui::Selectable( entry._name.c_str(), state._selectedBuffer == optional<uint32>{ index } ) )
                _viewModel.selectBuffer( index );
            EditorSelfTestMarks::recordFormat( "renderDebugger.buffer.%#", index );
            ImGui::SameLine();
            ImGui::TextDisabled( "[%u]  %u x %u B", entry._slot, entry._elementCount, entry._elementSize );
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild( "##bufferContent", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const BufferViewLayout& layout   = state._bufferLayout;
        const uint32            rowCount = layout.getRowCount( state._bufferContent._byte.size() );
        if ( state._bBufferReading != SW_FALSE )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderDebug", "ReadingBuffer" ) );
        else if ( state._selectedBuffer.has_value() && rowCount == 0 )
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorRenderDebug", "BufferNotRead" ) );
        if ( rowCount > 0 && ImGui::BeginTable( "##bufferRows", static_cast<int32>( layout._columnCount ) + 1, RenderDebuggerPanelInternal::kTableFlags ) )
        {
            // 번호 열 + 4 바이트 칸 — 숫자는 오른쪽 맞춤(표 규칙).
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableSetupColumn( "#" );
            for ( uint32 column = 0; column < layout._columnCount; ++column )
            {
                StringBuilder<constant::buffer::kMax32> header;
                header.appendFormat( "+%#", column * BufferViewLayout::kCellBytes );
                ImGui::TableSetupColumn( string( header.view() ).c_str() );
            }
            ImGui::TableHeadersRow();
            vector<string>    listCell;
            EditorVirtualList virtualList( static_cast<int32>( rowCount ) );
            while ( virtualList.step() )
            {
                for ( int32 row = virtualList.getBegin(); row < virtualList.getEnd(); ++row )
                {
                    if ( layout.formatRow( state._bufferContent._byte, static_cast<uint32>( row ), listCell ) == false )
                        continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled( "%d", row );
                    for ( const string& cell : listCell )
                    {
                        ImGui::TableNextColumn();
                        const float32 rightOffset = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( cell.c_str() ).x;
                        if ( rightOffset > 0.0f )
                            ImGui::SetCursorPosX( ImGui::GetCursorPosX() + rightOffset );
                        ImGui::TextUnformatted( cell.c_str() );
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
    }

    // ------------------------------------------------------------------------------
    // 가림 · 스트리밍
    // ------------------------------------------------------------------------------
    void RenderDebuggerPanel::drawOcclusionTab()
    {
        const RenderDebuggerState& state = *_pState;
        using Internal                   = RenderDebuggerPanelInternal;

        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EditorRenderDebug", "StreamingHeader" ) );
        const RenderStreamingCounters& streaming = state._streaming;
        ImGui::Text( "%s", SW_LOCKEYFORMAT( "EditorRenderDebug", "StreamingSummary",
                                            TextArgumentList()
                                                .addInteger( "textureCount", static_cast<int64>( streaming._textureCount ) )
                                                .addInteger( "belowWantedCount", static_cast<int64>( streaming._belowWantedCount ) )
                                                .addInteger( "missingMipCount", static_cast<int64>( streaming._missingMipCount ) )
                                                .addInteger( "uploadsWaiting", static_cast<int64>( streaming._uploadsWaiting ) ) )
                               .c_str() );
        ImGui::TextDisabled( "resident %.1f MB  pool %.1f MB  in flight %.1f MB", static_cast<float64>( streaming._residentBytes ) / Internal::kBytesPerMegabyte,
                             static_cast<float64>( streaming._poolBytes ) / Internal::kBytesPerMegabyte, static_cast<float64>( streaming._bytesInFlight ) / Internal::kBytesPerMegabyte );

        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EditorRenderDebug", "OcclusionHeader" ) );
        const shared_ptr<const OcclusionTileSnapshot>& pSnapshot = state._pOcclusion;
        if ( pSnapshot == nullptr || pSnapshot->isValid() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorRenderDebug", "NoOcclusion" ) );
            return;
        }
        const OcclusionTileStats& stats = state._occlusion;
        ImGui::Text( "%s", SW_LOCKEYFORMAT( "EditorRenderDebug", "OcclusionSummary",
                                            TextArgumentList()
                                                .addInteger( "columns", static_cast<int64>( pSnapshot->_columns ) )
                                                .addInteger( "rows", static_cast<int64>( pSnapshot->_rows ) )
                                                .addInteger( "coveredCount", static_cast<int64>( stats._coveredTileCount ) )
                                                .addInteger( "tileCount", static_cast<int64>( stats._tileCount ) )
                                                .addDecimal( "nearestDepth", static_cast<float64>( stats._nearestDepth ), Internal::kDepthDecimals ) )
                               .c_str() );

        // 타일 격자(HZB 16 px 밉의 최대 깊이) — 가까울수록 어둡고, 아무것도 없는 타일(깊이 1)은 바탕 그대로다. 그 뒤의 물체는 GPU 컬링이 거른다.
        const ImVec2  available = ImGui::GetContentRegionAvail();
        const float32 cell      = MathUtil::min( Internal::kTileCellMax, MathUtil::min( available.x / static_cast<float32>( pSnapshot->_columns ), available.y / static_cast<float32>( pSnapshot->_rows ) ) );
        if ( cell <= 0.0f )
            return;
        const ImVec2 origin    = ImGui::GetCursorScreenPos();
        ImDrawList*  pDrawList = ImGui::GetWindowDrawList();
        for ( uint32 row = 0; row < pSnapshot->_rows; ++row )
        {
            for ( uint32 column = 0; column < pSnapshot->_columns; ++column )
            {
                const float32 depth = pSnapshot->_listTileDepth[static_cast<size_t>( row ) * pSnapshot->_columns + column];
                if ( depth >= 1.0f )
                    continue;
                const float32 shade = MathUtil::saturate( depth );
                const ImVec2  cellMin{ origin.x + static_cast<float32>( column ) * cell, origin.y + static_cast<float32>( row ) * cell };
                pDrawList->AddRectFilled( cellMin, ImVec2{ cellMin.x + cell, cellMin.y + cell }, ImGui::GetColorU32( ImVec4{ shade, shade, shade, 1.0f } ) );
            }
        }
        ImGui::Dummy( ImVec2{ cell * static_cast<float32>( pSnapshot->_columns ), cell * static_cast<float32>( pSnapshot->_rows ) } );
        EditorSelfTestMarks::record( "renderDebugger.occlusionTiles" );
    }
} // namespace sw::editor
