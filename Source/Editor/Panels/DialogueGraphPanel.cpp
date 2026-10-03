#include "pch.h"

#include "Editor/Panels/DialogueGraphPanel.h"

#include "Core/Common/Defines.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorNodeGraphId.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSessionPolicy.h"
#include "Editor/Panels/EditorPanelManager.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 핀 번호의 정본은 **`DialogueGraphAsset`** 입니다. 여기서는 이름만 짧게 빌립니다.
         * @details 예전에는 이 구조체가 오프셋 상수와 `nodeId * 100 + offset` 인코딩을 자기 사본으로 들고 있었고, 읽는
         *          쪽(`DialogueGraphAsset`)에도 같은 상수가 따로 있었습니다. 링크는 디스크에 저장되므로, 한쪽만 바뀌면
         *          대화가 조용히 엉뚱한 분기를 탑니다.
         */
        struct DialogueGraphPanelInternal
        {
            static constexpr int32 kPinInputOffset  = DialogueGraphAsset::kPinOffsetIn;
            static constexpr int32 kPinOutputOffset = DialogueGraphAsset::kPinOffsetOut;
            static constexpr int32 kPinTrueOffset   = DialogueGraphAsset::kPinOffsetTrue;
            static constexpr int32 kPinFalseOffset  = DialogueGraphAsset::kPinOffsetFalse;

            static int32 pinIn( int32 nodeId )
            {
                return DialogueGraphAsset::encodePin( nodeId, kPinInputOffset );
            }

            static int32 pinOut( int32 nodeId )
            {
                return DialogueGraphAsset::encodePin( nodeId, kPinOutputOffset );
            }

            static int32 pinBranchTrue( int32 nodeId )
            {
                return DialogueGraphAsset::encodePin( nodeId, kPinTrueOffset );
            }

            static int32 pinBranchFalse( int32 nodeId )
            {
                return DialogueGraphAsset::encodePin( nodeId, kPinFalseOffset );
            }

            static int32 pinChoice( int32 nodeId, int32 choiceIndex )
            {
                return DialogueGraphAsset::encodeChoicePin( nodeId, choiceIndex );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "DialogueGraphPanel" );
    SW_EDITOR_PANEL( DialogueGraphPanel, "dialogue_graph", EditorPanelCategory::Tool, 1200 );

    DialogueGraphPanel::DialogueGraphPanel()
        : EditorGraphDocumentPanel{ EditorAssetKind::DialogueGraph, "Move Dialogue Nodes", "dialogue-graph-layout" }
        , _selectedNodeId{ 0 }
        , _previewNodeId{ 0 }
    {
    }

    void DialogueGraphPanel::shutdown( IRHIDevice* /*pRhiDevice*/ )
    {
        _nodeGraph.shutdown();
    }

    void DialogueGraphPanel::drawContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();

        tickPreview( ImGui::GetIO().DeltaTime );

        drawGraphToolbar();

        // 노드를 고르면 오른쪽에 인스펙터가 붙으므로 캔버스가 그만큼 좁아진다.
        const float32 availWidth  = ImGui::GetContentRegionAvail().x;
        const float32 canvasWidth = _selectedNodeId > 0 ? availWidth * 0.72f : availWidth;
        drawGraphCanvas( canvasWidth );

        if ( _selectedNodeId > 0 )
        {
            ImGui::SameLine();
            drawSelectedNodeInspector();
        }
    }

    void DialogueGraphPanel::drawGraphToolbar()
    {
        if ( EditorChrome::beginToolbar( "##DialogueToolbar" ) )
        {
            for ( const DialogueNodeTraits& traits : kArrDialogueNodeTraits )
            {
                if ( traits._bAddable == false )
                    continue;
                fixed_string<constant::kMaxBuffer64> label;
                formatstring( label.data(), label.capacity(), "+ %#", traits._pName );
                if ( ImGui::Button( label.c_str() ) )
                {
                    addNode( traits._type );
                    notifyDocumentEdited( "Add Dialogue Node" );
                }
                ImGui::SameLine();
            }
            EditorWidgets::drawToolbarSeparator();
            if ( ImGui::Button( "Save" ) )
                (void)saveGraphData(); // 실패는 저장 커맨드가 알린다
            ImGui::SameLine();
            if ( ImGui::Button( "Reload" ) )
                reloadDocument();
            ImGui::SameLine();
            if ( ImGui::Button( "Reset Default" ) )
            {
                _listNode.clear();
                _listLink.clear();
                ensureDefaults();
                _nodeGraph.requestContentFit();
                notifyDocumentEdited( "Reset Dialogue Graph" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Zoom Fit" ) )
                _nodeGraph.requestContentFit();
            drawPreviewToolbar();

            ImGui::SameLine();
            ImGui::TextDisabled( "(Nodes: %zu, Links: %zu)", _listNode.size(), _listLink.size() );
        }
        EditorChrome::endToolbar();
    }

    void DialogueGraphPanel::drawGraphCanvas( float32 canvasWidth )
    {
        editor::EditorSectionDesc canvasDesc{};
        canvasDesc._pId       = "DialogueCanvasRegion";
        canvasDesc._kind      = editor::EditorSectionKind::Child;
        canvasDesc._childSize = float2{ canvasWidth, 0.0f };
        canvasDesc._flags     = editor::EditorSectionFlags::NoScrollbar | editor::EditorSectionFlags::NoScrollWithMouse;
        EditorChrome::beginSection( canvasDesc );

        if ( _nodeGraph.beginCanvas( "DialogueGraphCanvas", "DialogueGraphEditor.json" ) == false )
        {
            ImGui::TextUnformatted( "Failed to create Dialogue Node Editor context." );
            EditorChrome::endSection();
            return;
        }

        drawGraphNodes();

        // 링크 렌더링
        for ( const DialogueLink& link : _listLink )
        {
            ed::Link( toLinkId( link._id ), toPinId( link._fromPin ), toPinId( link._toPin ) );
        }

        handleCanvasInteractions();
        // 선택 노드 추적
        ed::NodeId  selectedNodes[1];
        const int32 count = ed::GetSelectedNodes( selectedNodes, 1 );
        if ( count > 0 )
            _selectedNodeId = static_cast<int32>( selectedNodes[0].Get() );

        _nodeGraph.applyContentFitIfNeeded();
        cacheNodeLayout();

        _nodeGraph.endCanvas();
        EditorChrome::endSection();
    }

    void DialogueGraphPanel::drawGraphNodes()
    {
        // 노드 렌더링
        for ( DialogueNode& node : _listNode )
        {
            const ed::NodeId nodeId = toNodeId( node._id );
            ed::BeginNode( nodeId );

            drawNodeBody( node );

            ed::EndNode();

            if ( _nodeGraph.needsContentFit() )
                ed::SetNodePosition( nodeId, ImVec2( node._position._x, node._position._y ) );
        }
    }

    void DialogueGraphPanel::handleCanvasInteractions()
    {
        // 새 링크 생성 처리
        if ( ed::BeginCreate() )
        {
            ed::PinId a;
            ed::PinId b;
            if ( ed::QueryNewLink( &a, &b ) )
            {
                if ( a.Get() != 0 && b.Get() != 0 && ed::AcceptNewItem() )
                {
                    DialogueLink newLink{};
                    newLink._id      = nextLinkId();
                    const int32 pinA = static_cast<int32>( a.Get() );
                    const int32 pinB = static_cast<int32>( b.Get() );

                    // 핀 종류(In/Out) 구분: 핀 오프셋이 kPinInputOffset 이면 In 핀이다
                    const bool bIsAInput = DialogueGraphAsset::decodePinOffset( pinA ) == DialogueGraphPanelInternal::kPinInputOffset;
                    const bool bIsBInput = DialogueGraphAsset::decodePinOffset( pinB ) == DialogueGraphPanelInternal::kPinInputOffset;

                    if ( bIsAInput != bIsBInput )
                    {
                        if ( bIsAInput )
                        {
                            newLink._fromPin = pinB;
                            newLink._toPin   = pinA;
                        }
                        else
                        {
                            newLink._fromPin = pinA;
                            newLink._toPin   = pinB;
                        }
                        _listLink.push_back( newLink );
                        notifyDocumentEdited( "Link Dialogue Nodes" );
                    }
                }
            }
        }
        ed::EndCreate();

        // 삭제 처리. 링크가 노드에 닿는지는 핀 번호를 풀어 본다. **핀을 푸는 정본은 `DialogueGraphAsset` 이다.**
        // `decodePinNodeId` 는 자릿수 기준(`kPinScale`)이 다른 옛 핀도 함께 푼다. 예전에는 여기만 `/ 100` 을 손으로 적어서,
        // 기준이 바뀌면 노드를 지워도 그 링크가 남을 수 있었다.
        processCanvasDeletions(
            []( const DialogueLink& link, int32 nodeId )
        { return DialogueGraphAsset::decodePinNodeId( link._fromPin ) == nodeId || DialogueGraphAsset::decodePinNodeId( link._toPin ) == nodeId; },
            [this]( int32 nodeId )
        {
            if ( _selectedNodeId == nodeId )
                _selectedNodeId = 0;
        },
            "Delete Dialogue Link", "Delete Dialogue Node" );
    }

    void DialogueGraphPanel::drawSelectedNodeInspector()
    {
        editor::EditorSectionDesc inspectorDesc{};
        inspectorDesc._pId   = "DialogueNodeInspector";
        inspectorDesc._kind  = editor::EditorSectionKind::Child;
        inspectorDesc._flags = editor::EditorSectionFlags::Border;
        EditorChrome::beginSection( inspectorDesc );

        DialogueNode* pSelectedNode{ nullptr };
        for ( DialogueNode& node : _listNode )
        {
            if ( node._id == _selectedNodeId )
            {
                pSelectedNode = &node;
                break;
            }
        }

        if ( pSelectedNode != nullptr )
        {
            ImGui::TextColored( ImVec4( 0.2f, 0.8f, 1.0f, 1.0f ), "Node #%d (%s)", pSelectedNode->_id, DialogueGraphAsset::nodeTypeName( pSelectedNode->_type ) );
            ImGui::Separator();

            const DialogueNodeTraits* pTraits = DialogueGraphAsset::findNodeTraits( pSelectedNode->_type );
            if ( pTraits != nullptr )
                drawNodeFields( *pSelectedNode, *pTraits );
        }

        EditorChrome::endSection();
    }

    void DialogueGraphPanel::ensureDefaults()
    {
        _listNode.clear();
        _listLink.clear();

        DialogueNode startNode{};
        startNode._id          = 1;
        startNode._type        = DialogueAssetNodeType::Start;
        startNode._position._x = 50.0f;
        startNode._position._y = 100.0f;
        _listNode.push_back( startNode );

        DialogueNode dialogueNode{};
        dialogueNode._id          = 2;
        dialogueNode._type        = DialogueAssetNodeType::Dialogue;
        dialogueNode._speaker     = "Elder";
        dialogueNode._text        = "Greetings adventurer! The ancient ruins ahead are full of peril.";
        dialogueNode._position._x = 250.0f;
        dialogueNode._position._y = 100.0f;
        _listNode.push_back( dialogueNode );

        DialogueNode choiceNode{};
        choiceNode._id          = 3;
        choiceNode._type        = DialogueAssetNodeType::Choice;
        choiceNode._text        = "How do you respond?";
        choiceNode._listChoice  = { "I am ready for any challenge!", "Could you give me some supplies first?" };
        choiceNode._position._x = 650.0f;
        choiceNode._position._y = 100.0f;
        _listNode.push_back( choiceNode );

        DialogueNode actionNode{};
        actionNode._id            = 4;
        actionNode._type          = DialogueAssetNodeType::Action;
        actionNode._actionCommand = "give_item:healing_potion:3";
        actionNode._position._x   = 1050.0f;
        actionNode._position._y   = 220.0f;
        _listNode.push_back( actionNode );

        DialogueNode endNode{};
        endNode._id          = 5;
        endNode._type        = DialogueAssetNodeType::End;
        endNode._position._x = 1350.0f;
        endNode._position._y = 120.0f;
        _listNode.push_back( endNode );

        // 기본 링크 연결
        _listLink.push_back( DialogueLink{ 1, DialogueGraphPanelInternal::pinOut( 1 ), DialogueGraphPanelInternal::pinIn( 2 ) } );
        _listLink.push_back( DialogueLink{ 2, DialogueGraphPanelInternal::pinOut( 2 ), DialogueGraphPanelInternal::pinIn( 3 ) } );
        _listLink.push_back( DialogueLink{ 3, DialogueGraphPanelInternal::pinChoice( 3, 0 ), DialogueGraphPanelInternal::pinIn( 5 ) } );
        _listLink.push_back( DialogueLink{ 4, DialogueGraphPanelInternal::pinChoice( 3, 1 ), DialogueGraphPanelInternal::pinIn( 4 ) } );
        _listLink.push_back( DialogueLink{ 5, DialogueGraphPanelInternal::pinOut( 4 ), DialogueGraphPanelInternal::pinIn( 5 ) } );
    }

    ToolAssetLoadResult DialogueGraphPanel::loadDocument()
    {
        DialogueGraphAsset        data;
        const ToolAssetLoadResult result = EditorToolAssetCommands::loadDialogueGraph( data, getLoadedAssetPath() );
        // 읽지 못한 파일 앞에서는 앞 문서의 그래프를 들고 있지 않는다 — 기본 그래프를 보이고 저장을 막는다(덮지 않게).
        _listNode.clear();
        _listLink.clear();
        if ( result == ToolAssetLoadResult::Loaded )
        {
            _listNode = std::move( data._listNode );
            _listLink = std::move( data._listLink );
        }
        if ( _listNode.empty() )
            ensureDefaults();

        _bGraphLayoutReady = SW_FALSE;
        _previewNodeId     = 0;
        _bPreviewPlaying   = SW_FALSE;
        _nodeGraph.requestContentFit();
        return result;
    }

    bool DialogueGraphPanel::saveGraphData()
    {
        DialogueGraphAsset data = captureGraphData();
        // 실패하면 아무것도 지우지 않는다. AnimationGraphPanel 쪽 주석 참고.
        if ( EditorToolAssetCommands::saveDialogueGraph( data, getLoadedAssetPath() ) == false )
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
        ImGui::SameLine();
        if ( ImGui::Button( "Play Preview" ) )
        {
            const DialogueGraphAsset asset  = captureGraphData();
            const DialogueAssetNode* pStart = asset.findStartNode();
            _bPreviewPlaying                = SW_TRUE;
            enterPreviewNode( asset, pStart != nullptr ? pStart->_id : 0 );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Advance Preview" ) )
            previewStep( DialogueStepInput{} );
        ImGui::SameLine();
        if ( ImGui::Button( "Stop Preview" ) )
        {
            _bPreviewPlaying = SW_FALSE;
            _previewNodeId   = 0;
        }
        if ( _previewNodeId <= 0 )
            return;

        ImGui::SameLine();
        ImGui::TextDisabled( "Preview node #%d", _previewNodeId );
        const DialogueGraphAsset  asset   = captureGraphData();
        const DialogueAssetNode*  pNode   = asset.findNode( _previewNodeId );
        const DialogueNodeTraits* pTraits = pNode != nullptr ? DialogueGraphAsset::findNodeTraits( pNode->_type ) : nullptr;
        if ( pTraits == nullptr )
            return;

        // 기다리는 노드만 사람이 고른다. 고른 값은 러너와 같은 `DialogueCursor::step` 의 입력이 된다.
        if ( pTraits->_flow == DialogueNodeFlow::WaitChoice )
        {
            for ( int32 choiceIndex = 0; choiceIndex < static_cast<int32>( pNode->_listChoice.size() ); ++choiceIndex )
            {
                ImGui::SameLine();
                fixed_string<constant::kMaxBuffer64> label;
                formatstring( label.data(), label.capacity(), "Choice %#", choiceIndex );
                if ( ImGui::SmallButton( label.c_str() ) )
                {
                    DialogueStepInput input{};
                    input._choiceIndex = choiceIndex;
                    previewStep( input );
                }
            }
        }
        if ( pTraits->_flow == DialogueNodeFlow::Condition )
        {
            ImGui::SameLine();
            if ( ImGui::SmallButton( "True" ) )
            {
                DialogueStepInput input{};
                input._bConditionMet = true;
                previewStep( input );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( "False" ) )
                previewStep( DialogueStepInput{} );
        }
    }

    void DialogueGraphPanel::tickPreview( float32 deltaSeconds )
    {
        if ( _bPreviewPlaying == SW_FALSE || _previewNodeId <= 0 )
            return;
        const DialogueGraphAsset  asset   = captureGraphData();
        const DialogueAssetNode*  pNode   = asset.findNode( _previewNodeId );
        const DialogueNodeTraits* pTraits = pNode != nullptr ? DialogueGraphAsset::findNodeTraits( pNode->_type ) : nullptr;
        if ( pTraits == nullptr )
        {
            _bPreviewPlaying = SW_FALSE;
            return;
        }

        switch ( pTraits->_flow )
        {
            case DialogueNodeFlow::PassThrough:
            {
                previewStep( DialogueStepInput{} );
                return;
            }
            case DialogueNodeFlow::WaitAdvance:
            {
                _previewHoldSeconds += deltaSeconds;
                if ( _previewHoldSeconds < 0.9f )
                    return;
                previewStep( DialogueStepInput{} );
                return;
            }
            case DialogueNodeFlow::Finish:
            {
                _bPreviewPlaying = SW_FALSE;
                return;
            }
            case DialogueNodeFlow::WaitChoice: // 사람이 툴바 버튼으로 고른다
            case DialogueNodeFlow::Condition:
                return;
        }
    }

    void DialogueGraphPanel::previewStep( const DialogueStepInput& input )
    {
        const DialogueGraphAsset asset = captureGraphData();
        if ( _previewNodeId <= 0 )
        {
            const DialogueAssetNode* pStart = asset.findStartNode();
            enterPreviewNode( asset, pStart != nullptr ? pStart->_id : 0 );
            return;
        }
        const DialogueAssetNode* pNode = asset.findNode( _previewNodeId );
        enterPreviewNode( asset, pNode != nullptr ? DialogueCursor::step( asset, *pNode, input ) : 0 );
    }

    void DialogueGraphPanel::enterPreviewNode( const DialogueGraphAsset& asset, int32 nodeId )
    {
        _previewHoldSeconds               = 0.0f;
        const DialogueAssetNode*  pNode   = asset.findNode( nodeId );
        const DialogueNodeTraits* pTraits = pNode != nullptr ? DialogueGraphAsset::findNodeTraits( pNode->_type ) : nullptr;
        if ( pTraits == nullptr )
        {
            _bPreviewPlaying = SW_FALSE;
            return;
        }

        _previewNodeId = nodeId;
        // 러너가 알리는 노드(대사 · 선택지)만 뷰포트에 보인다.
        if ( pTraits->_flow == DialogueNodeFlow::WaitAdvance || pTraits->_flow == DialogueNodeFlow::WaitChoice )
            EditorViewportPreview::applyDialogueLine( pNode->_speaker, pNode->_text );
        if ( pTraits->_flow == DialogueNodeFlow::Finish )
            _bPreviewPlaying = SW_FALSE;
    }

    void DialogueGraphPanel::addNode( DialogueAssetNodeType type )
    {
        DialogueNode node = DialogueCursor::makeNode( type, nextNodeId() );
        node._position._x = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
        node._position._y = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );
        _selectedNodeId   = node._id;
        _listNode.push_back( std::move( node ) );
    }

    void DialogueGraphPanel::drawNodeBody( const DialogueNode& node )
    {
        const DialogueNodeTraits* pTraits = DialogueGraphAsset::findNodeTraits( node._type );
        if ( pTraits == nullptr )
        {
            ImGui::TextDisabled( "[Unknown %u]", static_cast<uint32>( node._type ) );
            return;
        }

        const ImVec4 headerColor( pTraits->_color._x, pTraits->_color._y, pTraits->_color._z, pTraits->_color._w );
        if ( pTraits->_bHasSpeaker )
            ImGui::TextColored( headerColor, "[%s: %s]", pTraits->_pName, node._speaker.empty() ? "(No Speaker)" : node._speaker.c_str() );
        else
            ImGui::TextColored( headerColor, "[%s]", pTraits->_pName );

        if ( pTraits->_bHasInputPin )
            drawInputPin( node._id );

        switch ( pTraits->_output )
        {
            case DialogueNodeOutput::Next:
            {
                if ( pTraits->_bHasInputPin )
                    ImGui::SameLine();
                drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Next ->" );
                break;
            }
            case DialogueNodeOutput::Branch:
            {
                ImGui::TextDisabled( "if (%s)", node._condition.c_str() );
                ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinBranchTrue( node._id ) ), ed::PinKind::Output );
                ImGui::TextColored( ImVec4( 0.3f, 1.0f, 0.4f, 1.0f ), "True ->" );
                ed::EndPin();
                ImGui::SameLine();
                ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinBranchFalse( node._id ) ), ed::PinKind::Output );
                ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "False ->" );
                ed::EndPin();
                return;
            }
            case DialogueNodeOutput::Choice:
            {
                // 선택지가 없으면 기본 출력 핀 하나를 보인다 — 이어지지 않은 선택지는 기본 출력 핀으로 간다(`DialogueCursor::step`).
                if ( node._listChoice.empty() )
                {
                    drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Choice 0 ->" );
                    return;
                }
                for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
                {
                    ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinChoice( node._id, static_cast<int32>( choiceIndex ) ) ), ed::PinKind::Output );
                    ImGui::Text( "#%zu: %s ->", choiceIndex + 1, node._listChoice[choiceIndex].c_str() );
                    ed::EndPin();
                }
                return;
            }
            case DialogueNodeOutput::None:
                break;
        }

        switch ( pTraits->_body )
        {
            case DialogueNodeBody::Text:
            {
                if ( node._text.empty() )
                    break;
                if ( node._text.size() > 40 )
                {
                    StringBuilder<constant::kMaxBuffer64> previewBuilder;
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
                ImGui::TextDisabled( "cmd: %s", node._actionCommand.c_str() );
                break;
            }
            case DialogueNodeBody::Condition:
            case DialogueNodeBody::None:
                break;
        }
    }

    void DialogueGraphPanel::drawNodeFields( DialogueNode& node, const DialogueNodeTraits& traits )
    {
        if ( traits._bHasSpeaker )
        {
            EditorWidgets::drawTextField( "Speaker", node._speaker );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
        }

        switch ( traits._body )
        {
            case DialogueNodeBody::Text:
            {
                fixed_string<constant::kMaxBuffer512> textBuf{ node._text.c_str() };
                if ( ImGui::InputTextMultiline( "Text", textBuf.data(), textBuf.capacity(), ImVec2( -1, 100 ) ) )
                    node._text = textBuf.c_str();
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                break;
            }
            case DialogueNodeBody::Condition:
            {
                EditorWidgets::drawTextField( "Condition", node._condition );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                ImGui::TextDisabled( "Ex: flag.boss_defeated == 1" );
                break;
            }
            case DialogueNodeBody::Action:
            {
                EditorWidgets::drawTextField( "Command", node._actionCommand );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                ImGui::TextDisabled( "Ex: give_item:potion:3" );
                break;
            }
            case DialogueNodeBody::None:
                break;
        }

        if ( traits._output != DialogueNodeOutput::Choice )
            return;

        ImGui::Text( "Choices (%zu):", node._listChoice.size() );
        for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
        {
            ImGui::PushID( static_cast<int32>( choiceIndex ) );
            EditorWidgets::drawTextField( "##Choice", node._listChoice[choiceIndex] );
            if ( ImGui::IsItemDeactivatedAfterEdit() )
                notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
            ImGui::SameLine();
            if ( ImGui::Button( "X" ) )
            {
                node._listChoice.erase( node._listChoice.begin() + static_cast<std::ptrdiff_t>( choiceIndex ) );
                notifyDocumentEdited( "Edit Dialogue Node" );
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        if ( ImGui::Button( "+ Add Choice Option" ) )
        {
            node._listChoice.push_back( "New choice option" );
            notifyDocumentEdited( "Edit Dialogue Node" );
        }
    }

    void DialogueGraphPanel::drawInputPin( int32 nodeId )
    {
        ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinIn( nodeId ) ), ed::PinKind::Input );
        ImGui::TextUnformatted( "-> In" );
        ed::EndPin();
    }

    void DialogueGraphPanel::drawOutputPin( int32 pinId, const utf8* pLabel )
    {
        ed::BeginPin( toPinId( pinId ), ed::PinKind::Output );
        ImGui::TextUnformatted( pLabel );
        ed::EndPin();
    }
} // namespace sw::editor
