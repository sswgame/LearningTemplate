#include "pch.h"

#include "Editor/Panels/Dialogue/DialogueGraphPanel.h"

#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"
#include "Editor/Panels/Dialogue/Model/DialogueGraphEdit.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 핀 번호의 원본은 `DialogueGraphAsset` 입니다. 여기서는 이름만 짧게 가져다 씁니다.
         * @details 오프셋 상수와 인코딩의 복사본을 여기 두지 않는다. 링크는 디스크에 저장되므로, 한쪽만 바뀌면
         *          대화가 조용히 엉뚱한 분기로 간다.
         */
        struct DialogueGraphPanelInternal
        {
            static constexpr Color kSelectedNodeText{ 0.2f, 0.8f, 1.0f, 1.0f };
            static constexpr Color kTrueBranchText{ 0.3f, 1.0f, 0.4f, 1.0f };
            static constexpr Color kFalseBranchText{ 1.0f, 0.4f, 0.4f, 1.0f };

            static constexpr int32 kPinInputOffset  = DialogueGraphAsset::kPinOffsetIn;
            static constexpr int32 kPinOutputOffset = DialogueGraphAsset::kPinOffsetOut;
            static constexpr int32 kPinTrueOffset   = DialogueGraphAsset::kPinOffsetTrue;
            static constexpr int32 kPinFalseOffset  = DialogueGraphAsset::kPinOffsetFalse;

            static int32 pinIn( int32 nodeID )
            {
                return DialogueGraphAsset::encodePin( nodeID, kPinInputOffset );
            }

            static int32 pinOut( int32 nodeID )
            {
                return DialogueGraphAsset::encodePin( nodeID, kPinOutputOffset );
            }

            static int32 pinBranchTrue( int32 nodeID )
            {
                return DialogueGraphAsset::encodePin( nodeID, kPinTrueOffset );
            }

            static int32 pinBranchFalse( int32 nodeID )
            {
                return DialogueGraphAsset::encodePin( nodeID, kPinFalseOffset );
            }

            static int32 pinChoice( int32 nodeID, int32 choiceIndex )
            {
                return DialogueGraphAsset::encodeChoicePin( nodeID, choiceIndex );
            }

            /** @brief 링크 양 끝의 노드 id 입니다(핀 번호에서 꺼낸다 — 해석의 원본은 `DialogueGraphAsset`). 복사 · 붙여 넣기가 쓴다. */
            static void linkNodes( const DialogueAssetLink& link, int32& outFromNode, int32& outToNode )
            {
                outFromNode = DialogueGraphAsset::decodePinNodeID( link._fromPin );
                outToNode   = DialogueGraphAsset::decodePinNodeID( link._toPin );
            }

            /** @brief 링크 양 끝을 새 노드 id 로 바꿉니다. 핀 오프셋(어느 출력 · 선택지인지)은 그대로 둔다. */
            static void remapLink( DialogueAssetLink& link, int32 fromNode, int32 toNode )
            {
                link._fromPin = DialogueGraphAsset::encodePin( fromNode, DialogueGraphAsset::decodePinOffset( link._fromPin ) );
                link._toPin   = DialogueGraphAsset::encodePin( toNode, DialogueGraphAsset::decodePinOffset( link._toPin ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( DialogueGraphPanel, DialogueGraphPanel::kPanelID, EditorPanelCategory::Tool, 1200 );

    DialogueGraphPanel::DialogueGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetType{ DialogueGraphAsset::kAssetType }, "Move Dialogue Nodes", "dialogue-graph-layout", &ed::SetCurrentEditor }
        , _selectedNodeID{ 0 }
        , _preview{}
        , _actionGrid{}
        , _actionProperties{}
        , _actionPropertiesText{}
        , _actionError{}
        , _listActionName{}
        , _pActionEntry{ nullptr }
        , _pActionTargetText{ nullptr }
    {
        _nodeGraph.setPinTypeColors( { SW_THEME_COLOR( "Graph", "FlowPin" ) } ); // 흐름 핀 하나
    }

    void DialogueGraphPanel::shutdown()
    {
        _nodeGraph.shutdown();
    }

    void DialogueGraphPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        _preview.update( _graph, ImGui::GetIO().DeltaTime );

        drawGraphToolbar();

        // 노드를 선택하면 오른쪽에 인스펙터가 붙으므로 캔버스가 그만큼 좁아진다.
        const float32 availWidth  = ImGui::GetContentRegionAvail().x;
        const float32 canvasWidth = _selectedNodeID > 0 ? availWidth * 0.72f : availWidth;
        drawGraphCanvas( canvasWidth );

        if ( _selectedNodeID > 0 )
        {
            ImGui::SameLine();
            drawSelectedNodeInspector();
        }
    }

    void DialogueGraphPanel::drawGraphToolbar()
    {
        if ( EditorChrome::beginToolbar( "##DialogueToolbar" ) )
        {
            // 노드 종류마다 단추 하나씩이던 줄을 메뉴 하나로(언리얼 · 유니티 그래프는 오른쪽 클릭 검색과 추가 메뉴 하나다 — 캔버스 검색도 같은 목록).
            if ( ImGui::Button( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdCommon", "GraphAddNode" ), "AddNode" ) ) )
                EditorInputGUI::openPopup( "##dialogueAddNode" );
            EditorSelfTestMarks::record( "dialogueGraph.addMenu" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "AddNodeTip" ) );
            if ( ImGui::BeginPopup( "##dialogueAddNode" ) )
            {
                for ( const DialogueNodeInfo& info : kArrDialogueNodeInfo )
                {
                    if ( info._bAddable == false )
                        continue;
                    if ( ImGui::MenuItem( info._pName ) )
                    {
                        addNode( info._type );
                        notifyDocumentEdited( "Add Dialogue Node" );
                    }
                    fixed_string<constant::buffer::kMax64> markKey;
                    formatstring( markKey.data(), markKey.capacity(), "dialogueGraph.add.%#", info._pName );
                    EditorSelfTestMarks::record( markKey.c_str() );
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            EditorWidgets::drawToolbarSeparator();
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Save" ) ) )
                (void)saveGraphData(); // 실패는 저장 커맨드가 알린다
            EditorSelfTestMarks::record( "dialogueGraph.save" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "SaveTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reload" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Reload" ) ) )
                reloadDocument();
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "ReloadTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "ResetDefault" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdText", "ResetDefault" ) ) )
            {
                ensureDefaults();
                _nodeGraph.requestContentFit();
                notifyDocumentEdited( "Reset Dialogue Graph" );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "ResetDefaultTip" ) );
            drawPreviewToolbar();

            ImGui::SameLine();
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "NodesLinks2", TextArgumentList().addInteger( "nodeCount", static_cast<int64>( _graph._listNode.size() ) ).addInteger( "linkCount", static_cast<int64>( _graph._listLink.size() ) ) ).c_str() );
        }
        EditorChrome::endToolbar();
    }

    void DialogueGraphPanel::drawGraphCanvas( float32 canvasWidth )
    {
        editor::EditorSectionDesc canvasDesc{};
        canvasDesc._pID       = "DialogueCanvasRegion";
        canvasDesc._kind      = editor::EditorSectionKind::Child;
        canvasDesc._childSize = float2{ canvasWidth, 0.0f };
        canvasDesc._flags     = editor::EditorSectionFlags::NoScrollbar | editor::EditorSectionFlags::NoScrollWithMouse;
        EditorChrome::beginSection( canvasDesc );

        if ( beginGraphCanvas( "DialogueGraphCanvas", EditorPaths::kDialogueGraphCanvasFileName, "dialogueGraph" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorDialogueGraph", "ContextCreateFailed" ) );
            EditorChrome::endSection();
            return;
        }

        drawGraphComments();
        drawGraphNodes();

        // 링크 렌더링
        for ( const DialogueLink& link : _graph._listLink )
        {
            ed::Link( toLinkID( link._id ), toPinID( link._fromPin ), toPinID( link._toPin ) );
        }

        updateNodeIssues();
        _nodeGraph.drawNodeIssues();
        handleCanvasInteractions();
        // 선택 노드 추적
        int32 selectedNodeID{ 0 };
        if ( _nodeGraph.findSelectedNode( selectedNodeID ) && EditorNodeGraph::isCommentNode( selectedNodeID ) == false ) // 주석 상자는 인스펙터가 없다
            _selectedNodeID = selectedNodeID;

        _nodeGraph.applyContentFitIfNeeded();
        cacheNodeLayout();

        endGraphCanvas();
        EditorChrome::endSection();
    }

    void DialogueGraphPanel::drawGraphNodes()
    {
        // 노드 렌더링
        for ( DialogueNode& node : _graph._listNode )
        {
            const ed::NodeId nodeID = toNodeID( node._id );
            _nodeGraph.beginNode( node._id );

            drawNodeBody( node );

            _nodeGraph.endNode();

            if ( _nodeGraph.needsContentFit() )
                ed::SetNodePosition( nodeID, ImVec2( node._position._x, node._position._y ) );
            applyNodePlacement( node._id );
        }
    }

    void DialogueGraphPanel::handleCanvasInteractions()
    {
        // 새 링크. 방향(나가는 핀에서 들어오는 핀)과 타입 판정, 거절 표시는 프레임워크(`EditorNodeGraph::queryNewLink`)가 한다.
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &DialogueGraphPanel::findGraphPin, this ), fromPin, toPin ) )
        {
            DialogueLink newLink{};
            newLink._id      = nextLinkID();
            newLink._fromPin = fromPin;
            newLink._toPin   = toPin;
            _graph._listLink.push_back( newLink );
            notifyDocumentEdited( "Link Dialogue Nodes" );
        }

        // 빈 곳을 오른쪽 클릭하면 노드 검색 팝업이 열린다. 고른 노드는 오른쪽 클릭한 위치에 놓는다.
        vector<EditorGraphNodeKind> listKind;
        for ( const DialogueNodeInfo& info : kArrDialogueNodeInfo )
        {
            if ( info._bAddable )
                listKind.push_back( EditorGraphNodeKind{ info._pName, "Dialogue", static_cast<uint32>( info._type ) } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) )
        {
            placeNodeOnNextDraw( addNode( static_cast<DialogueAssetNodeKind>( kindID ) ), canvasPosition );
            notifyDocumentEdited( "Add Dialogue Node" );
        }

        // 복사 · 잘라내기 · 붙여 넣기 · 복제(Ctrl+C · X · V · D). 잘라내기는 지우기 요청을 내므로 지우기 처리보다 먼저다.
        processCanvasEditCommands( &DialogueGraphPanelInternal::linkNodes, &DialogueGraphPanelInternal::remapLink, "Paste Dialogue Nodes" );

        // 삭제 처리. 링크가 노드에 닿는지는 핀 번호를 해석해서 판단한다. 핀을 해석하는 원본은 `DialogueGraphAsset` 이다.
        // 핀을 해석하는 기준(`kPinScale`)은 `decodePinNodeID` 한 곳에만 있다. 여기서 직접 해석하면
        // 기준이 바뀔 때 노드를 지워도 그 링크가 남는다.
        processCanvasDeletions(
            []( const DialogueLink& link, int32 nodeID )
        { return DialogueGraphAsset::decodePinNodeID( link._fromPin ) == nodeID || DialogueGraphAsset::decodePinNodeID( link._toPin ) == nodeID; },
            [this]( int32 nodeID )
        {
            if ( _selectedNodeID == nodeID )
                _selectedNodeID = 0;
        },
            "Delete Dialogue Link", "Delete Dialogue Node" );
    }

    void DialogueGraphPanel::drawSelectedNodeInspector()
    {
        editor::EditorSectionDesc inspectorDesc{};
        inspectorDesc._pID   = "DialogueNodeInspector";
        inspectorDesc._kind  = editor::EditorSectionKind::Child;
        inspectorDesc._flags = editor::EditorSectionFlags::Border;
        EditorChrome::beginSection( inspectorDesc );

        DialogueNode* pSelectedNode{ nullptr };
        for ( DialogueNode& node : _graph._listNode )
        {
            if ( node._id == _selectedNodeID )
            {
                pSelectedNode = &node;
                break;
            }
        }

        if ( pSelectedNode != nullptr )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( DialogueGraphPanelInternal::kSelectedNodeText ), "%s", SW_LOCKEYFORMAT( "EdFormat", "Node", TextArgumentList().addInteger( "id", static_cast<int64>( pSelectedNode->_id ) ).addText( "type", DialogueGraphAsset::nodeTypeName( pSelectedNode->_type ) ) ).c_str() );
            ImGui::Separator();

            const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( pSelectedNode->_type );
            if ( pInfo != nullptr )
                drawNodeProperties( *pSelectedNode, *pInfo );
            ImGui::Separator();
        }
        drawSpeakerList();

        EditorChrome::endSection();
    }

    void DialogueGraphPanel::drawSpeakerList()
    {
        ImGui::TextUnformatted( SW_LOCKEY( "EditorDialogueGraph", "Speakers" ) );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "SpeakersTip" ) );
        for ( size_t speakerIndex = 0; speakerIndex < _graph._listSpeaker.size(); ++speakerIndex )
        {
            DialogueSpeaker& speaker = _graph._listSpeaker[speakerIndex];
            ImGui::PushID( static_cast<int32>( speaker._id ) );
            EditorWidgets::drawTextField( "##SpeakerName", speaker._name );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Rename Dialogue Speaker", "dialogue-inspector" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "SpeakerNameTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "X" ) );
            if ( ImGui::Button( "X" ) )
            {
                DialogueGraphEdit::removeSpeaker( _graph, speaker._id );
                notifyDocumentEdited( "Remove Dialogue Speaker" );
                ImGui::PopID();
                break;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "RemoveSpeakerTip" ) );
            ImGui::PopID();
        }
        if ( ImGui::Button( SW_LOCKEY( "EditorDialogueGraph", "AddSpeaker" ) ) )
        {
            DialogueGraphEdit::addSpeaker( _graph, "Speaker" );
            notifyDocumentEdited( "Add Dialogue Speaker" );
        }
    }

    void DialogueGraphPanel::ensureDefaults()
    {
        DialogueGraphEdit::makeDefaultGraph( _graph );
    }

    ToolAssetLoadResult DialogueGraphPanel::loadDocument()
    {
        DialogueGraphAsset        data;
        const ToolAssetLoadResult result = DialogueGraphEdit::loadGraph( data, getLoadedAssetPath() );
        applyLoadedGraph( std::move( data ), result );
        _preview.resetForDocument( getLoadedAssetPath() );
        return result;
    }

    bool DialogueGraphPanel::saveGraphData()
    {
        DialogueGraphAsset data = captureGraphData();
        // 실패하면 아무것도 지우지 않는다. AnimationGraphPanel 쪽 주석을 참고한다.
        if ( DialogueGraphEdit::saveGraph( data, getLoadedAssetPath() ) == false )
            return false;

        clearDocumentDirty();
        syncDocumentUndoBaseline();
        return true;
    }

    bool DialogueGraphPanel::saveDocument()
    {
        return saveGraphData();
    }

    void DialogueGraphPanel::drawPreviewToolbar()
    {
        ImGui::SameLine();
        EditorWidgets::drawToolbarSeparator();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "PlayPreview" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "PlayPreview" ) ) )
            _preview.play( _graph );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "PlayPreviewTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "AdvancePreview" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "AdvancePreview" ) ) )
            _preview.step( _graph, DialogueStepInput{} );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "AdvancePreviewTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "StopPreview" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "StopPreview" ) ) )
            _preview.stop();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "StopPreviewTip" ) );
        const int32 previewNodeID = _preview.getNodeID();
        if ( previewNodeID <= 0 )
            return;

        ImGui::SameLine();
        ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "PreviewNode", TextArgumentList().addInteger( "previewNodeID", static_cast<int64>( previewNodeID ) ) ).c_str() );
        const DialogueAssetNode* pNode = _graph.findNode( previewNodeID );
        const DialogueNodeInfo*  pInfo = pNode != nullptr ? DialogueGraphAsset::findNodeInfo( pNode->_type ) : nullptr;
        if ( pInfo == nullptr )
            return;

        // 기다리는 노드만 사람이 선택한다. 선택한 값은 러너와 같은 `DialogueCursor::step` 의 입력이 된다.
        if ( pInfo->_flow == DialogueNodeFlow::WaitChoice )
        {
            for ( int32 choiceIndex = 0; choiceIndex < static_cast<int32>( pNode->_listChoice.size() ); ++choiceIndex )
            {
                fixed_string<constant::buffer::kMax64> label;
                formatstring( label.data(), label.capacity(), "Choice %#", choiceIndex );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( label.c_str() ) ); // 선택지 수는 노드가 정한다 — 좁은 패널에서 뒤 단추가 창 밖에 놓이지 않게 줄을 바꾼다
                if ( ImGui::SmallButton( label.c_str() ) )
                {
                    DialogueStepInput input{};
                    input._choiceIndex = choiceIndex;
                    _preview.step( _graph, input );
                }
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "PreviewChoiceTip" ) );
            }
        }
        if ( pInfo->_flow == DialogueNodeFlow::Condition )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "True" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "True" ) ) )
            {
                DialogueStepInput input{};
                input._bConditionMet = true;
                _preview.step( _graph, input );
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "PreviewTrueTip" ) );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "False" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "False" ) ) )
                _preview.step( _graph, DialogueStepInput{} );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "PreviewFalseTip" ) );
        }
    }

    int32 DialogueGraphPanel::addNode( DialogueAssetNodeKind type )
    {
        _selectedNodeID = nextNodeID();
        DialogueGraphEdit::addNode( _graph, type, _selectedNodeID );
        return _selectedNodeID;
    }

    bool DialogueGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const int32 nodeID = DialogueGraphAsset::decodePinNodeID( pinID );
        bool        bKnown = false;
        for ( const DialogueNode& node : _graph._listNode )
        {
            bKnown = bKnown || node._id == nodeID;
        }
        if ( bKnown == false )
            return false;
        outInfo._type   = 0;
        outInfo._bInput = DialogueGraphAsset::decodePinOffset( pinID ) == DialogueGraphPanelInternal::kPinInputOffset;
        return true;
    }

    bool DialogueGraphPanel::isPinLinked( int32 pinID ) const
    {
        for ( const DialogueLink& link : _graph._listLink )
        {
            if ( link._fromPin == pinID || link._toPin == pinID )
                return true;
        }
        return false;
    }

    void DialogueGraphPanel::updateNodeIssues()
    {
        vector<int32> listInputNode;
        for ( const DialogueNode& node : _graph._listNode )
        {
            const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
            if ( pInfo != nullptr && pInfo->_bHasInputPin )
                listInputNode.push_back( node._id );
        }
        vector<EditorGraphEdge> listEdge;
        listEdge.reserve( _graph._listLink.size() );
        for ( const DialogueLink& link : _graph._listLink )
        {
            listEdge.push_back( EditorGraphEdge{ DialogueGraphAsset::decodePinNodeID( link._fromPin ), DialogueGraphAsset::decodePinNodeID( link._toPin ) } );
        }
        vector<EditorGraphNodeIssue> listIssue;
        EditorNodeGraphRules::collectUnreachableNodes( listInputNode, listEdge, listIssue );
        _nodeGraph.setNodeIssues( std::move( listIssue ) );
    }

    void DialogueGraphPanel::drawNodeBody( const DialogueNode& node )
    {
        const DialogueNodeInfo* pInfo = DialogueGraphAsset::findNodeInfo( node._type );
        if ( pInfo == nullptr )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Unknown", TextArgumentList().addInteger( "type", static_cast<int64>( node._type ) ) ).c_str() );
            return;
        }

        const ImVec4           headerColor( pInfo->_color._x, pInfo->_color._y, pInfo->_color._z, pInfo->_color._w );
        const DialogueSpeaker* pSpeaker = pInfo->_bHasSpeaker ? _graph.findSpeaker( node._speaker ) : nullptr;
        if ( pInfo->_bHasSpeaker )
            ImGui::TextColored( headerColor, "[%s: %s]", pInfo->_pName, pSpeaker == nullptr ? SW_LOCKEY( "EditorDialogueGraph", "NoSpeaker" ) : pSpeaker->_name.c_str() );
        else
            ImGui::TextColored( headerColor, "[%s]", pInfo->_pName );

        if ( pInfo->_bHasInputPin )
            drawInputPin( node._id );

        switch ( pInfo->_output )
        {
            case DialogueNodeOutput::Next:
            {
                if ( pInfo->_bHasInputPin )
                    ImGui::SameLine();
                drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Next ->" );
                break;
            }
            case DialogueNodeOutput::Branch:
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "If", TextArgumentList().addText( "condition", node._condition.c_str() ) ).c_str() );
                _nodeGraph.beginPin( DialogueGraphPanelInternal::pinBranchTrue( node._id ), false );
                ImGui::TextColored( EditorColorUtil::toImVec4( DialogueGraphPanelInternal::kTrueBranchText ), "True ->" );
                _nodeGraph.endPin();
                ImGui::SameLine();
                _nodeGraph.beginPin( DialogueGraphPanelInternal::pinBranchFalse( node._id ), false );
                ImGui::TextColored( EditorColorUtil::toImVec4( DialogueGraphPanelInternal::kFalseBranchText ), "False ->" );
                _nodeGraph.endPin();
                return;
            }
            case DialogueNodeOutput::Choice:
            {
                // 선택지가 없으면 기본 출력 핀 하나를 보여 준다. 연결되지 않은 선택지는 기본 출력 핀으로 간다(`DialogueCursor::step`).
                if ( node._listChoice.empty() )
                {
                    drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Choice 0 ->" );
                    return;
                }
                for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
                {
                    _nodeGraph.beginPin( DialogueGraphPanelInternal::pinChoice( node._id, static_cast<int32>( choiceIndex ) ), false );
                    ImGui::Text( "#%zu: %s ->", choiceIndex + 1, node._listChoice[choiceIndex].c_str() );
                    _nodeGraph.endPin();
                }
                return;
            }
            case DialogueNodeOutput::None:
            {
                break;
            }
        }

        switch ( pInfo->_body )
        {
            case DialogueNodeBody::Text:
            {
                if ( node._text.empty() )
                    break;
                if ( node._text.size() > 40 )
                {
                    StringBuilder<constant::buffer::kMax64> previewBuilder;
                    previewBuilder.append( string_view{ node._text.data(), 37 } );
                    previewBuilder.append( "..." );
                    ImGui::TextDisabled( "\"%s\"", previewBuilder.c_str() );
                }
                else
                {
                    ImGui::TextDisabled( "\"%s\"", node._text.c_str() );
                }
                break;
            }
            case DialogueNodeBody::Action:
            {
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdFormat", "Cmd", TextArgumentList().addText( "actionCommand", node._actionCommand.c_str() ) ).c_str() );
                break;
            }
            case DialogueNodeBody::Condition:
            case DialogueNodeBody::None:
            {
                break;
            }
        }
    }

    void DialogueGraphPanel::drawActionProperties( DialogueNode& node )
    {
        EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Command" ), "Command" ), node._actionCommand );
        if ( ImGui::IsItemDeactivatedAfterEdit() )
            notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "CommandTip" ) );

        if ( _actionPropertiesText != node._actionCommand || _actionProperties.isValid() == false )
        {
            _actionPropertiesText = node._actionCommand;
            _actionError.clear();
            if ( const expected<void> read = GameActionCall::tryReadProperties( node._actionCommand, _pActionEntry, _actionProperties ); read.has_value() == false )
            {
                _actionError  = string( read.error().message().data(), read.error().message().size() ) + ": " + string( read.error().detail().data(), read.error().detail().size() );
                _pActionEntry = nullptr;
            }
        }

        // 고르기 목록은 등록에서 온다 — 동작을 더하면 코드 한 줄로 여기에 나타난다.
        const string_view currentName = GameActionCall::getCallName( node._actionCommand );
        if ( ImGui::BeginCombo( "##dialogue.action", _pActionEntry != nullptr ? _pActionEntry->_pName : "(pick an action)" ) )
        {
            _listActionName.clear();
            ActionRegistry::collectNames( _listActionName );
            for ( const string& name : _listActionName )
            {
                if ( ImGui::Selectable( name.c_str(), string_view{ name } == currentName ) )
                {
                    node._actionCommand = name;
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                }
            }
            ImGui::EndCombo();
        }
        if ( _pActionEntry == nullptr )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", _actionError.c_str() );
            return;
        }
        _pActionTargetText = &node._actionCommand;
        EditorPropertyGridTarget target{};
        target._pInstance = _actionProperties.getInstance();
        target._pType     = _actionProperties.getType();
        target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &DialogueGraphPanel::onActionFieldEdited, this );
        _actionGrid.drawProperties( target, nullptr, {} );
        _pActionTargetText = nullptr;
    }

    void DialogueGraphPanel::onActionFieldEdited( const PropertyInfo& /*prop*/ )
    {
        if ( _pActionTargetText == nullptr || _pActionEntry == nullptr )
            return;
        *_pActionTargetText   = GameActionCall::formatText( *_pActionEntry, _actionProperties );
        _actionPropertiesText = *_pActionTargetText;
        notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
    }

    void DialogueGraphPanel::drawNodeProperties( DialogueNode& node, const DialogueNodeInfo& info )
    {
        if ( info._bHasSpeaker )
        {
            // 화자는 그래프 화자 목록의 id 다 — 이름 글을 적지 않고 목록에서 고른다(이름을 바꿔도 줄이 끊기지 않는다).
            const DialogueSpeaker* pCurrent = _graph.findSpeaker( node._speaker );
            const utf8*            pPreview = pCurrent != nullptr ? pCurrent->_name.c_str() : SW_LOCKEY( "EditorDialogueGraph", "NoSpeaker" );
            if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Speaker" ), "Speaker" ), pPreview ) )
            {
                if ( ImGui::Selectable( SW_LOCKEY( "EditorDialogueGraph", "NoSpeaker" ), node._speaker == kDialogueNoSpeaker ) && node._speaker != kDialogueNoSpeaker )
                {
                    node._speaker = kDialogueNoSpeaker;
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                }
                for ( const DialogueSpeaker& speaker : _graph._listSpeaker )
                {
                    ImGui::PushID( static_cast<int32>( speaker._id ) );
                    if ( ImGui::Selectable( speaker._name.c_str(), node._speaker == speaker._id ) && node._speaker != speaker._id )
                    {
                        node._speaker = speaker._id;
                        notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "SpeakerTip" ) );
        }

        switch ( info._body )
        {
            case DialogueNodeBody::Text:
            {
                fixed_string<constant::buffer::kMax512> textBuf{ node._text.c_str() };
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "Text" ) ); // 폭 전체 칸이라 이름표는 위에
                if ( ImGui::InputTextMultiline( "##Text", textBuf.data(), textBuf.capacity(), ImVec2( -1, 100 ) ) )
                    node._text = textBuf.c_str();
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "TextTip" ) );
                break;
            }
            case DialogueNodeBody::Condition:
            {
                EditorWidgets::drawTextField( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Condition" ), "Condition" ), node._condition );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "ConditionTip" ) );
                ImGui::TextDisabled( "%s", SW_LOCKEY( "EditorDialogueGraph", "ConditionExample" ) );
                break;
            }
            case DialogueNodeBody::Action:
            {
                drawActionProperties( node );
                break;
            }
            case DialogueNodeBody::None:
            {
                break;
            }
        }

        if ( info._output != DialogueNodeOutput::Choice )
            return;

        ImGui::TextUnformatted( SW_LOCKEYFORMAT( "EdFormat", "Choices", TextArgumentList().addInteger( "choiceCount", static_cast<int64>( node._listChoice.size() ) ) ).c_str() );
        for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
        {
            ImGui::PushID( static_cast<int32>( choiceIndex ) );
            EditorWidgets::drawTextField( "##Choice", node._listChoice[choiceIndex] );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "X" ) );
            if ( ImGui::Button( "X" ) )
            {
                DialogueGraphEdit::removeChoice( _graph, node, choiceIndex );
                notifyDocumentEdited( "Edit Dialogue Node" );
                ImGui::PopID();
                break;
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "RemoveChoiceTip" ) );
            ImGui::PopID();
        }

        if ( ImGui::Button( "+ Add Choice Option" ) )
        {
            node._listChoice.push_back( "New choice option" );
            notifyDocumentEdited( "Edit Dialogue Node" );
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorDialogueGraph", "AddChoiceTip" ) );
    }

    void DialogueGraphPanel::drawInputPin( int32 nodeID )
    {
        const int32 pinID = DialogueGraphPanelInternal::pinIn( nodeID );
        _nodeGraph.beginPin( pinID, true );
        _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
        ImGui::SameLine();
        ImGui::TextUnformatted( "In" );
        _nodeGraph.endPin();
    }

    void DialogueGraphPanel::drawOutputPin( int32 pinID, const utf8* pLabel )
    {
        _nodeGraph.beginPin( pinID, false );
        ImGui::TextUnformatted( pLabel );
        ImGui::SameLine();
        _nodeGraph.drawPinIcon( 0, isPinLinked( pinID ) );
        _nodeGraph.endPin();
    }
} // namespace sw::editor
