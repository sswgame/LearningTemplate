#include "pch.h"

#include "Editor/Panels/AudioGraph/AudioGraphPanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    namespace
    {
        struct AudioGraphPanelInternal
        {
            /// @brief 그래프 복사 버퍼의 종류 열쇠 — 다른 그래프에는 붙지 않는다.
            static constexpr string_view kClipboardKind = "AudioGraph";
            /** @brief 핀 타입 하나(소리 신호)만 쓴다 — 어느 출력이든 어느 입력에 이을 수 있다. */
            static constexpr uint32 kSignalPinType = 0;
            /** @brief 캔버스가 차지하는 폭의 비율입니다(나머지는 인스펙터). */
            static constexpr float32 kCanvasWidthRatio = 0.66f;

            /** @brief 창을 엽니다(명령 표의 실행 함수). */
            static void openPanel()
            {
                (void)EditorPanelManager::getInEditorContext().setPanelOpen<AudioGraphPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace ed = ax::NodeEditor;

    AudioGraphPanel::AudioGraphPanel()
        : EditorDocumentPanel( EditorAssetType{ AudioClipData::kAssetType }, false )
        , _model{}
        , _nodeGraph{}
        , _graphGrid{}
        , _nodeGrid{}
        , _removal{}
    {
        _nodeGraph.setCommentsEnabled( true ); // 주석 상자는 그래프 파일에 실린다(`AudioGraphDef::_listComment`)
    }

    bool AudioGraphPanel::canAcceptDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, AudioGraphDef::kExtension );
    }

    ToolAssetLoadResult AudioGraphPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        // 새 그래프(없는 파일)를 만드는 길은 없다 — 고를 수 있는 것은 있는 파일뿐이라 읽지 못하면 망가진 파일이다(저장을 막는다).
        if ( _model.load( path ) == false )
            return ToolAssetLoadResult::Malformed;
        _nodeGraph.requestContentFit();
        return ToolAssetLoadResult::Loaded;
    }

    void AudioGraphPanel::applyDocumentText( string_view text )
    {
        _model.restore( text );
        _nodeGraph.requestContentFit(); // 되돌린 위치를 캔버스에 다시 넣는다
    }

    bool AudioGraphPanel::saveDocument()
    {
        const optional<AbsolutePath> diskPath = getDocumentDiskPath();
        if ( diskPath.has_value() == false || _model.saveDocument( *diskPath ) == false )
            return false;
        syncDocumentUndoBaseline();
        return true;
    }

    void AudioGraphPanel::onGraphEdited( const PropertyInfo& /*prop*/ )
    {
        _model.markEdited();
    }

    bool AudioGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo )
    {
        bool bInput = false;
        if ( _model.tryDecodePin( pinID, bInput ) == false )
            return false;
        outInfo._type   = AudioGraphPanelInternal::kSignalPinType;
        outInfo._bInput = bInput;
        return true;
    }

    void AudioGraphPanel::drawToolbar()
    {
        ImGui::BeginDisabled( isDocumentDirty() == false || canSaveDocument() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Save" ) ) )
            (void)saveDocumentAndClearDirty(); // 검사 실패는 그래프 저장이 오류로 알린다
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "SaveTip" ) );

        const bool bAudioReady = AudioGraphModel::isPreviewAudioReady();
        _model.refreshPreview();
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) );
        ImGui::BeginDisabled( bAudioReady == false || isDocumentDirty() );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) )
            _model.playPreview( hashed_string( getLoadedAssetPath() ) );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "PreviewTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) );
        ImGui::BeginDisabled( bAudioReady == false || _model.isPreviewPlaying() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) )
            _model.stopPreview();
        ImGui::EndDisabled();
        if ( isDocumentDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "Unsaved" ) );
        }
    }

    void AudioGraphPanel::drawCanvasNode( uint32 nodeIndex )
    {
        const AudioGraphDef&          graph    = _model.getGraph();
        const AudioGraphNodeDef&      node     = graph._listNode[nodeIndex];
        const AudioGraphNodeKindInfo& typeInfo = AudioGraphRegistry::getNodeType( node._type );
        const int32                   nodeID   = AudioGraphEditUtil::makeNodeID( nodeIndex );
        _nodeGraph.beginNode( nodeID );
        ImGui::TextUnformatted( node._name.c_str() );
        ImGui::TextDisabled( "%s%s", typeInfo._pName, node._name == graph._output ? "  [OUT]" : "" );
        if ( typeInfo._bSource == false )
        {
            _nodeGraph.beginPin( AudioGraphEditUtil::makeInputPinID( nodeIndex ), true );
            _nodeGraph.drawPinIcon( AudioGraphPanelInternal::kSignalPinType, node._listInput.empty() == false );
            _nodeGraph.endPin();
            ImGui::SameLine();
        }
        _nodeGraph.beginPin( AudioGraphEditUtil::makeOutputPinID( nodeIndex ), false );
        _nodeGraph.drawPinIcon( AudioGraphPanelInternal::kSignalPinType, true );
        _nodeGraph.endPin();
        _nodeGraph.endNode();

        const bool bPlaceNow = _nodeGraph.needsContentFit() || _model.isPlacingNode( nodeIndex );
        if ( bPlaceNow )
            ed::SetNodePosition( toNodeID( nodeID ), ImVec2( node._editorPosition._x, node._editorPosition._y ) );
    }

    void AudioGraphPanel::handleEditCommands()
    {
        // Ctrl+C · X · V · D — 문서 그래프 패널과 같은 단축키 · 복사 버퍼다. 복사 버퍼에는 노드 이름을 둔다(지우면 번호가 당겨진다).
        vector<int32>                listNodeID;
        float2                       pastePosition{};
        const EditorGraphEditCommand command = _nodeGraph.queryEditCommand( listNodeID, pastePosition );
        if ( command == EditorGraphEditCommand::None )
            return;
        vector<hashed_string> listName;
        if ( command == EditorGraphEditCommand::Paste )
        {
            string text;
            if ( EditorNodeGraph::readClipboard( AudioGraphPanelInternal::kClipboardKind, text ) == false )
                return;
            bool   bCorner = false;
            float2 corner{};
            for ( const string_view token : StringUtil::split( text, "," ) )
            {
                const optional<uint32> index = _model.getGraph().findNodeIndex( hashed_string( token ) );
                if ( index.has_value() == false )
                    continue; // 복사한 뒤 지운 노드
                const float2& position = _model.getGraph()._listNode[*index]._editorPosition;
                corner                 = bCorner ? float2{ MathUtil::min( corner._x, position._x ), MathUtil::min( corner._y, position._y ) } : position;
                bCorner                = true;
                listName.push_back( hashed_string( token ) );
            }
            (void)_model.duplicateNodes( listName, pastePosition - corner ); // 노드 수 한도면 모자란 만큼 넣지 않는다
            return;
        }
        _model.collectNodeNames( listNodeID, listName );
        if ( command == EditorGraphEditCommand::Duplicate )
        {
            (void)_model.duplicateNodes( listName, float2{ EditorNodeGraph::kDuplicateOffset, EditorNodeGraph::kDuplicateOffset } );
            return;
        }
        string text;
        for ( const hashed_string& name : listName )
        {
            text += text.empty() ? "" : ",";
            text += name.c_str();
        }
        EditorNodeGraph::storeClipboard( AudioGraphPanelInternal::kClipboardKind, std::move( text ) );
        if ( command != EditorGraphEditCommand::Cut )
            return;
        for ( const int32 nodeID : listNodeID )
        {
            _nodeGraph.requestNodeDeletion( nodeID ); // 지우기는 applyCanvasDeletions 가 같은 프레임에 받는다
        }
    }

    void AudioGraphPanel::applyCanvasDeletions()
    {
        // 캔버스가 지운 id 를 모두 모은 뒤에 모델이 지운다. 버퍼는 멤버로 두어 프레임마다 할당하지 않는다.
        _nodeGraph.collectRemovals( _removal );
        if ( EditorNodeGraph::removeComments( _model.editGraph()._listComment, _removal._listCommentID ) )
            _model.markCommentsEdited( "Delete Audio Graph Comment" );
        _model.applyDeletions( _removal._listLinkID, _removal._listNodeID );
    }

    void AudioGraphPanel::drawCanvas( float32 canvasWidth )
    {
        ImGui::BeginChild( "##AudioGraphCanvasRegion", ImVec2{ canvasWidth, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        if ( _nodeGraph.beginCanvas( "AudioGraphCanvas", EditorPaths::kAudioGraphCanvasFileName, "audioGraph" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorAudioGraph", "CanvasFailed" ) );
            ImGui::EndChild();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );
        _model.beginCanvasFrame();

        // 주석 상자는 노드보다 먼저 그린다(상자가 노드 뒤에 깔린다). 보기 막대의 주석 단추가 낸 요청이면 새 상자를 더한다.
        vector<GraphCommentBox>& listComment = _model.editGraph()._listComment;
        if ( _nodeGraph.drawComments( listComment, _nodeGraph.needsContentFit() ) )
            _model.markCommentsEdited( "Rename Audio Graph Comment" );
        if ( _nodeGraph.addRequestedComment( listComment ) )
            _model.markCommentsEdited( "Add Audio Graph Comment" );

        const AudioGraphDef& graph     = _model.getGraph();
        const uint32         nodeCount = static_cast<uint32>( graph._listNode.size() );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            drawCanvasNode( nodeIndex );
        }
        // 링크는 받는 노드의 입력 목록을 그린 것이다(링크 표를 따로 두지 않는다).
        for ( uint32 readerIndex = 0; readerIndex < nodeCount; ++readerIndex )
        {
            const vector<hashed_string>& listInput = graph._listNode[readerIndex]._listInput;
            for ( uint32 inputSlot = 0; inputSlot < listInput.size(); ++inputSlot )
            {
                const optional<uint32> fromIndex = graph.findNodeIndex( listInput[inputSlot] );
                if ( fromIndex.has_value() == false )
                    continue; // 없는 노드를 가리키는 입력 — 검사가 저장을 막고 그리드에서 고친다
                ed::Link( toLinkID( AudioGraphEditUtil::makeLinkID( readerIndex, inputSlot ) ), toPinID( AudioGraphEditUtil::makeOutputPinID( *fromIndex ) ),
                          toPinID( AudioGraphEditUtil::makeInputPinID( readerIndex ) ) );
            }
        }

        // 새 링크: 방향 판정 · 거절 표시는 프레임워크가 하고, 소스 입력 · 겹침 · 고리는 모델 규칙이 거절한다.
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &AudioGraphPanel::findGraphPin, this ), fromPin, toPin ) )
            (void)_model.connectPins( fromPin, toPin ); // 규칙이 거절한 링크는 그리지 않는다

        // 빈 곳 오른쪽 클릭: 노드 종류 검색. 고른 노드는 그 자리에 놓는다.
        vector<EditorGraphNodeKind> listKind;
        for ( uint32 typeIndex = 0; typeIndex <= static_cast<uint32>( AudioGraphNodeKind::Gain ); ++typeIndex )
        {
            const AudioGraphNodeKindInfo& typeInfo = AudioGraphRegistry::getNodeType( static_cast<AudioGraphNodeKind>( typeIndex ) );
            listKind.push_back( EditorGraphNodeKind{ typeInfo._pName, typeInfo._bSource ? "Sources" : "Processors", typeIndex } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) )
            (void)_model.addNode( static_cast<AudioGraphNodeKind>( kindID ), canvasPosition ); // 노드 수 한도면 넣지 않는다

        handleEditCommands();
        applyCanvasDeletions();

        // 끌어서 옮긴 노드의 위치를 데이터에 싣는다(맞추기 프레임은 데이터를 캔버스에 넣는 쪽이라 건너뛴다).
        // 지우기 뒤라 노드 수를 다시 읽는다.
        if ( _nodeGraph.needsContentFit() == false )
        {
            const uint32 movedNodeCount = static_cast<uint32>( graph._listNode.size() );
            for ( uint32 nodeIndex = 0; nodeIndex < movedNodeCount; ++nodeIndex )
            {
                const ImVec2 position = ed::GetNodePosition( toNodeID( AudioGraphEditUtil::makeNodeID( nodeIndex ) ) );
                _model.recordNodePosition( nodeIndex, float2{ position.x, position.y } );
            }
            if ( _nodeGraph.syncCommentLayout( _model.editGraph()._listComment ) )
                _model.markCommentsEdited( "Move Audio Graph Comment", "audiograph.comment.move" );
        }

        int32 selectedNodeID{ 0 };
        if ( _nodeGraph.findSelectedNode( selectedNodeID ) )
            _model.selectNodeByID( selectedNodeID );

        _nodeGraph.finishCanvas( static_cast<uint32>( graph._listNode.size() ) );
        ed::SetCurrentEditor( nullptr );
        ImGui::EndChild();
    }

    void AudioGraphPanel::drawInspector()
    {
        ImGui::BeginChild( "##AudioGraphInspector", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const Delegate<void( const PropertyInfo& )> onEdited = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &AudioGraphPanel::onGraphEdited, this );

        // 그래프 설정(출력 · 길이). 노드는 캔버스와 아래 노드 그리드가 고친다.
        EditorPropertyGridTarget graphTarget{};
        graphTarget._pInstance = &_model.editGraph();
        graphTarget._pType     = AudioGraphDef::StaticType();
        graphTarget._onEdited  = onEdited;
        ImGui::PushID( "##AudioGraphSettings" );
        _graphGrid.drawProperties( graphTarget, nullptr, { hashed_string( "_listNode" ) } );
        ImGui::PopID();

        if ( AudioGraphNodeDef* pNode = _model.editSelectedNode(); pNode != nullptr )
        {
            ImGui::SeparatorText( pNode->_name.c_str() );
            if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "SetOutput" ) ) )
                _model.setSelectedAsOutput();
            // 이름을 고치면 다른 노드의 입력 · 그래프 출력도 같은 이름을 따라간다(이름이 연결의 기준이다).
            const hashed_string      previousName = pNode->_name;
            EditorPropertyGridTarget nodeTarget{};
            nodeTarget._pInstance = pNode;
            nodeTarget._pType     = AudioGraphNodeDef::StaticType();
            nodeTarget._onEdited  = onEdited;
            ImGui::PushID( "##AudioGraphNode" );
            _nodeGrid.drawProperties( nodeTarget, nullptr, { hashed_string( "_editorPosition" ) } );
            ImGui::PopID();
            _model.followSelectedRename( previousName );
        }
        else
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "SelectNodeHint" ) );
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EditorAudioGraph", "ProcessingOrder" ) ) )
        {
            if ( _model.refreshProcessingOrder() == false )
                EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "BrokenLinks" ) );
            else
            {
                const AudioGraphDef& graph = _model.getGraph();
                for ( const uint32 nodeIndex : _model.getProcessingOrder() )
                {
                    ImGui::TextUnformatted( graph._listNode[nodeIndex]._name.c_str() );
                }
            }
        }
        ImGui::EndChild();
    }

    void AudioGraphPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawDocumentOpenBar( kPanelID );
        if ( getLoadedAssetPath().empty() || isDocumentLoadFailed() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioGraph", "NoGraph" ) );
            return;
        }
        drawToolbar();
        const float32 canvasWidth = ImGui::GetContentRegionAvail().x * AudioGraphPanelInternal::kCanvasWidthRatio;
        drawCanvas( canvasWidth );
        ImGui::SameLine();
        drawInspector();
        // 이번 그리기에서 캔버스 · 그리드가 고친 일을 문서 기반에 넘긴다 — 되돌리기 단계와 dirty 가 여기서 선다.
        AudioGraphEdit edit;
        if ( _model.takeEdit( edit ) )
            notifyDocumentEdited( edit._pLabel, edit._pCoalesceKey != nullptr ? string_view{ edit._pCoalesceKey } : string_view{} );
    }

    SW_EDITOR_PANEL( AudioGraphPanel, AudioGraphPanel::kPanelID, EditorPanelCategory::Tool, 2102 );
    SW_EDITOR_COMMAND( AudioGraphOpen, "window.audioGraph", &AudioGraphPanelInternal::openPanel, nullptr, NoScene );
} // namespace sw::editor
