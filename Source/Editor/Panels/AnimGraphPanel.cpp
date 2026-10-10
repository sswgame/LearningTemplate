#include "pch.h"

#include "Editor/Panels/AnimGraphPanel.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Graph/AnimGraphAsset.h"

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
        struct AnimGraphPanelInternal
        {
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
    SW_LOG_CALLER( "AnimGraph" );
    SW_EDITOR_PANEL( AnimGraphPanel, "animation_graph", EditorPanelCategory::Tool, 1100 );

    AnimGraphPanel::AnimGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetType::AnimGraph, "Move Animation Graph Nodes", "anim-graph-layout" }
        , _previewGraph{}
        , _previewPlayer{}
    {
        _nodeGraph.setPinTypeColors( {
            Color4{ 0.45f, 0.85f, 0.55f, 1.0f }
        } ); // 상태 전환 핀 하나
    }

    void AnimGraphPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _nodeGraph.shutdown();
    }

    void AnimGraphPanel::drawContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        tickPreview( ImGui::GetIO().DeltaTime );

        drawAnimationToolbar();

        drawAnimationCanvas();
    }

    void AnimGraphPanel::drawAnimationToolbar()
    {
        if ( EditorChrome::beginToolbar( "##AnimGraphToolbar" ) )
        {
            if ( ImGui::Button( "Add Idle" ) )
            {
                addNamedNode( "Idle" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Add Walk" ) )
            {
                addNamedNode( "Walk" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Add Attack" ) )
            {
                addNamedNode( "Attack" );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Link Selected" ) && _listNode.size() >= 2 )
            {
                GraphLink link{};
                link._id       = nextLinkID();
                link._fromNode = _listNode[_listNode.size() - 2]._id;
                link._toNode   = _listNode[_listNode.size() - 1]._id;
                _listLink.push_back( link );
                notifyDocumentEdited( "Link Animation Graph Nodes" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Load" ) )
                reloadDocument();
            ImGui::SameLine();
            if ( ImGui::Button( "Save" ) )
                (void)saveGraphData(); // 실패는 저장 커맨드가 알린다
            ImGui::SameLine();
            if ( ImGui::Button( "Play" ) )
            {
                syncPreviewGraph();
                (void)_previewPlayer.play( hashed_string{}, false, 0.0f ); // 클립이 없어 재생은 비고 상태 이름만 옮긴다
                _bPreviewPlaying    = SW_TRUE;
                _previewHoldSeconds = 0.0f;
                EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), getLoadedAssetPath() );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Advance" ) )
            {
                syncPreviewGraph();
                if ( _previewPlayer.advance() == false )
                    _bPreviewPlaying = SW_FALSE;
                else
                    EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), getLoadedAssetPath() );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Stop" ) )
            {
                _previewPlayer.stop();
                _bPreviewPlaying = SW_FALSE;
            }

            ImGui::SameLine();
            if ( _previewPlayer.getCurrentStateName().empty() == false )
                ImGui::TextDisabled( "Preview: %s  Nodes: %zu  Links: %zu", _previewPlayer.getCurrentStateName().c_str(),
                                     _listNode.size(), _listLink.size() );
            else
                ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _listNode.size(), _listLink.size(),
                                     EditorUtil::kAnimGraphDocumentFileName );
        }
        EditorChrome::endToolbar();
    }

    void AnimGraphPanel::drawAnimationCanvas()
    {
        if ( beginGraphCanvas( "AnimGraphCanvas", EditorUtil::kAnimGraphCanvasFileName ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
            return;
        }

        for ( GraphNode& node : _listNode )
        {
            const ed::NodeId nodeID = toNodeID( node._id );
            ed::BeginNode( nodeID );
            if ( _previewPlayer.getCurrentStateName().c_str() == node._name && _previewPlayer.getCurrentStateName().empty() == false )
                ImGui::TextColored( ImVec4( 0.4f, 0.9f, 0.5f, 1.0f ), "%s", node._name.c_str() );
            else
                ImGui::TextUnformatted( node._name.c_str() );
            bool bLinkedIn  = false;
            bool bLinkedOut = false;
            for ( const GraphLink& link : _listLink )
            {
                bLinkedIn  = bLinkedIn || link._toNode == node._id;
                bLinkedOut = bLinkedOut || link._fromNode == node._id;
            }
            ed::BeginPin( toPinID( AnimGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
            _nodeGraph.drawPinIcon( 0, bLinkedIn );
            ImGui::SameLine();
            ImGui::TextUnformatted( "In" );
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin( toPinID( AnimGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
            ImGui::TextUnformatted( "Out" );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( 0, bLinkedOut );
            ed::EndPin();
            ed::EndNode();

            if ( _nodeGraph.needsContentFit() )
                ed::SetNodePosition( nodeID, ImVec2( node._position._x, node._position._y ) );
            applyNodePlacement( node._id );
        }

        for ( const GraphLink& link : _listLink )
        {
            ed::Link( toLinkID( link._id ), toPinID( AnimGraphPanelInternal::pinOut( link._fromNode ) ), toPinID( AnimGraphPanelInternal::pinIn( link._toNode ) ) );
        }

        // 상태 그래프의 첫 노드가 진입이다 — 나머지는 들어오는 전환이 없으면 닿지 않는다.
        vector<int32> listInputNode;
        for ( size_t index = 1; index < _listNode.size(); ++index )
        {
            listInputNode.push_back( _listNode[index]._id );
        }
        vector<EditorGraphEdge> listEdge;
        for ( const GraphLink& link : _listLink )
        {
            listEdge.push_back( EditorGraphEdge{ link._fromNode, link._toNode } );
        }
        vector<EditorGraphNodeIssue> listIssue;
        EditorNodeGraphRules::collectUnreachableNodes( listInputNode, listEdge, listIssue );
        _nodeGraph.setNodeIssues( std::move( listIssue ) );
        _nodeGraph.drawNodeIssues();

        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &AnimGraphPanel::findGraphPin, this ), fromPin, toPin ) )
        {
            GraphLink link{};
            link._id       = nextLinkID();
            link._fromNode = AnimGraphPanelInternal::pinNodeID( fromPin );
            link._toNode   = AnimGraphPanelInternal::pinNodeID( toPin );
            _listLink.push_back( link );
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

        _nodeGraph.applyContentFitIfNeeded();
        cacheNodeLayout();
        endGraphCanvas();
    }

    void AnimGraphPanel::ensureDefaults()
    {
        if ( _listNode.empty() == false )
            return;
        _listNode.push_back( GraphNode{
            "Idle", 1, float2{ 40.0f, 40.0f }
        } );
        _listNode.push_back( GraphNode{
            "Walk", 2, float2{ 280.0f, 80.0f }
        } );
        GraphLink link{};
        link._id       = 100;
        link._fromNode = 1;
        link._toNode   = 2;
        _listLink.push_back( link );
    }

    ToolAssetLoadResult AnimGraphPanel::loadDocument()
    {
        AnimGraphAsset            data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadAnimGraph( data, getLoadedAssetPath() );
        adoptLoadedGraph( std::move( data ), result );
        _previewPlayer.stop();
        return result;
    }

    bool AnimGraphPanel::saveGraphData()
    {
        AnimGraphAsset data = captureGraphData();
        if ( _nodeGraph.bind() )
        {
            for ( GraphNode& node : data._listNode )
            {
                const ImVec2 pos  = ed::GetNodePosition( toNodeID( node._id ) );
                node._position._x = pos.x;
                node._position._y = pos.y;
            }
            _nodeGraph.unbind();
            _listNode = data._listNode;
        }
        // **저장이 실패하면 아무것도 지우지 않는다.** 실패를 "저장됨" 으로 표시하면 문서를 바꾸거나 에디터를 닫을 때
        // 종료 확인이 뜨지 않고 편집이 조용히 사라진다.
        if ( EditorToolAssetCommands::saveAnimGraph( data, getLoadedAssetPath() ) == false )
            return false;

        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool AnimGraphPanel::saveDocument()
    {
        return saveGraphData();
    }

    void AnimGraphPanel::syncPreviewGraph()
    {
        _previewGraph = captureGraphData();
        _previewPlayer.setGraph( &_previewGraph );
    }

    void AnimGraphPanel::tickPreview( float32 deltaSeconds )
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
    }

    int32 AnimGraphPanel::addNamedNode( const utf8* pName )
    {
        GraphNode n{};
        n._id              = nextNodeID();
        n._name            = ( pName != nullptr ) ? pName : "Node";
        n._position._x     = 40.0f + static_cast<float32>( _listNode.size() ) * 40.0f;
        n._position._y     = 40.0f + static_cast<float32>( _listNode.size() ) * 30.0f;
        const int32 nodeID = n._id;
        _listNode.push_back( std::move( n ) );
        return nodeID;
    }

    bool AnimGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const int32 nodeID = AnimGraphPanelInternal::pinNodeID( pinID );
        bool        bKnown = false;
        for ( const GraphNode& node : _listNode )
        {
            bKnown = bKnown || node._id == nodeID;
        }
        if ( bKnown == false )
            return false;
        outInfo._type   = 0;
        outInfo._bInput = AnimGraphPanelInternal::isOutputPin( pinID ) == false;
        return true;
    }
} // namespace sw::editor
