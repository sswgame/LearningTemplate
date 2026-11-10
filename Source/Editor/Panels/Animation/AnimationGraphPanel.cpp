#include "pch.h"

#include "Editor/Panels/Animation/AnimationGraphPanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Panels/Animation/Model/AnimationGraphEdit.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Skeletal/AnimationPoseNode.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/EnumName.h"

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

            /** @brief 노드 더하기 메뉴의 클립 상태 이름입니다(이름 = 클립). 포즈 노드 종류는 이 뒤에 등록 순서로 붙는다. */
            static constexpr const utf8* kArrClipStateName[] = { "Idle", "Walk", "Attack" };
            /** @brief 몽타주 섹션 시작 칸의 끌기 속도(초 / 픽셀)와 상한(초)입니다. */
            static constexpr float32 kSectionStartDragSpeed = 0.01f;
            static constexpr float32 kSectionStartMax       = 600.0f;
            /** @brief 새 섹션이 마지막 섹션 뒤에서 시작하는 간격(초)입니다(시작 차례 계약). */
            static constexpr float32 kNewSectionGap = 0.1f;

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
            /** @brief 링크 양 끝의 노드 id 입니다. 복사 · 붙여 넣기 · 복제가 쓴다. */
            static void linkNodes( const AnimationGraphLink& link, int32& outFromNode, int32& outToNode )
            {
                outFromNode = link._fromNode;
                outToNode   = link._toNode;
            }
            /** @brief 링크 양 끝을 새 노드 id 로 바꿉니다(전이 조건은 그대로). */
            static void remapLink( AnimationGraphLink& link, int32 fromNode, int32 toNode )
            {
                link._fromNode = fromNode;
                link._toNode   = toNode;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( AnimationGraphPanel, AnimationGraphPanel::kPanelID, EditorPanelCategory::Tool, 1100 );

    AnimationGraphPanel::AnimationGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetType{ AnimationGraphAsset::kAssetType }, "Move Animation Graph Nodes", "animation-graph-layout", &ed::SetCurrentEditor }
        , _preview{}
        , _layerEditor{}
        , _nameEditBuffer{}
        , _parameterEditBuffer{}
        , _nameFieldBuffer{}
        , _nameFieldShown{}
        , _listPoseTypeName{}
        , _nameFieldID{ 0 }
        , _listSelectionOrder{}
        , _selectedNodeID{ 0 }
        , _selectedLinkID{ 0 }
        , _canvasLinkID{ 0 }
    {
        _nodeGraph.setPinTypeColors( { SW_THEME_COLOR( "Graph", "TransitionPin" ) } ); // 상태 전환 핀 하나
    }

    void AnimationGraphPanel::shutdown()
    {
        _nodeGraph.shutdown();
    }

    void AnimationGraphPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        _preview.update( ImGui::GetIO().DeltaTime, getLoadedAssetPath() );

        drawAnimationToolbar();

        // 캔버스 오른쪽에 상태 · 전이 편집(언리얼 AnimBP 상태 기계 · 유니티 Animator 의 전이 인스펙터).
        const float32             availWidth = ImGui::GetContentRegionAvail().x;
        const float32             sideWidth  = MathUtil::min( 300.0f * EditorThemeUtil::getDPIScale(), availWidth * 0.4f );
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
        const bool bGraphTab = ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Graph" ) );
        EditorSelfTestMarks::record( "animationGraph.tab.graph" );
        if ( bGraphTab )
        {
            drawGraphInspector();
            ImGui::EndTabItem();
        }
        const bool bLayersTab = ImGui::BeginTabItem( SW_LOCKEY( "EdSection", "Layers" ) );
        EditorSelfTestMarks::record( "animationGraph.tab.layers" );
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
            // 상태 종류마다 단추 하나씩이던 줄을 메뉴 하나로(언리얼 AnimBP 상태 기계 · 유니티 Animator 의 "상태 만들기" 메뉴).
            if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphAddNode" ), "AddNode" ) ) )
                EditorInputGUI::openPopup( "##animationAddNode" );
            EditorSelfTestMarks::record( "animationGraph.addMenu" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "GraphAddNodeTip" ) );
            if ( ImGui::BeginPopup( "##animationAddNode" ) )
            {
                // 클립 상태 뒤에 등록된 포즈 노드 종류 — 캔버스의 더하기 메뉴와 같은 목록(`collectAddKinds`)이다.
                const utf8* const           arrClipTip[] = { SW_LOCKEY( "EditorAnimationGraph", "AddIdleTip" ), SW_LOCKEY( "EditorAnimationGraph", "AddWalkTip" ),
                                                             SW_LOCKEY( "EditorAnimationGraph", "AddAttackTip" ) };
                vector<EditorGraphNodeKind> listKind;
                collectAddKinds( listKind );
                for ( const EditorGraphNodeKind& kind : listKind )
                {
                    const bool bClipState = kind._kindID < SW_COUNT_OF( AnimationGraphPanelInternal::kArrClipStateName );
                    if ( bClipState == false && kind._kindID == SW_COUNT_OF( AnimationGraphPanelInternal::kArrClipStateName ) )
                        ImGui::SeparatorText( SW_LOCKEY( "EditorAnimationGraph", "AddPoseNodeSection" ) );
                    if ( ImGui::MenuItem( kind._pName ) && addNodeOfKind( kind._kindID ) != 0 )
                        notifyDocumentEdited( "Add Animation Graph Node" );
                    EditorSelfTestMarks::record( ( string( "animationGraph.add." ) + kind._pName ).c_str() );
                    EditorWidgets::drawTooltip( bClipState ? arrClipTip[kind._kindID] : SW_LOCKEY( "EditorAnimationGraph", "AddPoseNodeTip" ) );
                }
                ImGui::EndPopup();
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "LinkSelected" ) ) );
            // 고른 노드 둘(고른 순서)을 잇는다 — 캔버스에서 노드를 누르고 Ctrl 로 둘째를 고른다.
            if ( ImGui::Button( SW_LOCKEY( "EdText", "LinkSelected" ) ) )
                linkSelectedNodes();
            EditorSelfTestMarks::record( "animationGraph.linkSelected" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LinkSelectedTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Load" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Load" ) ) )
                reloadDocument();
            EditorSelfTestMarks::record( "animationGraph.load" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "LoadTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Save" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
                (void)saveGraphData(); // 실패는 저장 커맨드가 알린다
            EditorSelfTestMarks::record( "animationGraph.save" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "SaveTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Play" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Play" ) ) )
                _preview.play( _graph, getLoadedAssetPath() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "PlayTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Advance" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "Advance" ) ) )
                _preview.advance( _graph, getLoadedAssetPath() );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "AdvanceTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Stop" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Stop" ) ) )
                _preview.stop();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StopTip" ) );

            ImGui::SameLine();
            if ( _preview.getCurrentStateName().empty() == false )
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "PreviewNodesLinks", TextArgumentList().addText( "currentStateName", _preview.getCurrentStateName().c_str() ).addInteger( "nodeCount", static_cast<int64>( _graph._listNode.size() ) ).addInteger( "linkCount", static_cast<int64>( _graph._listLink.size() ) ) ).c_str() );
            else
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "NodesLinks", TextArgumentList().addInteger( "nodeCount", static_cast<int64>( _graph._listNode.size() ) ).addInteger( "linkCount", static_cast<int64>( _graph._listLink.size() ) ).addText( "animationGraphDocumentFileName", EditorPaths::kAnimationGraphDocumentFileName ) ).c_str() );
        }
        EditorChrome::endToolbar();
    }

    void AnimationGraphPanel::drawAnimationCanvas()
    {
        if ( beginGraphCanvas( "AnimationGraphCanvas", EditorPaths::kAnimationGraphCanvasFileName, "animGraph" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorAnimationGraph", "ContextCreateFailed" ) );
            return;
        }

        drawGraphComments();
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
            _nodeGraph.beginNode( node._id );
            if ( _preview.isCurrentState( node._name ) )
                ImGui::TextColored( EditorColorUtil::toImVec4( AnimationGraphPanelInternal::kCurrentStateText ), "%s", node._name.c_str() );
            else
                ImGui::TextUnformatted( node._name.c_str() );
            if ( EditorSelfTestMarks::isEnabled() )
            {
                // 캔버스 안의 위젯 좌표는 캔버스 좌표다 — 화면 좌표로 바꿔 적는다(시나리오가 노드를 누르고 Ctrl 로 둘째를 고른다).
                const ImVec2 nameMin = ed::CanvasToScreen( ImGui::GetItemRectMin() );
                const ImVec2 nameMax = ed::CanvasToScreen( ImGui::GetItemRectMax() );
                EditorSelfTestMarks::recordRect( ( "animationGraph.node." + node._name ).c_str(), float2{ nameMin.x, nameMin.y }, float2{ nameMax.x, nameMax.y } );
            }
            bool bLinkedIn  = false;
            bool bLinkedOut = false;
            for ( const GraphLink& link : _graph._listLink )
            {
                bLinkedIn  = bLinkedIn || link._toNode == node._id;
                bLinkedOut = bLinkedOut || link._fromNode == node._id;
            }
            _nodeGraph.beginPin( AnimationGraphPanelInternal::pinIn( node._id ), true );
            _nodeGraph.drawPinIcon( 0, bLinkedIn );
            ImGui::SameLine();
            ImGui::TextUnformatted( "In" );
            _nodeGraph.endPin();
            ImGui::SameLine();
            _nodeGraph.beginPin( AnimationGraphPanelInternal::pinOut( node._id ), false );
            ImGui::TextUnformatted( "Out" );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( 0, bLinkedOut );
            _nodeGraph.endPin();
            _nodeGraph.endNode();

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
        if ( const int32 flowLinkID = _preview.takeFlowLinkID(); flowLinkID != 0 )
            ed::Flow( toLinkID( flowLinkID ) );
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
            if ( ImGui::MenuItem( SW_LOCKEY( "EdLabel", "Duplicate" ) ) )
                duplicateGraphNodes( vector<int32>{ _selectedNodeID }, &AnimationGraphPanelInternal::linkNodes, &AnimationGraphPanelInternal::remapLink, "Duplicate Animation Graph Node" );
            if ( ImGui::MenuItem( SW_LOCKEY( "EdVerb", "Delete" ) ) )
                requestNodeDeletion( _selectedNodeID );
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

        vector<EditorGraphNodeKind> listKind;
        collectAddKinds( listKind );
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) && kindID < listKind.size() )
        {
            const int32 nodeID = addNodeOfKind( kindID );
            if ( nodeID != 0 )
            {
                placeNodeOnNextDraw( nodeID, canvasPosition );
                notifyDocumentEdited( "Add Animation Graph Node" );
            }
        }

        // 복사 · 잘라내기 · 붙여 넣기 · 복제(Ctrl+C · X · V · D). 잘라내기는 지우기 요청을 내므로 지우기 처리보다 먼저다.
        processCanvasEditCommands( &AnimationGraphPanelInternal::linkNodes, &AnimationGraphPanelInternal::remapLink, "Paste Animation Graph Nodes" );

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
        const ToolAssetLoadResult result = AnimationGraphEdit::loadGraph( data, getLoadedAssetPath() );
        applyLoadedGraph( std::move( data ), result );
        _preview.resetForDocument();
        _layerEditor.clearSelection();
        return result;
    }

    void AnimationGraphPanel::applyDocumentText( string_view text )
    {
        string baseID = _graph._baseID;
        EditorGraphDocumentPanel<AnimationGraphAsset>::applyDocumentText( text );
        _graph._baseID = std::move( baseID );
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
        if ( AnimationGraphEdit::saveGraph( data, getLoadedAssetPath() ) == false )
            return false;

        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool AnimationGraphPanel::saveDocument()
    {
        return saveGraphData();
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

    int32 AnimationGraphPanel::addPoseNode( const hashed_string& typeName )
    {
        AnimationGraphPoseNodeDef def;
        if ( engine::getSubsystem<AnimationPoseNodeRegistry>().makeDefaultDef( typeName, def ) == false )
            return 0; // 오류는 표가 남겼다
        const int32 nodeID                = addNamedNode( typeName.c_str() );
        _graph._listNode.back()._poseNode = std::move( def );
        return nodeID;
    }

    void AnimationGraphPanel::collectAddKinds( vector<EditorGraphNodeKind>& outListKind )
    {
        // 표는 게임 · 플러그인이 종류를 더할 수 있어 메뉴를 열 때마다 다시 읽는다. 이름 포인터는 이 목록(`_listPoseTypeName`)이 다음 호출까지 든다.
        _listPoseTypeName = engine::getSubsystem<AnimationPoseNodeRegistry>().getTypeNames();
        outListKind.clear();
        uint32 kindID = 0;
        for ( const utf8* pName : AnimationGraphPanelInternal::kArrClipStateName )
        {
            outListKind.push_back( EditorGraphNodeKind{ pName, "State", kindID++ } );
        }
        for ( const hashed_string& typeName : _listPoseTypeName )
        {
            outListKind.push_back( EditorGraphNodeKind{ typeName.c_str(), "Pose", kindID++ } );
        }
    }

    int32 AnimationGraphPanel::addNodeOfKind( uint32 kindID )
    {
        constexpr uint32 clipStateCount = SW_COUNT_OF( AnimationGraphPanelInternal::kArrClipStateName );
        if ( kindID < clipStateCount )
            return addNamedNode( AnimationGraphPanelInternal::kArrClipStateName[kindID] );
        const uint32 poseIndex = kindID - clipStateCount;
        return poseIndex < _listPoseTypeName.size() ? addPoseNode( _listPoseTypeName[poseIndex] ) : 0;
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
            if ( EditorNodeGraph::isCommentNode( static_cast<int32>( arrNode[index].Get() ) ) == false ) // 주석 상자는 상태가 아니다
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
            SW_LOG_WARNING_ID( "AnimationGraph.LinkSelectedNeedsTwo", "Link Selected needs two selected nodes (click one, Ctrl+click the other)" );
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

    uint32 AnimationGraphPanel::countConditionLinks() const
    {
        uint32 count{ 0 };
        for ( const GraphLink& link : _graph._listLink )
        {
            count += link._op != AnimationConditionOp::None ? 1u : 0u;
        }
        return count;
    }

    uint32 AnimationGraphPanel::countBoneMaskEntries() const
    {
        uint32 count{ 0 };
        for ( const AnimationBoneMaskDef& mask : _graph._listBoneMask )
        {
            count += static_cast<uint32>( mask._listEntry.size() );
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

        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "State" ) );
        if ( ImGui::IsAnyItemActive() == false )
            _nameEditBuffer = pNode->_name;
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Clip" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        EditorWidgets::drawTextField( "##stateName", _nameEditBuffer );
        EditorSelfTestMarks::record( "animationGraph.state.name" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StateClipTip" ) );
        const bool bStateNameCommitted = ImGui::IsItemDeactivatedAfterEdit() && _nameEditBuffer.empty() == false && _nameEditBuffer != pNode->_name;
        if ( bStateNameCommitted )
        {
            pNode->_name = _nameEditBuffer;
            notifyDocumentEdited( "Rename Animation State" );
        }
        const utf8* const arrLoop[] = { "Default", "Loop", "Once" };
        int32             loopIndex = pNode->_loop.has_value() == false ? 0 : ( *pNode->_loop ? 1 : 2 );
        ImGui::TextUnformatted( "Loop" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        if ( ImGui::Combo( "##stateLoop", &loopIndex, arrLoop, 3 ) )
        {
            pNode->_loop = loopIndex == 0 ? optional<bool>{} : optional<bool>( loopIndex == 1 );
            notifyDocumentEdited( "Edit Animation State" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "StateLoopTip" ) );
        if ( pNode->hasPoseNode() )
            drawPoseNodeInspector( *pNode );
    }

    bool AnimationGraphPanel::drawNameField( const utf8* pID, hashed_string& inoutName )
    {
        // 칸마다 버퍼를 두지 않는다 — 한 번에 하나만 친다. 치는 칸은 패널 버퍼, 나머지는 값을 그대로 보인다.
        const uint32 fieldID  = ImGui::GetID( pID );
        const bool   bEditing = _nameFieldID == fieldID;
        if ( bEditing == false )
            _nameFieldShown = string( inoutName.viewOrEmpty() );
        string& text = bEditing ? _nameFieldBuffer : _nameFieldShown;
        (void)EditorWidgets::drawTextField( pID, text, -FLT_MIN );
        if ( ImGui::IsItemActivated() )
        {
            _nameFieldID     = fieldID;
            _nameFieldBuffer = text;
        }
        if ( ImGui::IsItemDeactivated() == false )
            return false;
        _nameFieldID          = 0;
        const bool bCommitted = ImGui::IsItemDeactivatedAfterEdit() && text != inoutName.viewOrEmpty();
        if ( bCommitted )
            inoutName = hashed_string( text.c_str() );
        return bCommitted;
    }

    void AnimationGraphPanel::drawPoseNodeInspector( GraphNode& node )
    {
        const AnimationPoseNodeType* pType = engine::getSubsystem<AnimationPoseNodeRegistry>().findType( node._poseNode._type );
        ImGui::SeparatorText( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeSection" ) );
        ImGui::TextDisabled( "%s", node._poseNode._type.c_str() );
        if ( pType == nullptr )
            return; // 로드가 모르는 종류를 받지 않는다 — 코드가 지운 종류다
        AnimationGraphPoseNodeDef edited  = node._poseNode;
        bool                      bEdited = false;

        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeInputs" ), SW_LOCKEY( "EditorAnimationGraph", "PoseNodeInputsTip" ) );
        for ( size_t inputIndex = 0; inputIndex < edited._listInput.size(); ++inputIndex )
        {
            ImGui::PushID( static_cast<int32>( inputIndex ) );
            bEdited = drawNameField( "##poseInput", edited._listInput[inputIndex] ) || bEdited;
            ImGui::PopID();
        }
        EditorSelfTestMarks::record( "animationGraph.pose.input" );

        if ( edited._weightParameter.empty() == false )
        {
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeWeight" ), SW_LOCKEY( "EditorAnimationGraph", "PoseNodeWeightTip" ) );
            bEdited = drawNameField( "##poseWeight", edited._weightParameter ) || bEdited;
        }

        if ( edited._mirrorAxis != AnimationMirrorAxis::None )
        {
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeMirrorAxis" ), SW_LOCKEY( "EditorAnimationGraph", "PoseNodeMirrorAxisTip" ) );
            ImGui::SetNextItemWidth( -FLT_MIN );
            if ( ImGui::BeginCombo( "##poseMirrorAxis", getEnumName( edited._mirrorAxis ) ) )
            {
                for ( const AnimationMirrorAxis axis : { AnimationMirrorAxis::X, AnimationMirrorAxis::Y, AnimationMirrorAxis::Z } )
                {
                    if ( ImGui::Selectable( getEnumName( axis ), axis == edited._mirrorAxis ) && axis != edited._mirrorAxis )
                    {
                        edited._mirrorAxis = axis;
                        bEdited            = true;
                    }
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::record( "animationGraph.pose.mirrorAxis" );
        }

        if ( edited._listSection.empty() == false )
        {
            // 몽타주 섹션 — 이름 · 시작 · 다음 · 분기 파라미터 · 분기(언리얼 AnimMontage 의 섹션 · 링크 자리). 끝은 다음 섹션의 시작이다.
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeSections" ), SW_LOCKEY( "EditorAnimationGraph", "PoseNodeSectionsTip" ) );
            size_t removeIndex = edited._listSection.size();
            for ( size_t sectionIndex = 0; sectionIndex < edited._listSection.size(); ++sectionIndex )
            {
                AnimationMontageSection& section = edited._listSection[sectionIndex];
                ImGui::PushID( static_cast<int32>( sectionIndex ) );
                ImGui::SeparatorText( section._name.viewOrEmpty().empty() ? "?" : section._name.c_str() );
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "SectionName" ) );
                bEdited = drawNameField( "##sectionName", section._name ) || bEdited;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "SectionStart" ), SW_LOCKEY( "EditorAnimationGraph", "SectionStartTip" ) );
                ImGui::SetNextItemWidth( -FLT_MIN );
                ImGui::DragFloat( "##sectionStart", &section._startSeconds, AnimationGraphPanelInternal::kSectionStartDragSpeed, 0.0f, AnimationGraphPanelInternal::kSectionStartMax, "%.2f s" );
                bEdited = ImGui::IsItemDeactivatedAfterEdit() || bEdited;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "SectionNext" ), SW_LOCKEY( "EditorAnimationGraph", "SectionNextTip" ) );
                bEdited = drawNameField( "##sectionNext", section._next ) || bEdited;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "SectionBranchParam" ), SW_LOCKEY( "EditorAnimationGraph", "SectionBranchParamTip" ) );
                bEdited = drawNameField( "##sectionBranchParam", section._branchParameter ) || bEdited;
                EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EditorAnimationGraph", "SectionBranch" ) );
                bEdited = drawNameField( "##sectionBranch", section._branch ) || bEdited;
                if ( edited._listSection.size() > 1u && ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Delete" ) ) )
                    removeIndex = sectionIndex;
                ImGui::PopID();
            }
            if ( removeIndex < edited._listSection.size() )
            {
                edited._listSection.erase( edited._listSection.begin() + static_cast<ptrdiff_t>( removeIndex ) );
                bEdited = true;
            }
            if ( ImGui::SmallButton( SW_LOCKEY( "EditorAnimationGraph", "PoseNodeAddSection" ) ) )
            {
                // 새 섹션은 마지막 섹션 뒤 시작이다(차례 계약). 이름은 번호로 유일하게.
                AnimationMontageSection added{};
                added._name         = hashed_string( ( string( "Section" ) + to_string( edited._listSection.size() + 1u ).c_str() ).c_str() );
                added._startSeconds = edited._listSection.back()._startSeconds + AnimationGraphPanelInternal::kNewSectionGap;
                edited._listSection.push_back( added );
                bEdited = true;
            }
            EditorSelfTestMarks::record( "animationGraph.pose.addSection" );
        }

        // 계약을 어기는 편집(빈 입력 · 섹션 차례 · 없는 링크)은 들이지 않는다 — 까닭은 표가 오류로 남겼다.
        if ( bEdited && AnimationPoseNodeRegistry::isSatisfiedBy( *pType, edited, node._name ) )
        {
            node._poseNode = std::move( edited );
            notifyDocumentEdited( "Edit Animation Pose Node" );
        }
    }

    void AnimationGraphPanel::drawTransitionList()
    {
        // 전이 목록 — 누르면 그 전이를 고른다(캔버스에서 링크를 눌러도 된다).
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Transitions" ) );
        for ( size_t index = 0; index < _graph._listLink.size(); ++index )
        {
            const GraphLink&                        link   = _graph._listLink[index];
            const auto                              itFrom = std::find_if( _graph._listNode.begin(), _graph._listNode.end(), [&link]( const GraphNode& node )
                                         { return node._id == link._fromNode; } );
            const auto                              itTo   = std::find_if( _graph._listNode.begin(), _graph._listNode.end(), [&link]( const GraphNode& node )
                                           { return node._id == link._toNode; } );
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "%s -> %s%s", itFrom != _graph._listNode.end() ? itFrom->_name.c_str() : "?", itTo != _graph._listNode.end() ? itTo->_name.c_str() : "?",
                          link._op != AnimationConditionOp::None ? "  [if]" : "" );
            ImGui::PushID( static_cast<int32>( link._id ) );
            if ( ImGui::Selectable( label.c_str(), link._id == _selectedLinkID ) )
            {
                _selectedLinkID = link._id;
                _selectedNodeID = 0;
            }
            if ( EditorSelfTestMarks::isEnabled() )
            {
                fixed_string<constant::buffer::kMax64> text;
                formatstring( text.data(), text.capacity(), "animationGraph.link.%#", static_cast<uint64>( index ) );
                EditorSelfTestMarks::record( text.c_str() );
            }
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
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Condition" ) );
        bool bEdited = false;
        if ( ImGui::IsAnyItemActive() == false )
            _parameterEditBuffer = pLink->_parameter.viewOrEmpty(); // 빈 이름은 빈 칸이다("None" 이 아니다)
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Param" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        EditorWidgets::drawTextField( "##conditionParam", _parameterEditBuffer );
        EditorSelfTestMarks::record( "animationGraph.condition.param" );
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
        const bool bOpOpen = ImGui::BeginCombo( "##conditionOp", pLink->_op == AnimationConditionOp::None ? "(on finish)" : getEnumName( pLink->_op ) );
        EditorSelfTestMarks::record( "animationGraph.condition.op" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ConditionOpTip" ) );
        if ( bOpOpen )
        {
            for ( int32 op = 0; op <= static_cast<int32>( AnimationConditionOp::Trigger ); ++op )
            {
                const AnimationConditionOp value = static_cast<AnimationConditionOp>( op );
                if ( ImGui::Selectable( value == AnimationConditionOp::None ? "(on finish)" : getEnumName( value ), op == opIndex ) )
                {
                    pLink->_op = value;
                    bEdited    = true;
                }
                if ( EditorSelfTestMarks::isEnabled() )
                {
                    fixed_string<constant::buffer::kMax64> text;
                    formatstring( text.data(), text.capacity(), "animationGraph.condition.op.%#", static_cast<uint64>( op ) );
                    EditorSelfTestMarks::record( text.c_str() );
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Value" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::DragFloat( "##conditionValue", &pLink->_threshold, 0.05f );
        EditorSelfTestMarks::record( "animationGraph.condition.value" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "ConditionValueTip" ) );
        bEdited = ImGui::IsItemDeactivatedAfterEdit() || bEdited;
        ImGui::TextUnformatted( SW_LOCKEY( "EdText", "Blend" ) );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( -FLT_MIN );
        ImGui::DragFloat( "##transitionBlend", &pLink->_blendSeconds, 0.01f, -1.0f, 10.0f, pLink->_blendSeconds < 0.0f ? "(player default)" : "%.2f s" );
        EditorSelfTestMarks::record( "animationGraph.condition.blend" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAnimationGraph", "TransitionBlendTip" ) );
        bEdited = ImGui::IsItemDeactivatedAfterEdit() || bEdited;
        if ( bEdited )
            notifyDocumentEdited( "Edit Animation Transition" );
    }
} // namespace sw::editor
