#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/Gimmick/GimmickGraphPanel.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Plugin/Editor/Base/Gimmick/GimmickGraphBridge.h"

#include <imgui.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        struct GimmickGraphPanelInternal
        {
            static constexpr const utf8* kCanvasID           = "GimmickGraphCanvas";
            static constexpr const utf8* kCanvasSettingsFile = "GimmickGraphCanvas.json";
            static constexpr float32     kSidePanelWidth     = 340.0f; ///< 96 DPI 기준입니다
            static constexpr float32     kNewNodeRowHeight   = 150.0f; ///< 도구 모음으로 더한 노드를 기존 노드 아래에 놓는 간격(캔버스 좌표)

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
                        return ImVec4{ 0.45f, 0.85f, 0.55f, 1.0f };
                    case GimmickNodeCategory::Operator:
                        return ImVec4{ 0.55f, 0.75f, 1.0f, 1.0f };
                    case GimmickNodeCategory::Actuator:
                        return ImVec4{ 1.0f, 0.70f, 0.35f, 1.0f };
                }
                return ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }

            static const utf8* getCategoryName( GimmickNodeCategory category )
            {
                switch ( category )
                {
                    case GimmickNodeCategory::Sensor:
                        return "Sensor";
                    case GimmickNodeCategory::Operator:
                        return "Operator";
                    case GimmickNodeCategory::Actuator:
                        return "Actuator";
                }
                return "";
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
        , _model{ GimmickNodeRegistry::getDefault() }
        , _nodeGraph{}
        , _listIssue{}
        , _listSavedPosition{}
        , _ownerHandle{}
        , _formFrom{}
        , _formTo{}
        , _editText{}
        , _statusText{}
        , _signature{ 0 }
        , _editID{ 0 }
        , _selectedNode{ 0 }
    {
    }

    void GimmickGraphPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
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
        for ( GimmickGraphNode& node : _model.getMutableNodes() )
        {
            for ( const SavedPosition& saved : _listSavedPosition )
            {
                if ( saved._id == node._id.c_str() )
                {
                    node._position = saved._position;
                    break;
                }
            }
            node._bPlaced = false;
        }
        _signature = _model.computeSignature();
        if ( _model.findNodeIndex( _selectedNode ) < 0 )
            _selectedNode = 0;
        _formFrom = PortChoice{};
        _formTo   = PortChoice{};
        refreshIssues();
    }

    void GimmickGraphPanel::refreshIssues()
    {
        _model.collectIssues( _listIssue );
    }

    void GimmickGraphPanel::rememberPositions()
    {
        for ( const GimmickGraphNode& node : _model.getNodes() )
        {
            if ( node._bPlaced == false )
                continue;
            bool bFound = false;
            for ( SavedPosition& saved : _listSavedPosition )
            {
                if ( saved._id == node._id.c_str() )
                {
                    saved._position = node._position;
                    bFound          = true;
                    break;
                }
            }
            if ( bFound == false )
            {
                SavedPosition saved;
                saved._id       = node._id.c_str();
                saved._position = node._position;
                _listSavedPosition.push_back( std::move( saved ) );
            }
        }
    }

    void GimmickGraphPanel::drawContent()
    {
        GimmickCircuitComponent* pCircuit = findCircuit();
        if ( pCircuit == nullptr || pCircuit->getOwner() == nullptr )
        {
            _ownerHandle = GameObjectHandle{};
            _model.clear();
            _listIssue.clear();
            EditorWidgets::drawEmptyHint( "Select an object that has a Gimmick Circuit component" );
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
        if ( ImGui::Button( "Add node" ) )
            EditorInputGUI::openPopup( "##gimmickAddNode" );
        EditorSelfTestMarks::note( "gimmick.addnode" );
        if ( ImGui::BeginPopup( "##gimmickAddNode" ) )
        {
            const GimmickNodeCategory arrCategory[] = { GimmickNodeCategory::Sensor, GimmickNodeCategory::Operator, GimmickNodeCategory::Actuator };
            for ( const GimmickNodeCategory category : arrCategory )
            {
                ImGui::TextDisabled( "%s", GimmickGraphPanelInternal::getCategoryName( category ) );
                for ( const GimmickNodeKind& kind : GimmickNodeRegistry::getDefault().getKinds() )
                {
                    if ( kind._category != category )
                        continue;
                    if ( ImGui::Selectable( kind._name.c_str() ) )
                    {
                        float32 lowestY = -GimmickGraphPanelInternal::kNewNodeRowHeight;
                        for ( const GimmickGraphNode& node : _model.getNodes() )
                        {
                            lowestY = node._position._y > lowestY ? node._position._y : lowestY;
                        }
                        const float2 position{ 0.0f, lowestY + GimmickGraphPanelInternal::kNewNodeRowHeight };
                        editAddNode( circuit, kind._name, &position );
                    }
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::note( GimmickGraphPanelInternal::makeMark( "gimmick.kind.", kind._name.c_str() ).c_str() );
                }
            }
            ImGui::EndPopup();
        }

        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Auto layout" ) );
        if ( ImGui::Button( "Auto layout" ) )
            _model.autoLayout();
        EditorSelfTestMarks::note( "gimmick.layout" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Zoom fit" ) );
        if ( ImGui::Button( "Zoom fit" ) )
            _nodeGraph.requestContentFit();
        ImGui::SameLine();
        ImGui::TextDisabled( "%s - %d nodes, %d wires, %d problems", circuit.getOwner()->getName().c_str(), static_cast<int32>( _model.getNodes().size() ),
                             static_cast<int32>( _model.getWires().size() + _model.getOrphanWires().size() ), static_cast<int32>( _listIssue.size() ) );
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
        if ( _nodeGraph.beginCanvas( GimmickGraphPanelInternal::kCanvasID, GimmickGraphPanelInternal::kCanvasSettingsFile ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( "The node editor could not be created" );
            EditorChrome::endSection();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );

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
            const int32 fromIndex = _model.findNodeIndex( wire._fromNode );
            const int32 toIndex   = _model.findNodeIndex( wire._toNode );
            if ( fromIndex < 0 || toIndex < 0 )
                continue;
            const GimmickNodeKind* pFromKind = _model.findKind( _model.getNodes()[static_cast<size_t>( fromIndex )] );
            const GimmickNodeKind* pToKind   = _model.findKind( _model.getNodes()[static_cast<size_t>( toIndex )] );
            if ( pFromKind == nullptr || pToKind == nullptr )
                continue;
            const int32 outPort = pFromKind->findOutput( hashed_string( wire._fromPort ) );
            const int32 inPort  = pToKind->findInput( hashed_string( wire._toPort ) );
            if ( outPort < 0 || inPort < 0 )
                continue;
            const ImVec4 color = wire._bInvert ? ImVec4{ 1.0f, 0.45f, 0.4f, 1.0f } : ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f };
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

        ed::NodeId  arrSelected[1];
        const int32 selectedCount = ed::GetSelectedNodes( arrSelected, 1 );
        if ( selectedCount > 0 )
            _selectedNode = static_cast<int32>( arrSelected[0].Get() );

        _nodeGraph.applyContentFitIfNeeded();
        for ( GimmickGraphNode& node : _model.getMutableNodes() )
        {
            if ( node._bPlaced == false )
                continue;
            const ImVec2 position = ed::GetNodePosition( toNodeID( node._graphID ) );
            node._position        = float2{ position.x, position.y };
        }
        rememberPositions();

        EditorNodeGraph::noteDrawnNodeCount( static_cast<uint32>( _model.getNodes().size() ) );
        _nodeGraph.endCanvas();
        ed::SetCurrentEditor( nullptr );
        EditorChrome::endSection();
    }

    void GimmickGraphPanel::drawNode( const GimmickGraphNode& node )
    {
        const GimmickNodeKind* pKind = _model.findKind( node );
        ed::BeginNode( toNodeID( node._graphID ) );
        ImGui::TextColored( pKind != nullptr ? GimmickGraphPanelInternal::getCategoryColor( pKind->_category ) : ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", node._id.c_str() );
        ImGui::SameLine();
        ImGui::TextDisabled( "%s", node._kind.c_str() );
        if ( pKind == nullptr )
        {
            ImGui::TextColored( ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "Unknown node kind" );
        }
        else
        {
            ImGui::BeginGroup();
            for ( size_t portIndex = 0; portIndex < pKind->_listInput.size(); ++portIndex )
            {
                const int32 pinID = GimmickGraphPanelInternal::makeInputPin( node._graphID, static_cast<int32>( portIndex ) );
                ed::BeginPin( toPinID( pinID ), ed::PinKind::Input );
                _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
                ImGui::SameLine();
                ImGui::TextUnformatted( pKind->_listInput[portIndex].c_str() );
                ed::EndPin();
            }
            ImGui::EndGroup();
            ImGui::SameLine();
            ImGui::BeginGroup();
            for ( size_t portIndex = 0; portIndex < pKind->_listOutput.size(); ++portIndex )
            {
                const int32 pinID = GimmickGraphPanelInternal::makeOutputPin( node._graphID, static_cast<int32>( portIndex ) );
                ed::BeginPin( toPinID( pinID ), ed::PinKind::Output );
                ImGui::TextUnformatted( pKind->_listOutput[portIndex].c_str() );
                ImGui::SameLine();
                _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
                ed::EndPin();
            }
            ImGui::EndGroup();
        }
        ed::EndNode();
    }

    bool GimmickGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const int32 nodeIndex = _model.findNodeIndex( GimmickGraphPanelInternal::decodeNode( pinID ) );
        if ( nodeIndex < 0 )
            return false;
        const GimmickNodeKind* pKind = _model.findKind( _model.getNodes()[static_cast<size_t>( nodeIndex )] );
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
            const int32            nodeIndex = _model.findNodeIndex( nodeID );
            const GimmickNodeKind* pKind     = nodeIndex >= 0 ? _model.findKind( _model.getNodes()[static_cast<size_t>( nodeIndex )] ) : nullptr;
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
            const int32            fromIndex = _model.findNodeIndex( fromNode );
            const int32            toIndex   = _model.findNodeIndex( toNode );
            const GimmickNodeKind* pFromKind = fromIndex >= 0 ? _model.findKind( _model.getNodes()[static_cast<size_t>( fromIndex )] ) : nullptr;
            const GimmickNodeKind* pToKind   = toIndex >= 0 ? _model.findKind( _model.getNodes()[static_cast<size_t>( toIndex )] ) : nullptr;
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
        const vector<GimmickNodeKind>& listRegistryKind = GimmickNodeRegistry::getDefault().getKinds();
        for ( size_t kindIndex = 0; kindIndex < listRegistryKind.size(); ++kindIndex )
        {
            listKind.push_back( EditorGraphNodeKind{ listRegistryKind[kindIndex]._name.c_str(), GimmickGraphPanelInternal::getCategoryName( listRegistryKind[kindIndex]._category ),
                                                     static_cast<uint32>( kindIndex ) } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) && kindID < listRegistryKind.size() )
            editAddNode( circuit, listRegistryKind[kindID]._name, &canvasPosition );

        // 삭제: 노드를 지우면 그 노드에 닿은 배선도 모델이 같이 지운다
        vector<int32> listRemovedNode;
        vector<int32> listRemovedWire;
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkID;
            while ( ed::QueryDeletedLink( &linkID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    listRemovedWire.push_back( static_cast<int32>( linkID.Get() ) );
            }
            ed::NodeId nodeID;
            while ( ed::QueryDeletedNode( &nodeID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    listRemovedNode.push_back( static_cast<int32>( nodeID.Get() ) );
            }
            ed::EndDelete();
        }
        if ( listRemovedNode.empty() == false || listRemovedWire.empty() == false )
            editRemove( circuit, listRemovedNode, listRemovedWire );
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
        const int32 nodeIndex = _model.findNodeIndex( _selectedNode );
        if ( nodeIndex < 0 )
        {
            EditorWidgets::drawEmptyHint( "Click a node to edit it" );
            return;
        }
        const GimmickGraphNode node  = _model.getNodes()[static_cast<size_t>( nodeIndex )]; // 편집이 목록을 바꾸므로 사본으로 그린다
        const GimmickNodeKind* pKind = _model.findKind( node );
        EditorWidgets::drawSectionHeader( node._id.c_str(), node._kind.c_str() );

        ImGui::TextUnformatted( "Id" );
        string newID;
        if ( drawCommittedText( "gimmick.node.id", string( node._id.c_str() ), newID ) )
        {
            editRename( circuit, node._graphID, newID );
            return;
        }
        EditorSelfTestMarks::note( "gimmick.node.id" );

        if ( pKind != nullptr )
        {
            for ( const GimmickParamSpec& spec : pKind->_listParam )
            {
                ImGui::TextUnformatted( spec._name.c_str() );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Default: %s", spec._defaultText.empty() ? "(none)" : spec._defaultText.c_str() );
                string       newValue;
                const string current = GimmickGraphModel::readParam( node._params, spec._name.c_str() );
                if ( drawCommittedText( ( string( "gimmick.param." ) + spec._name.c_str() ).c_str(), current, newValue ) )
                {
                    editSetParam( circuit, node._graphID, spec._name.c_str(), newValue );
                    return;
                }
                if ( EditorSelfTestMarks::isEnabled() )
                    EditorSelfTestMarks::note( GimmickGraphPanelInternal::makeMark( "gimmick.param.", spec._name.c_str() ).c_str() );
            }
        }

        ImGui::TextUnformatted( "Target object" );
        GameObject* pTarget = node._target.isValid() ? findGameObject( node._target ) : nullptr;
        if ( node._target.isValid() == false )
            ImGui::TextDisabled( "(the object that owns this circuit)" );
        else if ( pTarget != nullptr )
            ImGui::Text( "%s", pTarget->getName().c_str() );
        else
        {
            StringBuilder<constant::kMaxBuffer64> missing;
            missing.appendFormat( "(missing object #%#)", node._target.objectID() );
            ImGui::TextColored( ImVec4{ 1.0f, 0.45f, 0.4f, 1.0f }, "%s", missing.c_str() );
        }
        if ( ImGui::Button( "Use selected object" ) )
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
        EditorSelfTestMarks::note( "gimmick.target.use" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Clear target" ) );
        if ( ImGui::Button( "Clear target" ) && node._target.isValid() )
        {
            editSetTarget( circuit, node._graphID, GameObjectHandle{} );
            return;
        }
        if ( ImGui::Button( "Delete node" ) )
        {
            editRemove( circuit, vector<int32>{ node._graphID }, vector<int32>{} );
            return;
        }
        EditorSelfTestMarks::note( "gimmick.node.delete" );
    }

    void GimmickGraphPanel::drawWireSection( GimmickCircuitComponent& circuit )
    {
        EditorWidgets::drawSectionHeader( "Wires" );

        // 캔버스에서 핀을 끌어 이어도 되고, 아래 폼으로 이을 수도 있다
        if ( ImGui::BeginCombo( "From##gimmickWireFrom", _formFrom._label.empty() ? "(output)" : _formFrom._label.c_str() ) )
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
                        EditorSelfTestMarks::note( GimmickGraphPanelInternal::makeMark( "gimmick.wire.from.", label ).c_str() );
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( "gimmick.wire.from" );
        if ( ImGui::BeginCombo( "To##gimmickWireTo", _formTo._label.empty() ? "(input)" : _formTo._label.c_str() ) )
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
                        EditorSelfTestMarks::note( GimmickGraphPanelInternal::makeMark( "gimmick.wire.to.", label ).c_str() );
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::note( "gimmick.wire.to" );
        ImGui::BeginDisabled( _formFrom._label.empty() || _formTo._label.empty() );
        if ( ImGui::Button( "Connect" ) )
        {
            editConnect( circuit, _formFrom._nodeGraphID, _formFrom._port, _formTo._nodeGraphID, _formTo._port );
            ImGui::EndDisabled();
            return;
        }
        EditorSelfTestMarks::note( "gimmick.wire.connect" );
        ImGui::EndDisabled();

        int32 removeWireID = 0;
        for ( const GimmickGraphWire& wire : _model.getWires() )
        {
            const int32 fromIndex = _model.findNodeIndex( wire._fromNode );
            const int32 toIndex   = _model.findNodeIndex( wire._toNode );
            if ( fromIndex < 0 || toIndex < 0 )
                continue;
            ImGui::PushID( wire._graphID );
            ImGui::Text( "%s.%s -> %s.%s", _model.getNodes()[static_cast<size_t>( fromIndex )]._id.c_str(), wire._fromPort.c_str(),
                         _model.getNodes()[static_cast<size_t>( toIndex )]._id.c_str(), wire._toPort.c_str() );
            bool bInvert = wire._bInvert;
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( "Invert" ) );
            if ( ImGui::Checkbox( "Invert", &bInvert ) )
            {
                ImGui::PopID();
                editSetInvert( circuit, wire._graphID, bInvert );
                return;
            }
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Remove" ) );
            if ( ImGui::SmallButton( "Remove" ) )
                removeWireID = wire._graphID;
            ImGui::PopID();
        }
        if ( removeWireID != 0 )
            editRemove( circuit, vector<int32>{}, vector<int32>{ removeWireID } );
    }

    void GimmickGraphPanel::drawIssueSection( GimmickCircuitComponent& circuit )
    {
        EditorWidgets::drawSectionHeader( "Problems" );
        if ( _listIssue.empty() )
        {
            ImGui::TextColored( ImVec4{ 0.45f, 0.85f, 0.45f, 1.0f }, "The circuit is valid" );
            return;
        }
        for ( const GimmickGraphIssue& issue : _listIssue )
        {
            ImGui::TextColored( ImVec4{ 0.95f, 0.45f, 0.35f, 1.0f }, "%s", issue._message.c_str() );
        }
        // 가리키는 노드가 없는 배선은 저장 내용에 남아 있다 - 지우면 저장 내용이 바뀌므로 편집(Undo 가능)으로 다룬다
        if ( _model.getOrphanWires().empty() == false && ImGui::SmallButton( "Remove broken wires" ) )
            editRemoveOrphanWires( circuit );
        EditorSelfTestMarks::note( "gimmick.wire.removeBroken" );
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
        SW_LOG_INFO( "Gimmick graph %#", pLabel );
    }

    void GimmickGraphPanel::editAddNode( GimmickCircuitComponent& circuit, const hashed_string& kind, const float2* pPosition )
    {
        const ObjectSnapshot before  = beginEdit( circuit );
        const int32          graphID = _model.addNode( kind );
        if ( graphID == GimmickGraphModel::kNoGraphID )
            return;
        const int32 index = _model.findNodeIndex( graphID );
        if ( index >= 0 && pPosition != nullptr )
        {
            GimmickGraphNode& node = _model.getMutableNodes()[static_cast<size_t>( index )];
            node._position         = *pPosition;
            node._bPlaced          = false;
        }
        _selectedNode = graphID;
        commitEdit( circuit, before, "Add Gimmick Node" );
    }

    void GimmickGraphPanel::editRemove( GimmickCircuitComponent& circuit, const vector<int32>& listNode, const vector<int32>& listWire )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        bool                 bChanged{ false };
        for ( const int32 wireID : listWire )
        {
            bChanged = _model.removeWire( wireID ) || bChanged;
        }
        for ( const int32 nodeID : listNode )
        {
            bChanged = _model.removeNode( nodeID ) || bChanged;
            if ( _selectedNode == nodeID )
                _selectedNode = 0;
        }
        if ( bChanged )
            commitEdit( circuit, before, listNode.empty() ? "Remove Gimmick Wire" : "Remove Gimmick Node" );
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
        int32                wireID = 0;
        string               error;
        if ( _model.addWire( fromNode, fromPort, toNode, toPort, wireID, error ) == false )
        {
            _statusText = error;
            return;
        }
        commitEdit( circuit, before, "Add Gimmick Wire" );
    }

    void GimmickGraphPanel::editRename( GimmickCircuitComponent& circuit, int32 nodeID, const string& newID )
    {
        const ObjectSnapshot before = beginEdit( circuit );
        string               error;
        if ( _model.renameNode( nodeID, hashed_string( newID ), error ) == false )
        {
            _statusText = error;
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
