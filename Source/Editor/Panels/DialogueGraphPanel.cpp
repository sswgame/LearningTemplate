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

#include "Engine/Dialogue/DialogueGraphAsset.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 핀 번호는 **`DialogueGraphAsset` 이 정본**이다 — 여기서는 이름만 짧게 빌린다.
         * @details 예전에는 이 구조체가 오프셋 상수와 `nodeId * 100 + offset` 인코딩을 자기 사본으로
         *          들고 있었고, 읽는 쪽(`DialogueGraphAsset`)에도 같은 상수가 따로 있었다. 링크는
         *          디스크에 저장되므로 한쪽만 바뀌면 대화가 조용히 엉뚱한 분기를 탄다.
         */
        struct DialogueGraphPanelInternal
        {
            static constexpr int32 kPinInputOffset  = DialogueGraphAsset::kPinOffsetIn;
            static constexpr int32 kPinOutputOffset = DialogueGraphAsset::kPinOffsetOut;
            static constexpr int32 kPinTrueOffset   = DialogueGraphAsset::kPinOffsetTrue;
            static constexpr int32 kPinFalseOffset  = DialogueGraphAsset::kPinOffsetFalse;
            static constexpr int32 kPinChoiceBase   = DialogueGraphAsset::kPinOffsetChoiceBase;

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
        if ( isDocumentLoaded() == false )
            loadGraphData();

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
            if ( ImGui::Button( "+ Dialogue" ) )
            {
                addNode( DialogueAssetNodeType::Dialogue, "NPC", "Enter dialogue text..." );
                notifyDocumentEdited( "Add Dialogue Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "+ Choice" ) )
            {
                addNode( DialogueAssetNodeType::Choice, "", "Player options" );
                notifyDocumentEdited( "Add Dialogue Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "+ Branch" ) )
            {
                addNode( DialogueAssetNodeType::Branch, "", "flag.visited == 1" );
                notifyDocumentEdited( "Add Dialogue Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "+ Action" ) )
            {
                addNode( DialogueAssetNodeType::Action, "", "give_item:potion:1" );
                notifyDocumentEdited( "Add Dialogue Node" );
            }
            ImGui::SameLine();
            if ( ImGui::Button( "+ End" ) )
            {
                addNode( DialogueAssetNodeType::End );
                notifyDocumentEdited( "Add Dialogue Node" );
            }
            ImGui::SameLine();
            EditorWidgets::drawToolbarSeparator();
            if ( ImGui::Button( "Save" ) )
                saveGraphData();
            ImGui::SameLine();
            if ( ImGui::Button( "Reload" ) )
                loadGraphData();
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

            switch ( node._type )
            {
                case DialogueAssetNodeType::Start:
                {
                    ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.3f, 1.0f ), "[START]" );
                    drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Next ->" );
                    break;
                }
                case DialogueAssetNodeType::Dialogue:
                {
                    ImGui::TextColored( ImVec4( 0.4f, 0.7f, 1.0f, 1.0f ), "[DIALOGUE: %s]", node._speaker.empty() ? "(No Speaker)" : node._speaker.c_str() );
                    drawInputPin( node._id );
                    ImGui::SameLine();
                    drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Next ->" );

                    if ( node._text.empty() == false )
                    {
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
                    }
                    break;
                }
                case DialogueAssetNodeType::Choice:
                {
                    ImGui::TextColored( ImVec4( 0.8f, 0.5f, 1.0f, 1.0f ), "[CHOICE]" );
                    drawInputPin( node._id );

                    if ( node._listChoice.empty() )
                    {
                        drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Choice 0 ->" );
                    }
                    else
                    {
                        for ( size_t choiceIndex = 0; choiceIndex < node._listChoice.size(); ++choiceIndex )
                        {
                            ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinChoice( node._id, static_cast<int32>( choiceIndex ) ) ), ed::PinKind::Output );
                            ImGui::Text( "#%zu: %s ->", choiceIndex + 1, node._listChoice[choiceIndex].c_str() );
                            ed::EndPin();
                        }
                    }
                    break;
                }
                case DialogueAssetNodeType::Branch:
                {
                    ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "[BRANCH]" );
                    drawInputPin( node._id );
                    ImGui::TextDisabled( "if (%s)", node._condition.c_str() );

                    ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinBranchTrue( node._id ) ), ed::PinKind::Output );
                    ImGui::TextColored( ImVec4( 0.3f, 1.0f, 0.4f, 1.0f ), "True ->" );
                    ed::EndPin();
                    ImGui::SameLine();
                    ed::BeginPin( toPinId( DialogueGraphPanelInternal::pinBranchFalse( node._id ) ), ed::PinKind::Output );
                    ImGui::TextColored( ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ), "False ->" );
                    ed::EndPin();
                    break;
                }
                case DialogueAssetNodeType::Action:
                {
                    ImGui::TextColored( ImVec4( 0.2f, 0.9f, 0.9f, 1.0f ), "[ACTION]" );
                    drawInputPin( node._id );
                    ImGui::SameLine();
                    drawOutputPin( DialogueGraphPanelInternal::pinOut( node._id ), "Next ->" );
                    ImGui::TextDisabled( "cmd: %s", node._actionCommand.c_str() );
                    break;
                }
                case DialogueAssetNodeType::End:
                {
                    ImGui::TextColored( ImVec4( 0.9f, 0.3f, 0.3f, 1.0f ), "[END]" );
                    drawInputPin( node._id );
                    break;
                }
                default:
                    break;
            }

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

                    // 핀 종류(In vs Out) 분별: In 핀은 끝자리가 1
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

        // 삭제 처리
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkId;
            while ( ed::QueryDeletedLink( &linkId ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const int32 id = static_cast<int32>( linkId.Get() );
                    _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
                                                     [id]( const DialogueLink& l )
                    { return l._id == id; } ),
                                     _listLink.end() );
                    notifyDocumentEdited( "Delete Dialogue Link" );
                }
            }
            ed::NodeId nodeId;
            while ( ed::QueryDeletedNode( &nodeId ) )
            {
                if ( ed::AcceptDeletedItem() )
                {
                    const int32 id = static_cast<int32>( nodeId.Get() );
                    _listNode.erase( std::remove_if( _listNode.begin(), _listNode.end(),
                                                     [id]( const DialogueNode& n )
                    { return n._id == id; } ),
                                     _listNode.end() );
                    _listLink.erase( std::remove_if( _listLink.begin(), _listLink.end(),
                                                     [id]( const DialogueLink& l )
                    {
                        // **핀을 푸는 것도 `DialogueGraphAsset` 이 정본이다.** 여기만 `/ 100` 을
                        // 손으로 적고 있었다 — 그 파일의 "핀 번호 계약" 절이 경고하는 바로 그
                        // 모양이다(인코딩은 이미 한 곳으로 모았는데 디코딩 한 자리가 남았다).
                        // `decodePinNodeId` 는 단순한 나눗셈이 아니라 자릿수 기준(`kPinScale`)이
                        // 다른 옛 핀도 함께 푼다 — 손으로 적은 `/ 100` 은 그것을 모른다. 간격이
                        // 바뀌면 이 줄만 조용히 틀려서, 노드를 지워도 그 링크가 남는다.
                        return DialogueGraphAsset::decodePinNodeId( l._fromPin ) == id ||
                               DialogueGraphAsset::decodePinNodeId( l._toPin ) == id;
                    } ),
                                     _listLink.end() );
                    if ( _selectedNodeId == id )
                        _selectedNodeId = 0;
                    notifyDocumentEdited( "Delete Dialogue Node" );
                }
            }
            ed::EndDelete();
        }
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

            if ( pSelectedNode->_type == DialogueAssetNodeType::Dialogue )
            {
                EditorWidgets::drawTextField( "Speaker", pSelectedNode->_speaker );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );

                fixed_string<constant::kMaxBuffer512> textBuf{ pSelectedNode->_text.c_str() };
                if ( ImGui::InputTextMultiline( "Text", textBuf.data(), textBuf.capacity(), ImVec2( -1, 100 ) ) )
                    pSelectedNode->_text = textBuf.c_str();
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
            }
            else if ( pSelectedNode->_type == DialogueAssetNodeType::Choice )
            {
                EditorWidgets::drawTextField( "Prompt", pSelectedNode->_text );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );

                ImGui::Text( "Choices (%zu):", pSelectedNode->_listChoice.size() );
                for ( size_t choiceIndex = 0; choiceIndex < pSelectedNode->_listChoice.size(); ++choiceIndex )
                {
                    ImGui::PushID( static_cast<int32>( choiceIndex ) );
                    EditorWidgets::drawTextField( "##Choice", pSelectedNode->_listChoice[choiceIndex] );
                    if ( ImGui::IsItemDeactivatedAfterEdit() )
                        notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                    ImGui::SameLine();
                    if ( ImGui::Button( "X" ) )
                    {
                        pSelectedNode->_listChoice.erase( pSelectedNode->_listChoice.begin() + choiceIndex );
                        notifyDocumentEdited( "Edit Dialogue Node" );
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }

                if ( ImGui::Button( "+ Add Choice Option" ) )
                {
                    pSelectedNode->_listChoice.push_back( "New choice option" );
                    notifyDocumentEdited( "Edit Dialogue Node" );
                }
            }
            else if ( pSelectedNode->_type == DialogueAssetNodeType::Branch )
            {
                EditorWidgets::drawTextField( "Condition", pSelectedNode->_condition );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                ImGui::TextDisabled( "Ex: flag.boss_defeated == 1" );
            }
            else if ( pSelectedNode->_type == DialogueAssetNodeType::Action )
            {
                EditorWidgets::drawTextField( "Command", pSelectedNode->_actionCommand );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    notifyDocumentEdited( "Edit Dialogue Node", "dialogue-inspector" );
                ImGui::TextDisabled( "Ex: give_item:potion:3" );
            }
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

        DialogueNode diagNode{};
        diagNode._id          = 2;
        diagNode._type        = DialogueAssetNodeType::Dialogue;
        diagNode._speaker     = "Elder";
        diagNode._text        = "Greetings adventurer! The ancient ruins ahead are full of peril.";
        diagNode._position._x = 250.0f;
        diagNode._position._y = 100.0f;
        _listNode.push_back( diagNode );

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

    void DialogueGraphPanel::loadGraphData()
    {
        DialogueGraphAsset data;
        if ( EditorToolAssetCommands::loadDialogueGraph( data, getLoadedAssetPath() ) )
        {
            _listNode = std::move( data._listNode );
            _listLink = std::move( data._listLink );
        }
        if ( _listNode.empty() )
            ensureDefaults();

        _bGraphLayoutReady = SW_FALSE;
        _previewNodeId     = 0;
        _bPreviewPlaying   = SW_FALSE;
        markDocumentLoaded();
        _nodeGraph.requestContentFit();
    }

    bool DialogueGraphPanel::saveGraphData()
    {
        DialogueGraphAsset data = captureGraphData();
        // 실패하면 아무것도 지우지 않는다 — AnimationGraphPanel 쪽 주석 참고.
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
            DialogueGraphAsset       asset  = captureGraphData();
            const DialogueAssetNode* pStart = asset.findStartNode();
            _previewNodeId                  = ( pStart != nullptr ) ? pStart->_id : 0;
            _bPreviewPlaying                = SW_TRUE;
            _previewHoldSeconds             = 0.0f;
            if ( pStart != nullptr )
                EditorViewportPreview::applyDialogueLine( pStart->_speaker, pStart->_text );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Advance Preview" ) )
            previewAdvance();
        ImGui::SameLine();
        if ( ImGui::Button( "Stop Preview" ) )
        {
            _bPreviewPlaying = SW_FALSE;
            _previewNodeId   = 0;
        }
        if ( _previewNodeId > 0 )
        {
            ImGui::SameLine();
            ImGui::TextDisabled( "Preview node #%d", _previewNodeId );
            DialogueGraphAsset       asset = captureGraphData();
            const DialogueAssetNode* pNode = asset.findNode( _previewNodeId );
            if ( pNode != nullptr && pNode->_type == DialogueAssetNodeType::Choice )
            {
                for ( int32 choiceIndex = 0; choiceIndex < static_cast<int32>( pNode->_listChoice.size() ); ++choiceIndex )
                {
                    ImGui::SameLine();
                    fixed_string<constant::kMaxBuffer64> label;
                    formatstring( label.data(), label.capacity(), "Choice %#", choiceIndex );
                    if ( ImGui::SmallButton( label.c_str() ) )
                        previewAdvance( DialogueGraphPanelInternal::kPinChoiceBase + choiceIndex );
                }
            }
            if ( pNode != nullptr && pNode->_type == DialogueAssetNodeType::Branch )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( "True" ) )
                    previewAdvance( DialogueGraphPanelInternal::kPinTrueOffset );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "False" ) )
                    previewAdvance( DialogueGraphPanelInternal::kPinFalseOffset );
            }
        }
    }

    void DialogueGraphPanel::tickPreview( float32 deltaSeconds )
    {
        if ( _bPreviewPlaying == SW_FALSE || _previewNodeId <= 0 )
            return;
        DialogueGraphAsset       asset = captureGraphData();
        const DialogueAssetNode* pNode = asset.findNode( _previewNodeId );
        if ( pNode == nullptr )
        {
            _bPreviewPlaying = SW_FALSE;
            return;
        }
        if ( pNode->_type == DialogueAssetNodeType::Choice || pNode->_type == DialogueAssetNodeType::Branch )
            return;
        _previewHoldSeconds += deltaSeconds;
        if ( _previewHoldSeconds < 0.9f )
            return;
        _previewHoldSeconds = 0.0f;
        previewAdvance();
    }

    void DialogueGraphPanel::previewAdvance( int32 pinOffset )
    {
        DialogueGraphAsset asset = captureGraphData();
        if ( _previewNodeId <= 0 )
        {
            const DialogueAssetNode* pStart = asset.findStartNode();
            _previewNodeId                  = ( pStart != nullptr ) ? pStart->_id : 0;
            return;
        }
        int32 nextId{ 0 };
        if ( pinOffset == DialogueGraphPanelInternal::kPinOutputOffset )
            nextId = asset.findDefaultNextNodeId( _previewNodeId );
        else
            nextId = asset.findLinkedNodeId( _previewNodeId, pinOffset );
        if ( nextId <= 0 )
        {
            _bPreviewPlaying = SW_FALSE;
            return;
        }
        _previewNodeId                 = nextId;
        const DialogueAssetNode* pNext = asset.findNode( nextId );
        if ( pNext != nullptr )
            EditorViewportPreview::applyDialogueLine( pNext->_speaker, pNext->_text );
        if ( pNext != nullptr && pNext->_type == DialogueAssetNodeType::End )
            _bPreviewPlaying = SW_FALSE;
    }

    void DialogueGraphPanel::addNode( DialogueAssetNodeType type, const utf8* pSpeaker, const utf8* pText )
    {
        DialogueNode node{};
        node._id          = nextNodeId();
        node._type        = type;
        node._speaker     = pSpeaker;
        node._text        = pText;
        node._position._x = 200.0f + static_cast<float32>( ( node._id % 5 ) * 80 );
        node._position._y = 150.0f + static_cast<float32>( ( node._id % 5 ) * 60 );

        if ( type == DialogueAssetNodeType::Choice )
            node._listChoice = { "Option 1", "Option 2" };

        _listNode.push_back( node );
        _selectedNodeId = node._id;
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
