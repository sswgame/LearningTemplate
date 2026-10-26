#include "pch.h"

#include "Editor/Panels/Animation/AnimationGraphPanel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Localization/LocText.h"

#include <algorithm>
#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 핀 번호 계약입니다. **만드는 것과 푸는 것이 한곳에 있습니다.**
         * @details 핀 번호는 `노드 id * kPinScale + 오프셋` 입니다. 푸는 쪽을 따로 적으면 자릿수 기준을 바꿀 때 한쪽만 따라가서
         *          **링크가 엉뚱한 노드에 붙습니다.** `DialogueGraphAsset` 도 같은 이유로 한곳에 둡니다(그 파일의 "핀 번호 계약" 절).
         */
        struct AnimationGraphPanelInternal
        {
            static constexpr Color kCurrentStateText{ 0.4f, 0.9f, 0.5f, 1.0f };
            /** @brief 조건이 있는 전이 · 조건 없는 전이 링크 색입니다. */
            static constexpr Color kConditionedLink{ 1.0f, 0.75f, 0.3f, 1.0f };
            static constexpr Color kPlainLink{ 1.0f, 1.0f, 1.0f, 1.0f };

            /** @brief 핀 번호의 자릿수 기준입니다. 한 노드가 가질 수 있는 핀 오프셋 개수이기도 합니다. */
            static constexpr int32 kPinScale = 10;
            /** @brief 입력 핀의 오프셋입니다. */
            static constexpr int32 kPinOffsetIn = 1;
            /** @brief 출력 핀의 오프셋입니다. */
            static constexpr int32 kPinOffsetOut = 2;

            static int32 pinIn( int32 nodeID )
            {
                return nodeID * kPinScale + kPinOffsetIn;
            }
            static int32 pinOut( int32 nodeID )
            {
                return nodeID * kPinScale + kPinOffsetOut;
            }
            /** @brief 핀 번호에서 노드 id 를 꺼냅니다. */
            static int32 pinNodeID( int32 pin )
            {
                return pin / kPinScale;
            }
            /** @brief 그 핀이 출력 핀인지 여부입니다. */
            static bool isOutputPin( int32 pin )
            {
                return ( pin % kPinScale ) == kPinOffsetOut;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "AnimationGraph" );
    SW_EDITOR_PANEL( AnimationGraphPanel, AnimationGraphPanel::kPanelID, EditorPanelCategory::Tool, 1100 );

    AnimationGraphPanel::AnimationGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetType::AnimationGraph, "Move Animation Graph Nodes", "animation-graph-layout" }
        , _previewGraph{}
        , _previewPlayer{}
        , _layerEditor{}
        , _previewStateName{}
        , _nameEditBuffer{}
        , _parameterEditBuffer{}
        , _listSelectionOrder{}
        , _selectedNodeID{ 0 }
        , _selectedLinkID{ 0 }
        , _canvasLinkID{ 0 }
        , _flowLinkID{ 0 }
    {
        _nodeGraph.setPinTypeColors( {
            Color{ 0.45f, 0.85f, 0.55f, 1.0f }
        } ); // 상태 전환 핀 하나
    }

    void AnimationGraphPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _nodeGraph.shutdown();
    }

    void AnimationGraphPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        updatePreview( ImGui::GetIO().DeltaTime );

        drawAnimationToolbar();

        // 캔버스 오른쪽에 상태 · 전이 편집(언리얼 AnimBP 상태 기계 · 유니티 Animator 의 전이 인스펙터).
        const float32             availWidth = ImGui::GetContentRegionAvail().x;
        const float32             sideWidth  = MathUtil::min( 300.0f * EditorThemeUtil::getDpiScale(), availWidth * 0.4f );
        editor::EditorSectionDesc canvasDesc{};
        canvasDesc._pID       = "AnimationGraphCanvasRegion";
        canvasDesc._kind      = editor::EditorSectionKind::Child;
        canvasDesc._childSize = float2{ availWidth - sideWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f };
        canvasDesc._flags     = editor::EditorSectionFlags::NoScrollbar | editor::EditorSectionFlags::NoScrollWithMouse;
        EditorChrome::beginSection( canvasDesc );
        drawAnimationCanvas();
        EditorChrome::endSection();

        ImGui::SameLine();
        editor::EditorSectionDesc sideDesc{};
        sideDesc._pID   = "AnimationGraphInspector";
        sideDesc._kind  = editor::EditorSectionKind::Child;
        sideDesc._flags = editor::EditorSectionFlags::Border;
        EditorChrome::beginSection( sideDesc );
        drawInspector();
        EditorChrome::endSection();
    }

    void AnimationGraphPanel::drawInspector()
    {
        // 유니티 Animator 의 Layers 탭처럼 상태 기계(캔버스)와 그 위에 적용하는 레이어를 탭으로 나눈다.
        if ( ImGui::BeginTabBar( "##AnimationGraphInspectorTabs" ) == false )
            return;
        const bool bGraphTab = ImGui::BeginTabItem( "Graph" );
        EditorSelfTestMarks::note( "animationGraph.tab.graph" );
        if ( bGraphTab )
        {
            drawGraphInspector();
            ImGui::EndTabItem();
        }
        const bool bLayersTab = ImGui::BeginTabItem( "Layers" );
        EditorSelfTestMarks::note( "animationGraph.tab.layers" );
        if ( bLayersTab )
        {
            const utf8* pEdit = _layerEditor.draw( _graph );
            if ( pEdit != nullptr )
                notifyDocumentEdited( pEdit );
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void AnimationGraphPanel::drawAnimationToolbar()
    {
        if ( EditorChrome::beginToolbar( "##AnimationGraphToolbar" ) )
        {
            if ( ImGui::Button( "Add Idle" ) )
            {
                addNamedNode( "Idle" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            EditorSelfTestMarks::note( "animationGraph.add.Idle" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddIdleTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Add Walk" ) )
            {
                addNamedNode( "Walk" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            EditorSelfTestMarks::note( "animationGraph.add.Walk" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddWalkTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Add Attack" ) )
            {
                addNamedNode( "Attack" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            EditorSelfTestMarks::note( "animationGraph.add.Attack" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AddAttackTip" ) );
            ImGui::SameLine();
            // 고른 노드 둘(고른 순서)을 잇는다 — 캔버스에서 노드를 누르고 Ctrl 로 둘째를 고른다.
            if ( ImGui::Button( "Link Selected" ) )
                linkSelectedNodes();
            EditorSelfTestMarks::note( "animationGraph.linkSelected" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LinkSelectedTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Load" ) )
                reloadDocument();
            EditorSelfTestMarks::note( "animationGraph.load" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LoadTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Save" ) )
                (void)saveGraphData(); // 실패는 저장 커맨드가 알린다
            EditorSelfTestMarks::note( "animationGraph.save" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "SaveTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Play" ) )
            {
                syncPreviewGraph();
                (void)_previewPlayer.play( hashed_string{}, false, 0.0f ); // 클립이 없어 재생은 비고 상태 이름만 옮긴다
                _bPreviewPlaying    = SW_TRUE;
                _previewHoldSeconds = 0.0f;
                EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), getLoadedAssetPath() );
                notePreviewTransition();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "PlayTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Advance" ) )
            {
                syncPreviewGraph();
                if ( _previewPlayer.advance() == false )
                    _bPreviewPlaying = SW_FALSE;
                else
                    EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), getLoadedAssetPath() );
                notePreviewTransition();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AdvanceTip" ) );
            ImGui::SameLine();
            if ( ImGui::Button( "Stop" ) )
            {
                _previewPlayer.stop();
                _bPreviewPlaying = SW_FALSE;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StopTip" ) );

            ImGui::SameLine();
            if ( _previewPlayer.getCurrentStateName().empty() == false )
                ImGui::TextDisabled( "Preview: %s  Nodes: %zu  Links: %zu", _previewPlayer.getCurrentStateName().c_str(),
                                     _graph._listNode.size(), _graph._listLink.size() );
            else
                ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _graph._listNode.size(), _graph._listLink.size(),
                                     EditorPaths::kAnimationGraphDocumentFileName );
        }
        EditorChrome::endToolbar();
    }

    void AnimationGraphPanel::drawAnimationCanvas()
    {
        if ( beginGraphCanvas( "AnimationGraphCanvas", EditorPaths::kAnimationGraphCanvasFileName ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorAnimationGraph", "ContextCreateFailed" ) );
            return;
        }

        drawStateNodes();
        drawTransitionLinks();
        drawNodeContextMenu();
        // 캔버스의 선택을 따라간다(고른 순서는 `_listSelectionOrder` — Link Selected 가 쓴다).
        trackCanvasSelection();

        updateUnreachableIssues();
        handleCanvasEdits();

        _nodeGraph.applyContentFitIfNeeded();
        cacheNodeLayout();
        endGraphCanvas();
    }

    void AnimationGraphPanel::drawStateNodes()
    {
        for ( GraphNode& node : _graph._listNode )
        {
            const ed::NodeId nodeID = toNodeID( node._id );
            ed::BeginNode( nodeID );
            if ( _previewPlayer.getCurrentStateName().c_str() == node._name && _previewPlayer.getCurrentStateName().empty() == false )
                ImGui::TextColored( EditorColorUtil::toImVec4( AnimationGraphPanelInternal::kCurrentStateText ), "%s", node._name.c_str() );
            else
                ImGui::TextUnformatted( node._name.c_str() );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                // 캔버스 안의 위젯 좌표는 캔버스 좌표다 — 화면 좌표로 바꿔 적는다(시나리오가 노드를 누르고 Ctrl 로 둘째를 고른다).
                const ImVec2 nameMin = ed::CanvasToScreen( ImGui::GetItemRectMin() );
                const ImVec2 nameMax = ed::CanvasToScreen( ImGui::GetItemRectMax() );
                EditorSelfTestMarks::noteRect( ( "animationGraph.node." + node._name ).c_str(), float2{ nameMin.x, nameMin.y }, float2{ nameMax.x, nameMax.y } );
            }
            bool bLinkedIn  = false;
            bool bLinkedOut = false;
            for ( const GraphLink& link : _graph._listLink )
            {
                bLinkedIn  = bLinkedIn || link._toNode == node._id;
                bLinkedOut = bLinkedOut || link._fromNode == node._id;
            }
            ed::BeginPin( toPinID( AnimationGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
            _nodeGraph.drawPinIcon( 0, bLinkedIn );
            ImGui::SameLine();
            ImGui::TextUnformatted( "In" );
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin( toPinID( AnimationGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
            ImGui::TextUnformatted( "Out" );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( 0, bLinkedOut );
            ed::EndPin();
            ed::EndNode();

            if ( _nodeGraph.needsContentFit() )
                ed::SetNodePosition( nodeID, ImVec2( node._position._x, node._position._y ) );
            applyNodePlacement( node._id );
        }
    }

    void AnimationGraphPanel::drawTransitionLinks()
    {
        for ( const GraphLink& link : _graph._listLink )
        {
            // 조건이 있는 전이는 굵게 · 다른 색으로(조건 없는 "끝나면 다음" 과 구별).
            const bool   bConditioned = link._op != AnimationConditionOp::None;
            const ImVec4 color        = EditorColorUtil::toImVec4( bConditioned ? AnimationGraphPanelInternal::kConditionedLink : AnimationGraphPanelInternal::kPlainLink );
            ed::Link( toLinkID( link._id ), toPinID( AnimationGraphPanelInternal::pinOut( link._fromNode ) ), toPinID( AnimationGraphPanelInternal::pinIn( link._toNode ) ), color,
                      bConditioned ? 2.5f : 1.5f );
        }
        // 미리보기가 전이를 탔으면 그 링크에 흐름을 보인다(플레이 중 현재 전이 강조).
        if ( _flowLinkID != 0 )
        {
            ed::Flow( toLinkID( _flowLinkID ) );
            _flowLinkID = 0;
        }
    }

    void AnimationGraphPanel::drawNodeContextMenu()
    {
        // 노드 오른쪽 클릭 메뉴(지우기 · 복제).
        ed::NodeId contextNode{};
        ed::Suspend();
        if ( ed::ShowNodeContextMenu( &contextNode ) )
        {
            _selectedNodeID = static_cast<int32>( contextNode.Get() );
            EditorInputGUI::openPopup( "AnimationNodeContext" );
        }
        if ( ImGui::BeginPopup( "AnimationNodeContext" ) )
        {
            if ( ImGui::MenuItem( "Duplicate" ) )
                duplicateNode( _selectedNodeID );
            if ( ImGui::MenuItem( "Delete" ) )
                ed::DeleteNode( toNodeID( _selectedNodeID ) );
            ImGui::EndPopup();
        }
        ed::Resume();
    }

    void AnimationGraphPanel::updateUnreachableIssues()
    {
        // 상태 그래프의 첫 노드가 진입이다 — 나머지는 들어오는 전환이 없으면 닿지 않는다.
        vector<int32> listInputNode;
        for ( size_t index = 1; index < _graph._listNode.size(); ++index )
        {
            listInputNode.push_back( _graph._listNode[index]._id );
        }
        vector<EditorGraphEdge> listEdge;
        for ( const GraphLink& link : _graph._listLink )
        {
            listEdge.push_back( EditorGraphEdge{ link._fromNode, link._toNode } );
        }
        vector<EditorGraphNodeIssue> listIssue;
        EditorNodeGraphRules::collectUnreachableNodes( listInputNode, listEdge, listIssue );
        _nodeGraph.setNodeIssues( std::move( listIssue ) );
        _nodeGraph.drawNodeIssues();
    }

    void AnimationGraphPanel::handleCanvasEdits()
    {
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &AnimationGraphPanel::findGraphPin, this ), fromPin, toPin ) )
        {
            GraphLink link{};
            link._id       = nextLinkID();
            link._fromNode = AnimationGraphPanelInternal::pinNodeID( fromPin );
            link._toNode   = AnimationGraphPanelInternal::pinNodeID( toPin );
            _graph._listLink.push_back( link );
            notifyDocumentEdited( "Link Animation Graph Nodes" );
        }

        const vector<EditorGraphNodeKind> listKind{
            EditorGraphNodeKind{  "Idle", "State", 0},
            EditorGraphNodeKind{  "Walk", "State", 1},
            EditorGraphNodeKind{"Attack", "State", 2}
        };
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) && kindID < listKind.size() )
        {
            placeNodeOnNextDraw( addNamedNode( listKind[kindID]._pName ), canvasPosition );
            notifyDocumentEdited( "Add Animation Graph Node" );
        }

        processCanvasDeletions( []( const auto& link, int32 nodeID )
        { return link._fromNode == nodeID || link._toNode == nodeID; },
                                []( int32 ) {}, "Delete Animation Graph Link", "Delete Animation Graph Node" );
    }
    void AnimationGraphPanel::ensureDefaults()
    {
        if ( _graph._listNode.empty() == false )
            return;
        _graph._listNode.push_back( GraphNode{
            "Idle", 1, float2{ 40.0f, 40.0f }
        } );
        _graph._listNode.push_back( GraphNode{
            "Walk", 2, float2{ 280.0f, 80.0f }
        } );
        GraphLink link{};
        link._id       = 100;
        link._fromNode = 1;
        link._toNode   = 2;
        _graph._listLink.push_back( link );
    }

    ToolAssetLoadResult AnimationGraphPanel::loadDocument()
    {
        AnimationGraphAsset       data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadAnimationGraph( data, getLoadedAssetPath() );
        adoptLoadedGraph( std::move( data ), result );
        _previewPlayer.stop();
        _layerEditor.clearSelection();
        return result;
    }

    bool AnimationGraphPanel::saveGraphData()
    {
        AnimationGraphAsset data = captureGraphData();
        if ( _nodeGraph.bind() )
        {
            for ( GraphNode& node : data._listNode )
            {
                const ImVec2 pos  = ed::GetNodePosition( toNodeID( node._id ) );
                node._position._x = pos.x;
                node._position._y = pos.y;
            }
            _nodeGraph.unbind();
            _graph._listNode = data._listNode;
        }
        // **저장이 실패하면 아무것도 지우지 않는다.** 실패를 "저장됨" 으로 표시하면 문서를 바꾸거나 에디터를 닫을 때
        // 종료 확인이 뜨지 않고 편집이 조용히 사라진다.
        if ( EditorToolAssetCommands::saveAnimationGraph( data, getLoadedAssetPath() ) == false )
            return false;

        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool AnimationGraphPanel::saveDocument()
    {
        return saveGraphData();
    }

    void AnimationGraphPanel::syncPreviewGraph()
    {
        _previewGraph = captureGraphData();
        _previewPlayer.setGraph( &_previewGraph );
    }

    void AnimationGraphPanel::notePreviewTransition()
    {
        const string current{ _previewPlayer.getCurrentStateName().c_str() };
        if ( current != _previewStateName && _previewStateName.empty() == false )
        {
            const AnimationGraphNode* pFrom = _previewGraph.findNodeByName( _previewStateName );
            const AnimationGraphNode* pTo   = _previewGraph.findNodeByName( current );
            for ( const GraphLink& link : _graph._listLink )
            {
                const bool bLinkBetweenNodes = pFrom != nullptr && pTo != nullptr && link._fromNode == pFrom->_id && link._toNode == pTo->_id;
                if ( bLinkBetweenNodes )
                    _flowLinkID = link._id;
            }
        }
        _previewStateName = current;
    }

    void AnimationGraphPanel::updatePreview( float32 deltaSeconds )
    {
        if ( _bPreviewPlaying == SW_FALSE )
            return;
        _previewHoldSeconds += deltaSeconds;
        if ( _previewHoldSeconds < 0.75f )
            return;
        _previewHoldSeconds = 0.0f;
        if ( _previewPlayer.advance() == false )
            _bPreviewPlaying = SW_FALSE;
        else
            EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), getLoadedAssetPath() );
        notePreviewTransition();
    }

    int32 AnimationGraphPanel::addNamedNode( const utf8* pName )
    {
        GraphNode n{};
        n._id              = nextNodeID();
        n._name            = ( pName != nullptr ) ? pName : "Node";
        n._position._x     = 40.0f + static_cast<float32>( _graph._listNode.size() ) * 40.0f;
        n._position._y     = 40.0f + static_cast<float32>( _graph._listNode.size() ) * 30.0f;
        const int32 nodeID = n._id;
        _graph._listNode.push_back( std::move( n ) );
        return nodeID;
    }

    bool AnimationGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const int32 nodeID = AnimationGraphPanelInternal::pinNodeID( pinID );
        bool        bKnown = false;
        for ( const GraphNode& node : _graph._listNode )
        {
            bKnown = bKnown || node._id == nodeID;
        }
        if ( bKnown == false )
            return false;
        outInfo._type   = 0;
        outInfo._bInput = AnimationGraphPanelInternal::isOutputPin( pinID ) == false;
        return true;
    }

    void AnimationGraphPanel::trackCanvasSelection()
    {
        constexpr int32 kMaxSelection = 16;
        ed::NodeId      arrNode[kMaxSelection];
        const int32     nodeCount = ed::GetSelectedNodes( arrNode, kMaxSelection );
        // 고른 순서를 지킨다 — 새로 고른 것을 뒤에 붙이고, 풀린 것을 뺀다.
        vector<int32> listNow;
        for ( int32 index = 0; index < nodeCount; ++index )
        {
            listNow.push_back( static_cast<int32>( arrNode[index].Get() ) );
        }
        vector<int32> listOrdered;
        for ( const int32 nodeID : _listSelectionOrder )
        {
            if ( VectorUtil::contains( listNow, nodeID ) )
                listOrdered.push_back( nodeID );
        }
        for ( const int32 nodeID : listNow )
        {
            if ( VectorUtil::contains( listOrdered, nodeID ) == false )
                listOrdered.push_back( nodeID );
        }
        // 편집 대상은 캔버스 선택이 **바뀐 프레임에만** 따라간다 — 인스펙터의 전이 목록으로 고른 것을 매 프레임 덮지 않는다.
        const bool bNodesChanged = listOrdered != _listSelectionOrder;
        _listSelectionOrder.swap( listOrdered );
        if ( bNodesChanged && _listSelectionOrder.empty() == false )
        {
            _selectedNodeID = _listSelectionOrder.back();
            _selectedLinkID = 0;
        }
        ed::LinkId  arrLink[1];
        const int32 canvasLinkID = ed::GetSelectedLinks( arrLink, 1 ) > 0 ? static_cast<int32>( arrLink[0].Get() ) : 0;
        if ( canvasLinkID != _canvasLinkID )
        {
            _canvasLinkID = canvasLinkID;
            if ( canvasLinkID != 0 )
            {
                _selectedLinkID = canvasLinkID;
                _selectedNodeID = 0;
            }
        }
    }

    void AnimationGraphPanel::linkSelectedNodes()
    {
        if ( _listSelectionOrder.size() < 2 )
        {
            SW_LOG_WARNING( "Link Selected needs two selected nodes (click one, Ctrl+click the other)" );
            return;
        }
        GraphLink link{};
        link._id       = nextLinkID();
        link._fromNode = _listSelectionOrder[_listSelectionOrder.size() - 2];
        link._toNode   = _listSelectionOrder.back();
        _graph._listLink.push_back( link );
        _selectedLinkID = link._id;
        notifyDocumentEdited( "Link Animation Graph Nodes" );
    }

    void AnimationGraphPanel::duplicateNode( int32 nodeID )
    {
        for ( const GraphNode& node : _graph._listNode )
        {
            if ( node._id != nodeID )
                continue;
            GraphNode copy = node;
            copy._id       = nextNodeID();
            copy._position = float2{ node._position._x + 40.0f, node._position._y + 40.0f };
            placeNodeOnNextDraw( copy._id, copy._position );
            _graph._listNode.push_back( std::move( copy ) );
            notifyDocumentEdited( "Duplicate Animation Graph Node" );
            return;
        }
    }

    uint32 AnimationGraphPanel::countConditionLinks() const
    {
        uint32 count{ 0 };
        for ( const GraphLink& link : _graph._listLink )
        {
            count += link._op != AnimationConditionOp::None ? 1u : 0u;
        }
        return count;
    }

    void AnimationGraphPanel::drawGraphInspector()
    {
        drawStateInspector();
        drawTransitionList();
        drawTransitionCondition();
    }

    void AnimationGraphPanel::drawStateInspector()
    {
        // 상태(노드): 이름은 클립을 묶는 열쇠다 — 이름이 곧 클립 지정이다(AnimationGraphNode::_name).
        GraphNode* pNode = nullptr;
        for ( GraphNode& node : _graph._listNode )
        {
            if ( node._id == _selectedNodeID )
                pNode = &node;
        }
        if ( pNode == nullptr )
            return;

        ImGui::SeparatorText( "State" );
        if ( ImGui::IsAnyItemActive() == false )
            _nameEditBuffer = pNode->_name;
        ImGui::TextUnformatted( "Clip" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        EditorWidgets::drawTextField( "##stateName", _nameEditBuffer );
        EditorSelfTestMarks::note( "animationGraph.state.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StateClipTip" ) );
        const bool bStateNameCommitted = ImGui::IsItemDeactivatedAfterEdit() && _nameEditBuffer.empty() == false && _nameEditBuffer != pNode->_name;
        if ( bStateNameCommitted )
        {
            pNode->_name = _nameEditBuffer;
            notifyDocumentEdited( "Rename Animation State" );
        }
        const utf8* const arrLoop[] = { "Default", "Loop", "Once" };
        int32             loopIndex = pNode->_loopOverride < 0 ? 0 : ( pNode->_loopOverride > 0 ? 1 : 2 );
        ImGui::TextUnformatted( "Loop" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        if ( ImGui::Combo( "##stateLoop", &loopIndex, arrLoop, 3 ) )
        {
            pNode->_loopOverride = loopIndex == 0 ? int8{ -1 } : ( loopIndex == 1 ? int8{ 1 } : int8{ 0 } );
            notifyDocumentEdited( "Edit Animation State" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StateLoopTip" ) );
    }

    void AnimationGraphPanel::drawTransitionList()
    {
        // 전이 목록 — 누르면 그 전이를 고른다(캔버스에서 링크를 눌러도 된다).
        ImGui::SeparatorText( "Transitions" );
        for ( size_t index = 0; index < _graph._listLink.size(); ++index )
        {
            const GraphLink&                      link   = _graph._listLink[index];
            const auto                            itFrom = std::find_if( _graph._listNode.begin(), _graph._listNode.end(), [&link]( const GraphNode& node )
                                       { return node._id == link._fromNode; } );
            const auto                            itTo   = std::find_if( _graph._listNode.begin(), _graph._listNode.end(), [&link]( const GraphNode& node )
                                         { return node._id == link._toNode; } );
            fixed_string<constant::kMaxBuffer128> label;
            formatstring( label.data(), label.capacity(), "%s -> %s%s", itFrom != _graph._listNode.end() ? itFrom->_name.c_str() : "?", itTo != _graph._listNode.end() ? itTo->_name.c_str() : "?",
                          link._op != AnimationConditionOp::None ? "  [if]" : "" );
            ImGui::PushID( static_cast<int32>( link._id ) );
            if ( ImGui::Selectable( label.c_str(), link._id == _selectedLinkID ) )
            {
                _selectedLinkID = link._id;
                _selectedNodeID = 0;
            }
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( "animationGraph.link." + to_string( static_cast<uint64>( index ) ) ).c_str() );
            ImGui::PopID();
        }
    }

    void AnimationGraphPanel::drawTransitionCondition()
    {
        GraphLink* pLink = nullptr;
        for ( GraphLink& link : _graph._listLink )
        {
            if ( link._id == _selectedLinkID )
                pLink = &link;
        }
        if ( pLink == nullptr )
            return;
        ImGui::SeparatorText( "Condition" );
        bool bEdited = false;
        if ( ImGui::IsAnyItemActive() == false )
            _parameterEditBuffer = pLink->_parameter.empty() ? "" : pLink->_parameter.c_str(); // 빈 hashed_string 의 c_str 는 "None" 이다
        ImGui::TextUnformatted( "Param" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        EditorWidgets::drawTextField( "##conditionParam", _parameterEditBuffer );
        EditorSelfTestMarks::note( "animationGraph.condition.param" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ConditionParamTip" ) );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            pLink->_parameter = hashed_string( _parameterEditBuffer.c_str() );
            bEdited           = true;
        }
        // 비교 — 조건 없음(끝나면 다음) · 비교 여섯 · 트리거
        int32 opIndex = static_cast<int32>( pLink->_op );
        ImGui::TextUnformatted( "Op" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        const bool bOpOpen = ImGui::BeginCombo( "##conditionOp", pLink->_op == AnimationConditionOp::None ? "(on finish)" : AnimationGraphAsset::getConditionOpText( pLink->_op ) );
        EditorSelfTestMarks::note( "animationGraph.condition.op" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ConditionOpTip" ) );
        if ( bOpOpen )
        {
            for ( int32 op = 0; op <= static_cast<int32>( AnimationConditionOp::Trigger ); ++op )
            {
                const AnimationConditionOp value = static_cast<AnimationConditionOp>( op );
                if ( ImGui::Selectable( value == AnimationConditionOp::None ? "(on finish)" : AnimationGraphAsset::getConditionOpText( value ), op == opIndex ) )
                {
                    pLink->_op = value;
                    bEdited    = true;
                }
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( ( "animationGraph.condition.op." + to_string( static_cast<uint64>( op ) ) ).c_str() );
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted( "Value" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::DragFloat( "##conditionValue", &pLink->_threshold, 0.05f );
        EditorSelfTestMarks::note( "animationGraph.condition.value" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ConditionValueTip" ) );
        bEdited = ImGui::IsItemDeactivatedAfterEdit() || bEdited;
        ImGui::TextUnformatted( "Blend" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::DragFloat( "##transitionBlend", &pLink->_blendSeconds, 0.01f, -1.0f, 10.0f, pLink->_blendSeconds < 0.0f ? "(player default)" : "%.2f s" );
        EditorSelfTestMarks::note( "animationGraph.condition.blend" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "TransitionBlendTip" ) );
        bEdited = ImGui::IsItemDeactivatedAfterEdit() || bEdited;
        if ( bEdited )
            notifyDocumentEdited( "Edit Animation Transition" );
    }
} // namespace sw::editor
