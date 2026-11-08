#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphPanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphBridge.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuitComponent.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        struct GimmickGraphPanelInternal
        {
            /// @brief 그래프 복사 버퍼의 종류 열쇠 — 다른 그래프에는 붙지 않는다.
            static constexpr string_view kClipboardKind      = "GimmickGraph";
            static constexpr const utf8* kCanvasID           = "GimmickGraphCanvas";
            static constexpr const utf8* kCanvasSettingsFile = "GimmickGraphCanvas.json";
            static constexpr float32     kSidePanelWidth     = 340.0f; ///< 96 DPI 기준입니다

            // 핀 번호 = 노드 그래프 번호 × 64 + (출력이면 32) + 포트 번호. 포트는 종류마다 최대 32 개다(`GimmickGraphModel::kMaxPortCount`).
            static constexpr int32 kPinsPerNode   = 2 * GimmickGraphModel::kMaxPortCount;
            static constexpr int32 kOutputPinBase = GimmickGraphModel::kMaxPortCount;

            static int32 makeInputPin( int32 nodeID, int32 port ) { return nodeID * kPinsPerNode + port; }
            static int32 makeOutputPin( int32 nodeID, int32 port ) { return nodeID * kPinsPerNode + kOutputPinBase + port; }
            static int32 decodeNode( int32 pinID ) { return pinID / kPinsPerNode; }
            static bool  isOutputPin( int32 pinID ) { return pinID % kPinsPerNode >= kOutputPinBase; }
            static int32 decodePort( int32 pinID ) { return pinID % kOutputPinBase; }

            static ImVec4 getCategoryColor( GimmickNodeCategory category )
            {
                switch ( category )
                {
                    case GimmickNodeCategory::Sensor:
                        return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Graph", "Sensor" ) );
                    case GimmickNodeCategory::Operator:
                        return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Graph", "Operator" ) );
                    case GimmickNodeCategory::Actuator:
                        return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Graph", "Actuator" ) );
                }
                return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Text" ) );
            }

            static string makeMark( string_view prefix, string_view name )
            {
                string mark( prefix );
                mark.append( name.data(), name.size() );
                return mark;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    GimmickGraphPanel::GimmickGraphPanel()
        : IEditorPanel( false )
        , _model{ game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry() }
        , _nodeGraph{}
        , _listIssue{}
        , _ownerHandle{}
        , _formFrom{}
        , _formTo{}
        , _editText{}
        , _statusText{}
        , _signature{ 0 }
        , _editID{ 0 }
        , _selectedNode{ 0 }
        , _bSeedComments{ SW_FALSE }
    {
        _nodeGraph.setCommentsEnabled( true ); // 주석 상자는 컴포넌트에 실린다(`GimmickCircuitComponent::_listComment`)
    }

    void GimmickGraphPanel::shutdown()
    {
        _nodeGraph.shutdown();
    }

    GimmickCircuitComponent* GimmickGraphPanel::findCircuit()
    {
        // 고른 오브젝트의 컴포넌트를 고치는 창(디테일 패널의 확장)이라 겨누는 창의 선택이다 — 미리보기 창의 오브젝트도 편집한다.
        const EditorSelection* pSelection = editor::getFocusedSelection();
        if ( pSelection != nullptr )
        {
            vector<GameObject*> listSelected;
            pSelection->getSelectedObjects( listSelected );
            for ( GameObject* pObject : listSelected )
            {
                GimmickCircuitComponent* pCircuit = pObject != nullptr ? pObject->getComponent<GimmickCircuitComponent>() : nullptr;
                if ( pCircuit != nullptr )
                    return pCircuit;
            }
        }
        // 선택에 회로가 없으면 보던 회로를 이어서 본다 - 대상 오브젝트를 고르려고 다른 오브젝트를 고른 경우다
        GameObject* pOwner = _ownerHandle.isValid() ? findGameObject( _ownerHandle ) : nullptr;
        return pOwner != nullptr ? pOwner->getComponent<GimmickCircuitComponent>() : nullptr;
    }

    void GimmickGraphPanel::reload( GimmickCircuitComponent& circuit )
    {
        if ( GimmickGraphBridge::load( circuit, _model ) == false )
        {
            _model.clear();
            _statusText = "Could not read the circuit properties";
        }
        // 자리는 컴포넌트에 실려 왔다 — 다음 그리기에서 캔버스에 넣는다(주석 상자도).
        for ( GimmickGraphNode& node : _model.getMutableNodes() )
        {
            node._bPlaced = false;
        }
        _bSeedComments = SW_TRUE;
        _signature     = _model.computeSignature();
        if ( _model.findNodeIndex( _selectedNode ).has_value() == false )
            _selectedNode = 0;
        _formFrom = PortChoice{};
        _formTo   = PortChoice{};
        refreshIssues();
    }

    void GimmickGraphPanel::refreshIssues()
    {
        _model.collectIssues( _listIssue );
    }

    void GimmickGraphPanel::drawComments( GimmickCircuitComponent& circuit )
    {
        // 컴포넌트는 커밋 전까지 그대로라 "고치기 전" 스냅숏은 커밋할 때 찍어도 된다.
        vector<GraphCommentBox>& listComment = _model.getMutableComments();
        const bool               bRenamed    = _nodeGraph.drawComments( listComment, _bSeedComments || _nodeGraph.needsContentFit() );
        _bSeedComments                       = SW_FALSE;
        if ( bRenamed )
            commitEdit( circuit, beginEdit( circuit ), "Rename Gimmick Comment" );
        if ( _nodeGraph.addRequestedComment( listComment ) )
            commitEdit( circuit, beginEdit( circuit ), "Add Gimmick Comment" );
    }

    void GimmickGraphPanel::commitLayoutWhenReleased( GimmickCircuitComponent& circuit )
    {
        // 캔버스 좌표의 반올림을 옮김으로 읽지 않는다(문서 그래프 패널과 같은 문턱).
        for ( GimmickGraphNode& node : _model.getMutableNodes() )
        {
            if ( node._bPlaced == false )
                continue;
            const ImVec2 position = ed::GetNodePosition( toNodeID( node._graphID ) );
            if ( EditorSessionPolicy::hasNodeMoved( node._position._x, node._position._y, position.x, position.y ) )
                node._position = float2{ position.x, position.y };
        }
        if ( _nodeGraph.needsContentFit() == false )
            (void)_nodeGraph.syncCommentLayout( _model.getMutableComments() ); // 바뀐 것은 아래 서명 비교가 본다
        // 구조 편집은 그 자리에서 커밋한다 — 커밋한 서명과 다르면 남은 것은 배치(끌기 · 늘이기 · 자동 배치)다. 끄는 동안에는 쌓기만 한다.
        if ( ImGui::IsMouseDown( ImGuiMouseButton_Left ) || _model.computeSignature() == _signature )
            return;
        commitEdit( circuit, beginEdit( circuit ), "Move Gimmick Nodes" );
    }

    void GimmickGraphPanel::drawContent()
    {
        GimmickCircuitComponent* pCircuit = findCircuit();
        if ( pCircuit == nullptr || pCircuit->getOwner() == nullptr )
        {
            _ownerHandle = GameObjectHandle{};
            _model.clear();
            _listIssue.clear();
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "SelectAnObjectThatHasAGimmickCircuit" ) );
            return;
        }

        const GameObjectHandle handle = pCircuit->getOwner()->getHandle();
        if ( handle != _ownerHandle )
        {
            _ownerHandle = handle;
            reload( *pCircuit );
            _nodeGraph.requestContentFit();
        }
        else if ( GimmickGraphBridge::computeSignature( *pCircuit ) != _signature )
        {
            reload( *pCircuit ); // 되돌리기나 인스펙터가 바깥에서 바꿨다
        }

        drawToolbar( *pCircuit );
        ImGui::Separator();
        drawCanvasArea( *pCircuit );
        ImGui::SameLine();
        drawSidePanel( *pCircuit );
    }

    void GimmickGraphPanel::drawToolbar( GimmickCircuitComponent& circuit )
    {
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AddNode" ) ) )
            EditorInputGUI::openPopup( "##gimmickAddNode" );
        EditorSelfTestMarks::record( "gimmick.addnode" );
        if ( ImGui::BeginPopup( "##gimmickAddNode" ) )
        {
            const GimmickNodeCategory arrCategory[] = { GimmickNodeCategory::Sensor, GimmickNodeCategory::Operator, GimmickNodeCategory::Actuator };
            for ( const GimmickNodeCategory category : arrCategory )
            {
                ImGui::TextDisabled( "%s", getEnumName( category ) );
                for ( const GimmickNodeKind& kind : game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry().getKinds() )
                {
                    if ( kind._category != category )
                        continue;
                    if ( ImGui::Selectable( kind._name.c_str() ) )
                    {
                        const float2 position = _model.computeNewNodePosition();
                        editAddNode( circuit, kind._name, &position );
                    }
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::record( GimmickGraphPanelInternal::makeMark( "gimmick.kind.", kind._name.c_str() ).c_str() );
                }
            }
            ImGui::EndPopup();
        }

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "AutoLayout" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "AutoLayout" ) ) )
            _model.autoLayout();
        EditorSelfTestMarks::record( "gimmick.layout" );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "NodesWiresProblems", TextArgumentList().addText( "name", circuit.getOwner()->getName().c_str() ).addInteger( "nodesCount", static_cast<int64>( _model.getNodes().size() ) ).addInteger( "wiresCount", static_cast<int64>( _model.getWires().size() + _model.getOrphanWires().size() ) ).addInteger( "issueCount", static_cast<int64>( _listIssue.size() ) ) ).c_str() );
        if ( _statusText.empty() == false )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "| %s", _statusText.c_str() );
        }
    }

    void GimmickGraphPanel::drawCanvasArea( GimmickCircuitComponent& circuit )
    {
        const float32     sideWidth = GimmickGraphPanelInternal::kSidePanelWidth * EditorThemeUtil::getDPIScale();
        EditorSectionDesc canvasDesc{};
        canvasDesc._pID       = "GimmickCanvasRegion";
        canvasDesc._kind      = EditorSectionKind::Child;
        canvasDesc._childSize = float2{ ImGui::GetContentRegionAvail().x - sideWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f };
        canvasDesc._flags     = EditorSectionFlags::NoScrollbar | EditorSectionFlags::NoScrollWithMouse;
        EditorChrome::beginSection( canvasDesc );

        // 확장 모듈은 imgui-node-editor 를 자기 사본으로 링크하므로, 캔버스를 연 뒤 이 사본에도 지금 편집기를 건다(`EditorGraphDocumentPanel::beginGraphCanvas` 와 같다).
        if ( _nodeGraph.beginCanvas( GimmickGraphPanelInternal::kCanvasID, GimmickGraphPanelInternal::kCanvasSettingsFile, "gimmickGraph" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "TheNodeEditorCouldNotBeCreated" ) );
            EditorChrome::endSection();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );

        drawComments( circuit );
        for ( GimmickGraphNode& node : _model.getMutableNodes() )
        {
            drawNode( node );
            if ( node._bPlaced == false )
            {
                ed::SetNodePosition( toNodeID( node._graphID ), ImVec2{ node._position._x, node._position._y } );
                node._bPlaced = true;
            }
        }
        for ( const GimmickGraphWire& wire : _model.getWires() )
        {
            // 모델이 노드를 지울 때 배선도 지우므로 두 끝은 늘 있다.
            const GimmickNodeKind* pFromKind = _model.findKind( _model.getNodes()[_model.findNodeIndex( wire._fromNode ).value()] );
            const GimmickNodeKind* pToKind   = _model.findKind( _model.getNodes()[_model.findNodeIndex( wire._toNode ).value()] );
            if ( pFromKind == nullptr || pToKind == nullptr )
                continue;
            const int32 outPort = pFromKind->findOutput( hashed_string( wire._fromPort ) );
            const int32 inPort  = pToKind->findInput( hashed_string( wire._toPort ) );
            if ( outPort < 0 || inPort < 0 )
                continue;
            const ImVec4 color = wire._bInvert ? EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ) : ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            ed::Link( toLinkID( wire._graphID ), toPinID( GimmickGraphPanelInternal::makeOutputPin( wire._fromNode, outPort ) ),
                      toPinID( GimmickGraphPanelInternal::makeInputPin( wire._toNode, inPort ) ), color, wire._bInvert ? 3.0f : 1.0f );
        }

        vector<EditorGraphNodeIssue> listNodeIssue;
        for ( const GimmickGraphIssue& issue : _listIssue )
        {
            if ( issue._nodeGraphID == GimmickGraphModel::kNoGraphID )
                continue;
            EditorGraphNodeIssue nodeIssue;
            nodeIssue._nodeID  = issue._nodeGraphID;
            nodeIssue._message = issue._message;
            listNodeIssue.push_back( std::move( nodeIssue ) );
        }
        _nodeGraph.setNodeIssues( std::move( listNodeIssue ) );
        _nodeGraph.drawNodeIssues();

        handleCanvasInteractions( circuit );

        (void)_nodeGraph.findSelectedNode( _selectedNode ); // 아무것도 안 고르면 앞의 선택을 둔다

        commitLayoutWhenReleased( circuit );
        _nodeGraph.finishCanvas( static_cast<uint32>( _model.getNodes().size() ) );
        ed::SetCurrentEditor( nullptr );
        EditorChrome::endSection();
    }

    void GimmickGraphPanel::drawNode( const GimmickGraphNode& node )
    {
        const GimmickNodeKind* pKind = _model.findKind( node );
        _nodeGraph.beginNode( node._graphID );
        ImGui::TextColored( pKind != nullptr ? GimmickGraphPanelInternal::getCategoryColor( pKind->_category ) : EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", node._id.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", node._kind.c_str() );
        if ( pKind == nullptr )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "Unknown node kind" );
        }
        else
        {
            ImGui::BeginGroup();
            for ( size_t portIndex = 0; portIndex < pKind->_listInput.size(); ++portIndex )
            {
                const int32 pinID = GimmickGraphPanelInternal::makeInputPin( node._graphID, static_cast<int32>( portIndex ) );
                _nodeGraph.beginPin( pinID, true );
                _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
                ImGui::SameLine();
                ImGui::TextUnformatted( pKind->_listInput[portIndex].c_str() );
                _nodeGraph.endPin();
            }
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            for ( size_t portIndex = 0; portIndex < pKind->_listOutput.size(); ++portIndex )
            {
                const int32 pinID = GimmickGraphPanelInternal::makeOutputPin( node._graphID, static_cast<int32>( portIndex ) );
                _nodeGraph.beginPin( pinID, false );
                ImGui::TextUnformatted( pKind->_listOutput[portIndex].c_str() );
                ImGui::SameLine();
                _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
                _nodeGraph.endPin();
            }
            ImGui::EndGroup();
        }
        _nodeGraph.endNode();
    }

    bool GimmickGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const optional<uint32> nodeIndex = _model.findNodeIndex( GimmickGraphPanelInternal::decodeNode( pinID ) );
        if ( nodeIndex.has_value() == false )
            return false;
        const GimmickNodeKind* pKind = _model.findKind( _model.getNodes()[*nodeIndex] );
        if ( pKind == nullptr )
            return false;
        const bool   bOutput = GimmickGraphPanelInternal::isOutputPin( pinID );
        const size_t port    = static_cast<size_t>( GimmickGraphPanelInternal::decodePort( pinID ) );
        if ( port >= ( bOutput ? pKind->_listOutput.size() : pKind->_listInput.size() ) )
            return false;
        outInfo._type   = 0;
        outInfo._bInput = bOutput == false;
        return true;
    }

    bool GimmickGraphPanel::isPinLinked( int32 pinID ) const
    {
        const int32 nodeID = GimmickGraphPanelInternal::decodeNode( pinID );
        const int32 port   = GimmickGraphPanelInternal::decodePort( pinID );
        for ( const GimmickGraphWire& wire : _model.getWires() )
        {
            const bool  bOutput  = GimmickGraphPanelInternal::isOutputPin( pinID );
            const int32 wireNode = bOutput ? wire._fromNode : wire._toNode;
            if ( wireNode != nodeID )
                continue;
            const optional<uint32> nodeIndex = _model.findNodeIndex( nodeID );
            const GimmickNodeKind* pKind     = nodeIndex.has_value() ? _model.findKind( _model.getNodes()[*nodeIndex] ) : nullptr;
            if ( pKind == nullptr )
                continue;
            const int32 wirePort = bOutput ? pKind->findOutput( hashed_string( wire._fromPort ) ) : pKind->findInput( hashed_string( wire._toPort ) );
            if ( wirePort == port )
                return true;
        }
        return false;
    }

    void GimmickGraphPanel::handleCanvasInteractions( GimmickCircuitComponent& circuit )
    {
        // 새 링크: 방향과 타입 판정, 거절 표시는 프레임워크(`EditorNodeGraph::queryNewLink`)가 한다
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &GimmickGraphPanel::findGraphPin, this ), fromPin, toPin ) )
        {
            const int32            fromNode  = GimmickGraphPanelInternal::decodeNode( fromPin );
            const int32            toNode    = GimmickGraphPanelInternal::decodeNode( toPin );
            const optional<uint32> fromIndex = _model.findNodeIndex( fromNode );
            const optional<uint32> toIndex   = _model.findNodeIndex( toNode );
            const GimmickNodeKind* pFromKind = fromIndex.has_value() ? _model.findKind( _model.getNodes()[*fromIndex] ) : nullptr;
            const GimmickNodeKind* pToKind   = toIndex.has_value() ? _model.findKind( _model.getNodes()[*toIndex] ) : nullptr;
            if ( pFromKind != nullptr && pToKind != nullptr )
            {
                const size_t fromPort = static_cast<size_t>( GimmickGraphPanelInternal::decodePort( fromPin ) );
                const size_t toPort   = static_cast<size_t>( GimmickGraphPanelInternal::decodePort( toPin ) );
                if ( fromPort < pFromKind->_listOutput.size() && toPort < pToKind->_listInput.size() )
                    editConnect( circuit, fromNode, pFromKind->_listOutput[fromPort].c_str(), toNode, pToKind->_listInput[toPort].c_str() );
            }
        }

        // 빈 곳을 오른쪽 클릭하면 노드 검색 팝업이 열린다. 고른 노드는 오른쪽 클릭한 위치에 놓는다
        vector<EditorGraphNodeKind>    listKind;
        const vector<GimmickNodeKind>& listRegistryKind = game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry().getKinds();
        for ( size_t kindIndex = 0; kindIndex < listRegistryKind.size(); ++kindIndex )
        {
            listKind.push_back( EditorGraphNodeKind{ listRegistryKind[kindIndex]._name.c_str(), getEnumName( listRegistryKind[kindIndex]._category ),
                                                     static_cast<uint32>( kindIndex ) } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) && kindID < listRegistryKind.size() )
            editAddNode( circuit, listRegistryKind[kindID]._name, &canvasPosition );

        handleEditCommands( circuit );

        // 삭제: 노드를 지우면 그 노드에 닿은 배선도 모델이 같이 지운다
        EditorGraphRemoval removal;
        _nodeGraph.collectRemovals( removal );
        if ( removal.isEmpty() == false )
            editRemove( circuit, removal );
    }

    bool GimmickGraphPanel::drawCommittedText( const utf8* pID, const string& current, string& outText, const utf8* /*pHint*/ )
    {
        ImGui::PushID( pID );
        const ImGuiID fieldID  = ImGui::GetID( "##field" );
        const bool    bEditing = _editID == fieldID;
        string        display  = bEditing ? _editText : current;
        const bool    bChanged = EditorWidgets::drawTextField( "##field", display, -1.0f );
        if ( bChanged )
        {
            _editID   = fieldID;
            _editText = display;
        }
        bool bCommitted = false;
        if ( ImGui::IsItemDeactivatedAfterEdit() )
        {
            outText    = _editText;
            bCommitted = _editText != current;
            _editID    = 0;
        }
        else if ( ImGui::IsItemDeactivated() && bEditing )
        {
            _editID = 0;
        }
        ImGui::PopID();
        return bCommitted;
    }

    void GimmickGraphPanel::drawSidePanel( GimmickCircuitComponent& circuit )
    {
        EditorSectionDesc sideDesc{};
        sideDesc._pID   = "GimmickSidePanel";
        sideDesc._kind  = EditorSectionKind::Child;
        sideDesc._flags = EditorSectionFlags::Border;
        EditorChrome::beginSection( sideDesc );
        drawNodeInspector( circuit );
        ImGui::Separator();
        drawWireSection( circuit );
        ImGui::Separator();
        drawIssueSection( circuit );
        EditorChrome::endSection();
    }

    void GimmickGraphPanel::drawNodeInspector( GimmickCircuitComponent& circuit )
    {
        const optional<uint32> nodeIndex = _model.findNodeIndex( _selectedNode );
        if ( nodeIndex.has_value() == false )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdPlugin", "ClickANodeToEditIt" ) );
            return;
        }
        const GimmickGraphNode node  = _model.getNodes()[*nodeIndex]; // 편집이 목록을 바꾸므로 사본으로 그린다
        const GimmickNodeKind* pKind = _model.findKind( node );
        EditorWidgets::drawSectionHeader( node._id.c_str(), node._kind.c_str() );

        ImGui::TextUnformatted( "Id" );
        string newID;
        if ( drawCommittedText( "gimmick.node.id", string( node._id.c_str() ), newID ) )
        {
            editRename( circuit, node._graphID, newID );
            return;
        }
        EditorSelfTestMarks::record( "gimmick.node.id" );

        if ( pKind != nullptr )
        {
            for ( const GimmickParamSpec& spec : pKind->_listParam )
            {
                ImGui::TextUnformatted( spec._name.c_str() );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "ParamDefault", TextArgumentList().addText( "hasDefault", spec._defaultText.empty() ? "no" : "yes" ).addText( "value", spec._defaultText ) ).c_str() );
                string       newValue;
                const string current = GimmickGraphModel::readParam( node._params, spec._name.c_str() );
                if ( drawCommittedText( ( string( "gimmick.param." ) + spec._name.c_str() ).c_str(), current, newValue ) )
                {
                    editSetParam( circuit, node._graphID, spec._name.c_str(), newValue );
                    return;
                }
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::record( GimmickGraphPanelInternal::makeMark( "gimmick.param.", spec._name.c_str() ).c_str() );
            }
        }

        ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "TargetObject" ) );
        GameObject* pTarget = node._target.isValid() ? findGameObject( node._target ) : nullptr;
        if ( node._target.isValid() == false )
            ImGui::TextDisabled( "(the object that owns this circuit)" );
        else if ( pTarget != nullptr )
            ImGui::Text( "%s", pTarget->getName().c_str() );
        else
        {
            StringBuilder<constant::buffer::kMax64> missing;
            missing.appendFormat( "(missing object #%#)", node._target.objectID() );
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", missing.c_str() );
        }
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "UseSelectedObject" ) ) )
        {
            // 회로(`findCircuit`)와 같은 선택에서 고른다 — 대상은 회로와 같은 씬에 있어야 한다.
            const EditorSelection* pSelection = editor::getFocusedSelection();
            vector<GameObject*>    listSelected;
            if ( pSelection != nullptr )
                pSelection->getSelectedObjects( listSelected );
            if ( listSelected.empty() == false && listSelected[0] != nullptr )
            {
                editSetTarget( circuit, node._graphID, listSelected[0]->getHandle() );
                return;
            }
            _statusText = "Select an object in the Hierarchy first";
        }
        EditorSelfTestMarks::record( "gimmick.target.use" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "ClearTarget" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "ClearTarget" ) ) && node._target.isValid() )
        {
            editSetTarget( circuit, node._graphID, GameObjectHandle{} );
            return;
        }
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "DeleteNode" ) ) )
        {
            EditorGraphRemoval removal;
            removal._listNodeID.push_back( node._graphID );
            editRemove( circuit, removal );
            return;
        }
        EditorSelfTestMarks::record( "gimmick.node.delete" );
    }

    void GimmickGraphPanel::drawWireSection( GimmickCircuitComponent& circuit )
    {
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "Wires" ) );

        // 캔버스에서 핀을 끌어 이어도 되고, 아래 폼으로 이을 수도 있다
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "From" ), "From##gimmickWireFrom" ), _formFrom._label.empty() ? "(output)" : _formFrom._label.c_str() ) )
        {
            for ( const GimmickGraphNode& node : _model.getNodes() )
            {
                const GimmickNodeKind* pKind = _model.findKind( node );
                if ( pKind == nullptr )
                    continue;
                for ( const hashed_string& port : pKind->_listOutput )
                {
                    const string label = string( node._id.c_str() ) + "." + port.c_str();
                    if ( ImGui::Selectable( label.c_str(), label == _formFrom._label ) )
                        _formFrom = PortChoice{ label, port.c_str(), node._graphID };
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::record( GimmickGraphPanelInternal::makeMark( "gimmick.wire.from.", label ).c_str() );
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "gimmick.wire.from" );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdPluginWidget", "To" ), "To##gimmickWireTo" ), _formTo._label.empty() ? "(input)" : _formTo._label.c_str() ) )
        {
            for ( const GimmickGraphNode& node : _model.getNodes() )
            {
                const GimmickNodeKind* pKind = _model.findKind( node );
                if ( pKind == nullptr )
                    continue;
                for ( const hashed_string& port : pKind->_listInput )
                {
                    const string label = string( node._id.c_str() ) + "." + port.c_str();
                    if ( ImGui::Selectable( label.c_str(), label == _formTo._label ) )
                        _formTo = PortChoice{ label, port.c_str(), node._graphID };
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::record( GimmickGraphPanelInternal::makeMark( "gimmick.wire.to.", label ).c_str() );
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( "gimmick.wire.to" );
        ImGui::BeginDisabled( _formFrom._label.empty() || _formTo._label.empty() );
        if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Connect" ) ) )
        {
            editConnect( circuit, _formFrom._nodeGraphID, _formFrom._port, _formTo._nodeGraphID, _formTo._port );
            ImGui::EndDisabled();
            return;
        }
        EditorSelfTestMarks::record( "gimmick.wire.connect" );
        ImGui::EndDisabled();

        int32 removeWireID = 0;
        for ( const GimmickGraphWire& wire : _model.getWires() )
        {
            // 모델이 노드를 지울 때 배선도 지우므로 두 끝은 늘 있다.
            const uint32 fromIndex = _model.findNodeIndex( wire._fromNode ).value();
            const uint32 toIndex   = _model.findNodeIndex( wire._toNode ).value();
            ImGui::PushID( wire._graphID );
            ImGui::Text( "%s.%s -> %s.%s", _model.getNodes()[fromIndex]._id.c_str(), wire._fromPort.c_str(), _model.getNodes()[toIndex]._id.c_str(), wire._toPort.c_str() );
            bool bInvert = wire._bInvert;
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdPlugin", "Invert" ) ) );
            if ( ImGui::Checkbox( SW_LOCKEY( "EdPlugin", "Invert" ), &bInvert ) )
            {
                ImGui::PopID();
                editSetInvert( circuit, wire._graphID, bInvert );
                return;
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "Remove" ) ) )
                removeWireID = wire._graphID;
            ImGui::PopID();
        }
        if ( removeWireID != 0 )
        {
            EditorGraphRemoval removal;
            removal._listLinkID.push_back( removeWireID );
            editRemove( circuit, removal );
        }
    }

    void GimmickGraphPanel::drawIssueSection( GimmickCircuitComponent& circuit )
    {
        EditorWidgets::drawSectionHeader( SW_LOCKEY( "EdPluginWidget", "Problems" ) );
        if ( _listIssue.empty() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "The circuit is valid" );
            return;
        }
        for ( const GimmickGraphIssue& issue : _listIssue )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", issue._message.c_str() );
        }
        // 가리키는 노드가 없는 배선은 저장 내용에 남아 있다 - 지우면 저장 내용이 바뀌므로 편집(Undo 가능)으로 다룬다
        if ( _model.getOrphanWires().empty() == false && ImGui::SmallButton( SW_LOCKEY( "EdPlugin", "RemoveBrokenWires" ) ) )
            editRemoveOrphanWires( circuit );
        EditorSelfTestMarks::record( "gimmick.wire.removeBroken" );
    }

    ObjectSnapshot GimmickGraphPanel::beginEdit( GimmickCircuitComponent& circuit ) const
    {
        return EditorTransaction::captureSnapshot( circuit.getOwner() );
    }

    void GimmickGraphPanel::commitEdit( GimmickCircuitComponent& circuit, const ObjectSnapshot& before, const utf8* pLabel )
    {
        if ( GimmickGraphBridge::apply( circuit, _model ) == false )
        {
            _statusText = "Could not write the circuit properties";
            return;
        }
        GameObject* pOwner = circuit.getOwner();
        if ( pOwner != nullptr )
            EditorTransaction::recordModify( pOwner, before, EditorTransaction::captureSnapshot( pOwner ), pLabel );
        _signature  = _model.computeSignature();
        _statusText = pLabel;
        refreshIssues();
        SW_LOG_INFO_ID( "GimmickGraph.Edited", "Gimmick graph %#", pLabel );
    }

    void GimmickGraphPanel::handleEditCommands( GimmickCircuitComponent& circuit )
    {
        vector<int32>                listNode;
        float2                       pastePosition{};
        const EditorGraphEditCommand command = _nodeGraph.queryEditCommand( listNode, pastePosition );
        if ( command == EditorGraphEditCommand::None )
            return;
        if ( command == EditorGraphEditCommand::Duplicate )
        {
            editDuplicate( circuit, listNode, float2{ EditorNodeGraph::kDuplicateOffset, EditorNodeGraph::kDuplicateOffset } );
            return;
        }
        if ( command == EditorGraphEditCommand::Paste )
        {
            string text;
            if ( EditorNodeGraph::readClipboard( GimmickGraphPanelInternal::kClipboardKind, text ) == false )
                return;
            // 복사한 노드 번호를 읽어 지금 회로에 남은 것만 붙인다 — 조각의 왼쪽 위가 마우스 자리에 온다.
            vector<int32> listID;
            vector<int32> listCopied;
            bool          bCorner = false;
            float2        corner{};
            if ( StringUtil::parseIntList( text, ",", listID ) == false )
                return; // 이 패널이 쓴 번호 목록이다 — 읽지 못하면 붙일 것이 없다
            for ( const int32 graphID : listID )
            {
                const optional<uint32> index = _model.findNodeIndex( graphID );
                if ( index.has_value() == false )
                    continue; // 복사한 뒤 지운 노드
                const float2& position = _model.getNodes()[*index]._position;
                corner                 = bCorner ? float2{ MathUtil::min( corner._x, position._x ), MathUtil::min( corner._y, position._y ) } : position;
                bCorner                = true;
                listCopied.push_back( graphID );
            }
            if ( listCopied.empty() == false )
                editDuplicate( circuit, listCopied, pastePosition - corner );
            return;
        }
        string text;
        for ( const int32 graphID : listNode )
        {
            fixed_string<constant::buffer::kMax32> number;
            formatstring( number.data(), number.capacity(), text.empty() ? "%#" : ",%#", graphID );
            text += number.c_str();
        }
        EditorNodeGraph::storeClipboard( GimmickGraphPanelInternal::kClipboardKind, std::move( text ) );
        if ( command != EditorGraphEditCommand::Cut )
            return;
        EditorGraphRemoval removal;
        for ( const int32 graphID : listNode )
        {
            if ( EditorNodeGraph::isCommentNode( graphID ) )
                removal._listCommentID.push_back( graphID - EditorNodeGraph::kCommentNodeIDBase );
            else
                removal._listNodeID.push_back( graphID );
        }
        editRemove( circuit, removal );
    }

    void GimmickGraphPanel::editDuplicate( GimmickCircuitComponent& circuit, const vector<int32>& listNode, const float2& offset )
    {
        if ( listNode.empty() )
            return;
        const ObjectSnapshot before  = beginEdit( circuit );
        const vector<int32>  listNew = _model.duplicateNodes( listNode, offset );
        if ( listNew.empty() )
            return;
        _selectedNode = listNew.back();
        commitEdit( circuit, before, "Duplicate Gimmick Nodes" );
    }

    void GimmickGraphPanel::editAddNode( GimmickCircuitComponent& circuit, const hashed_string& kind, const float2* pPosition )
    {
        const ObjectSnapshot before  = beginEdit( circuit );
        const int32          graphID = _model.addNode( kind );
        if ( graphID == GimmickGraphModel::kNoGraphID )
            return;
        const optional<uint32> index = _model.findNodeIndex( graphID );
        if ( index.has_value() && pPosition != nullptr )
        {
            GimmickGraphNode& node = _model.getMutableNodes()[*index];
            node._position         = *pPosition;
            node._bPlaced          = false;
        }
        _selectedNode = graphID;
        commitEdit( circuit, before, "Add Gimmick Node" );
    }

    void GimmickGraphPanel::editRemove( GimmickCircuitComponent& circuit, const EditorGraphRemoval& removal )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        bool                 bChanged{ false };
        for ( const int32 wireID : removal._listLinkID )
        {
            bChanged = _model.removeWire( wireID ) || bChanged;
        }
        bChanged = EditorNodeGraph::removeComments( _model.getMutableComments(), removal._listCommentID ) || bChanged;
        for ( const int32 nodeID : removal._listNodeID )
        {
            bChanged = _model.removeNode( nodeID ) || bChanged;
            if ( _selectedNode == nodeID )
                _selectedNode = 0;
        }
        if ( bChanged )
            commitEdit( circuit, before, removal._listNodeID.empty() && removal._listCommentID.empty() ? "Remove Gimmick Wire" : "Remove Gimmick Node" );
    }

    void GimmickGraphPanel::editRemoveOrphanWires( GimmickCircuitComponent& circuit )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( _model.removeOrphanWires() > 0 )
            commitEdit( circuit, before, "Remove Broken Gimmick Wires" );
    }

    void GimmickGraphPanel::editConnect( GimmickCircuitComponent& circuit, int32 fromNode, string_view fromPort, int32 toNode, string_view toPort )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( const expected<int32> wireID = _model.addWire( fromNode, fromPort, toNode, toPort ); wireID.has_value() == false )
        {
            _statusText = EditorWidgets::makeStatusText( wireID.error() );
            return;
        }
        commitEdit( circuit, before, "Add Gimmick Wire" );
    }

    void GimmickGraphPanel::editRename( GimmickCircuitComponent& circuit, int32 nodeID, const string& newID )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( const expected<void> renamed = _model.renameNode( nodeID, hashed_string( newID ) ); renamed.has_value() == false )
        {
            _statusText = EditorWidgets::makeStatusText( renamed.error() );
            return;
        }
        commitEdit( circuit, before, "Rename Gimmick Node" );
    }

    void GimmickGraphPanel::editSetParam( GimmickCircuitComponent& circuit, int32 nodeID, string_view name, string_view value )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( _model.setNodeParam( nodeID, name, value ) == false )
            return;
        commitEdit( circuit, before, "Edit Gimmick Parameter" );
    }

    void GimmickGraphPanel::editSetTarget( GimmickCircuitComponent& circuit, int32 nodeID, GameObjectHandle target )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( _model.setNodeTarget( nodeID, target ) == false )
            return;
        commitEdit( circuit, before, "Set Gimmick Target" );
    }

    void GimmickGraphPanel::editSetInvert( GimmickCircuitComponent& circuit, int32 wireID, bool bInvert )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        if ( _model.setWireInvert( wireID, bInvert ) == false )
            return;
        commitEdit( circuit, before, "Invert Gimmick Wire" );
    }

    SW_EDITOR_PANEL( GimmickGraphPanel, GimmickGraphPanel::kPanelID, EditorPanelCategory::Custom, 9300 );
} // namespace sw::editor
