#include "pch.h"

#include "Editor/Panels/AnimationGraphPanel.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorNodeGraphId.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimationGraphAsset.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 핀 번호 계약 — **짓는 것과 푸는 것이 한 자리에 있다.**
         * @details 핀 번호는 `노드 id * kPinScale + 오프셋` 이다. 예전에는 짓는 쪽만 여기 있고
         *          푸는 쪽은 링크를 만드는 코드에 `/ 10` · `% 10` 으로 적혀 있었다 — 자릿수 기준을
         *          바꾸면 한쪽만 따라가서 **링크가 엉뚱한 노드에 붙는다.** `DialogueGraphAsset` 이
         *          같은 이유로 이미 한 자리에 모았다(그 파일의 "핀 번호 계약" 절).
         */
        struct AnimationGraphPanelInternal
        {
            /** @brief 핀 번호의 자릿수 기준 — 한 노드가 가질 수 있는 핀 오프셋 개수이기도 하다. */
            static constexpr int32 kPinScale = 10;
            /** @brief 입력 핀의 오프셋. */
            static constexpr int32 kPinOffsetIn = 1;
            /** @brief 출력 핀의 오프셋. */
            static constexpr int32 kPinOffsetOut = 2;

            static int32 pinIn( int32 nodeId )
            {
                return nodeId * kPinScale + kPinOffsetIn;
            }
            static int32 pinOut( int32 nodeId )
            {
                return nodeId * kPinScale + kPinOffsetOut;
            }
            /** @brief 핀 번호에서 노드 id 를 꺼냅니다. */
            static int32 pinNodeId( int32 pin )
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

    AnimationGraphPanel::AnimationGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetKind::AnimationGraph, "Move Animation Graph Nodes", "anim-graph-layout" }
        , _previewPlayer{}
        , _listPreviewClip{}
    {
    }

    void AnimationGraphPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
    {
        _nodeGraph.shutdown();
    }

    void AnimationGraphPanel::drawContent()
    {
        updateFocusedDocument();
        if ( isDocumentLoaded() == false )
            loadGraphData();

        tickPreview( ImGui::GetIO().DeltaTime );

        drawAnimationToolbar();

        drawAnimationCanvas();
    }

    void AnimationGraphPanel::drawAnimationToolbar()
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
                GraphLink l{};
                l._id       = nextLinkId();
                l._fromNode = _listNode[_listNode.size() - 2]._id;
                l._toNode   = _listNode[_listNode.size() - 1]._id;
                _listLink.push_back( l );
                notifyDocumentEdited( "Link Animation Graph Nodes" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Load" ) )
                loadGraphData();
            ImGui::SameLine();
            if ( ImGui::Button( "Save" ) )
                saveGraphData();
            ImGui::SameLine();
            if ( ImGui::Button( "Play" ) )
            {
                syncPreviewGraph();
                _previewPlayer.play();
                _bPreviewPlaying    = SW_TRUE;
                _previewHoldSeconds = 0.0f;
                EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Advance" ) )
            {
                syncPreviewGraph();
                if ( _previewPlayer.advance() == false )
                    _bPreviewPlaying = SW_FALSE;
                else
                    EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Stop" ) )
            {
                _previewPlayer.stop();
                _bPreviewPlaying = SW_FALSE;
            }

            ImGui::SameLine();
            if ( _previewPlayer.getCurrentNodeName().empty() == false )
                ImGui::TextDisabled( "Preview: %s  Nodes: %zu  Links: %zu", _previewPlayer.getCurrentNodeName().c_str(),
                                     _listNode.size(), _listLink.size() );
            else
                ImGui::TextDisabled( "Nodes: %zu  Links: %zu  (%s)", _listNode.size(), _listLink.size(),
                                     getEditorData()._animationGraphDataFile.c_str() );
        }
        EditorChrome::endToolbar();
    }

    void AnimationGraphPanel::drawAnimationCanvas()
    {
        if ( _nodeGraph.beginCanvas( "AnimationGraphCanvas",
                                     getEditorData()._animationGraphSettingsFile.c_str() ) == false )
        {
            ImGui::TextUnformatted( "Failed to create Animation Graph editor context." );
            return;
        }

        for ( GraphNode& node : _listNode )
        {
            const ed::NodeId nodeId = toNodeId( node._id );
            ed::BeginNode( nodeId );
            if ( _previewPlayer.getCurrentNodeName() == node._name && _previewPlayer.getCurrentNodeName().empty() == false )
                ImGui::TextColored( ImVec4( 0.4f, 0.9f, 0.5f, 1.0f ), "%s", node._name.c_str() );
            else
                ImGui::TextUnformatted( node._name.c_str() );
            ed::BeginPin( toPinId( AnimationGraphPanelInternal::pinIn( node._id ) ), ed::PinKind::Input );
            ImGui::TextUnformatted( "-> In" );
            ed::EndPin();
            ImGui::SameLine();
            ed::BeginPin( toPinId( AnimationGraphPanelInternal::pinOut( node._id ) ), ed::PinKind::Output );
            ImGui::TextUnformatted( "Out ->" );
            ed::EndPin();
            ed::EndNode();

            if ( _nodeGraph.needsContentFit() )
                ed::SetNodePosition( nodeId, ImVec2( node._position._x, node._position._y ) );
        }

        for ( const GraphLink& link : _listLink )
        {
            ed::Link( toLinkId( link._id ), toPinId( AnimationGraphPanelInternal::pinOut( link._fromNode ) ), toPinId( AnimationGraphPanelInternal::pinIn( link._toNode ) ) );
        }

        if ( ed::BeginCreate() )
        {
            ed::PinId a;
            ed::PinId b;
            if ( ed::QueryNewLink( &a, &b ) )
            {
                if ( a.Get() != 0 && b.Get() != 0 && ed::AcceptNewItem() )
                {
                    GraphLink link{};
                    link._id          = nextLinkId();
                    const int32 ap    = static_cast<int32>( a.Get() );
                    const int32 bp    = static_cast<int32>( b.Get() );
                    const int32 aNode = AnimationGraphPanelInternal::pinNodeId( ap );
                    const int32 bNode = AnimationGraphPanelInternal::pinNodeId( bp );
                    if ( AnimationGraphPanelInternal::isOutputPin( ap ) )
                    {
                        link._fromNode = aNode;
                        link._toNode   = bNode;
                    }
                    else
                    {
                        link._fromNode = bNode;
                        link._toNode   = aNode;
                    }
                    _listLink.push_back( link );
                    notifyDocumentEdited( "Link Animation Graph Nodes" );
                }
            }
        }
        ed::EndCreate();

        if ( ed::BeginDelete() )
        {
            ed::LinkId linkId;
            while ( ed::QueryDeletedLink( &linkId ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const int32 id = static_cast<int32>( linkId.Get() );
                    _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
                                                     [id]( const GraphLink& link )
                    { return link._id == id; } ),
                                     _listLink.end() );
                    notifyDocumentEdited( "Delete Animation Graph Link" );
                }
            }
            ed::NodeId nodeId;
            while ( ed::QueryDeletedNode( &nodeId ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const int32 id = static_cast<int32>( nodeId.Get() );
                    _listNode.erase( std::remove_if( _listNode.begin(), _listNode.end(),
                                                     [id]( const GraphNode& node )
                    { return node._id == id; } ),
                                     _listNode.end() );
                    _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
                                                     [id]( const GraphLink& link )
                    { return link._fromNode == id || link._toNode == id; } ),
                                     _listLink.end() );
                    notifyDocumentEdited( "Delete Animation Graph Node" );
                }
            }
            ed::EndDelete();
        }

        _nodeGraph.applyContentFitIfNeeded();
        cacheNodeLayout();
        _nodeGraph.endCanvas();
    }

    void AnimationGraphPanel::ensureDefaults()
    {
        if ( _listNode.empty() == false )
            return;
        _listNode.push_back( GraphNode{
            "Idle", 1, float2{ 40.0f, 40.0f }
        } );
        _listNode.push_back( GraphNode{
            "Walk", 2, float2{ 280.0f, 80.0f }
        } );
        _listLink.push_back( GraphLink{ 100, 1, 2 } );
    }

    void AnimationGraphPanel::loadGraphData()
    {
        AnimationGraphAsset data;
        if ( EditorToolAssetCommands::loadAnimationGraph( data, getLoadedAssetPath() ) )
        {
            _listNode = std::move( data._listNode );
            _listLink = std::move( data._listLink );
        }
        if ( _listNode.empty() )
            ensureDefaults();
        _bGraphLayoutReady = SW_FALSE;
        _previewPlayer.stop();
        _bPreviewPlaying = SW_FALSE;
        markDocumentLoaded();
        _nodeGraph.requestContentFit();
    }

    bool AnimationGraphPanel::saveGraphData()
    {
        AnimationGraphAsset data = captureGraphData();
        if ( _nodeGraph.bind() )
        {
            for ( GraphNode& node : data._listNode )
            {
                const ImVec2 pos  = ed::GetNodePosition( toNodeId( node._id ) );
                node._position._x = pos.x;
                node._position._y = pos.y;
            }
            _nodeGraph.unbind();
            _listNode = data._listNode;
        }
        // **저장이 실패하면 아무것도 지우지 않는다.** 예전에는 반환값을 버리고 무조건
        // `clearDocumentDirty()` 를 불렀다 — 실패해도 "저장됨" 으로 표시되고, 되돌리기 기준점까지
        // 옮겨지고, `saveDocument()` 는 `true` 를 돌려줬다. 그래서 문서를 바꾸거나 에디터를 닫을 때
        // 종료 확인이 뜨지 않고 편집이 조용히 사라졌다.
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
        AnimationGraphAsset asset = captureGraphData();
        _previewPlayer.setGraph( asset );
        _previewPlayer.clearClips();
        _listPreviewClip.clear();
        _listPreviewClip.reserve( _listNode.size() );
        for ( const GraphNode& node : _listNode )
            _listPreviewClip.push_back( AnimClip( node._name, 0.75f ) );
        for ( AnimClip& clip : _listPreviewClip )
            _previewPlayer.registerClip( clip.getName(), &clip );
    }

    void AnimationGraphPanel::tickPreview( float32 deltaSeconds )
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
            EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentNodeName(), getLoadedAssetPath() );
    }

    void AnimationGraphPanel::addNamedNode( const utf8* pName )
    {
        GraphNode n{};
        n._id          = nextNodeId();
        n._name        = ( pName != nullptr ) ? pName : "Node";
        n._position._x = 40.0f + static_cast<float32>( _listNode.size() ) * 40.0f;
        n._position._y = 40.0f + static_cast<float32>( _listNode.size() ) * 30.0f;
        _listNode.push_back( std::move( n ) );
    }
} // namespace sw::editor
