#include "pch.h"

#include "Editor/Panels/AudioGraph/AudioGraphPanel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/EditorPaths.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/Graph/AudioGraph.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    namespace
    {
        struct AudioGraphPanelInternal
        {
            /** @brief 핀 타입 하나(소리 신호)만 쓴다 — 어느 출력이든 어느 입력에 이을 수 있다. */
            static constexpr uint32 kSignalPinType = 0;
            /** @brief 캔버스가 차지하는 폭의 비율입니다(나머지는 인스펙터). */
            static constexpr float32 kCanvasWidthRatio = 0.66f;
            /** @brief 옮긴 것으로 보는 위치 차이(캔버스 좌표)입니다. 부동소수 반올림을 고침으로 읽지 않는다. */
            static constexpr float32 kMoveEpsilon = 0.5f;

            /** @brief 창을 엽니다(명령 표의 실행 함수). */
            static void openPanel()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<AudioGraphPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace ed = ax::NodeEditor;

    AudioGraphPanel::AudioGraphPanel()
        : IEditorPanel( false )
        , _editGraph{}
        , _nodeGraph{}
        , _graphGrid{}
        , _nodeGrid{}
        , _listOrder{}
        , _editPath{}
        , _previewID{ 0 }
        , _selectedNode{ -1 }
        , _pendingPlacedNode{ -1 }
        , _pathInput{}
        , _bEditDirty{ false }
    {
    }

    void AudioGraphPanel::loadGraph()
    {
        const string_view path{ _pathInput.c_str() };
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        if ( _editGraph.loadFromResource( path ) == false )
        {
            _editGraph = AudioGraphDef{};
            _editPath  = hashed_string{};
            return;
        }
        _editPath   = hashed_string( path );
        _bEditDirty = false;
        // 손으로 쓴 파일은 위치가 없다 — 처리 깊이로 한 번 놓는다. 놓기는 고침이 아니다(저장할 것이 생기지 않는다).
        (void)AudioGraphEditUtil::layoutIfUnplaced( _editGraph ); // 이미 놓인 파일이면 아무것도 하지 않는다
        _nodeGraph.requestContentFit();
    }

    void AudioGraphPanel::onGraphEdited( const PropertyInfo& /*prop*/ )
    {
        _bEditDirty = true;
    }

    bool AudioGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo )
    {
        uint32 nodeIndex = 0;
        bool   bInput    = false;
        if ( AudioGraphEditUtil::tryDecodePinID( _editGraph, pinID, nodeIndex, bInput ) == false )
            return false;
        outInfo._type   = AudioGraphPanelInternal::kSignalPinType;
        outInfo._bInput = bInput;
        return true;
    }

    void AudioGraphPanel::drawToolbar()
    {
        ImGui::BeginDisabled( _bEditDirty == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Save" ) ) )
        {
            // 검사를 통과하지 못하면 쓰지 않고 오류를 남긴다(사본은 남아 계속 고칠 수 있다).
            if ( _editGraph.saveToResource( _editPath.view() ) )
                _bEditDirty = false;
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "SaveTip" ) );

        IAudioService* pAudio      = editor::findSubsystem<IAudioService>();
        const bool     bAudioReady = pAudio != nullptr && pAudio->isInitialized();
        ImGui::SameLine();
        ImGui::BeginDisabled( bAudioReady == false || _bEditDirty );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) )
        {
            AudioEngine& engine = pAudio->getEngine();
            if ( _previewID != 0 )
                engine.stop( _previewID, 0.0f );
            _previewID = engine.playClip( _editPath, hashed_string( AudioBusNames::kUI ), AudioClipPlayParams{} );
        }
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "PreviewTip" ) );
        ImGui::SameLine();
        ImGui::BeginDisabled( bAudioReady == false || _previewID == 0 );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) )
        {
            pAudio->getEngine().stop( _previewID, 0.05f );
            _previewID = 0;
        }
        ImGui::EndDisabled();
        if ( _bEditDirty )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "Unsaved" ) );
        }
    }

    void AudioGraphPanel::drawCanvasNode( uint32 nodeIndex )
    {
        const AudioGraphNodeDesc&     node     = _editGraph._listNode[nodeIndex];
        const AudioGraphNodeTypeInfo& typeInfo = AudioGraphRegistry::getNodeType( node._type );
        const int32                   nodeID   = AudioGraphEditUtil::makeNodeID( nodeIndex );
        ed::BeginNode( toNodeID( nodeID ) );
        ImGui::TextUnformatted( node._name.c_str() );
        ImGui::TextDisabled( "%s%s", typeInfo._pName, node._name == _editGraph._output ? "  [OUT]" : "" );
        if ( typeInfo._bSource == false )
        {
            ed::BeginPin( toPinID( AudioGraphEditUtil::makeInputPinID( nodeIndex ) ), ed::PinKind::Input );
            _nodeGraph.drawPinIcon( AudioGraphPanelInternal::kSignalPinType, node._listInput.empty() == false );
            ed::EndPin();
            ImGui::SameLine();
        }
        ed::BeginPin( toPinID( AudioGraphEditUtil::makeOutputPinID( nodeIndex ) ), ed::PinKind::Output );
        _nodeGraph.drawPinIcon( AudioGraphPanelInternal::kSignalPinType, true );
        ed::EndPin();
        ed::EndNode();

        const bool bPlaceNow = _nodeGraph.needsContentFit() || _pendingPlacedNode == static_cast<int32>( nodeIndex );
        if ( bPlaceNow )
        {
            ed::SetNodePosition( toNodeID( nodeID ), ImVec2( node._editorPosition._x, node._editorPosition._y ) );
            if ( _pendingPlacedNode == static_cast<int32>( nodeIndex ) )
                _pendingPlacedNode = -1;
        }
    }

    void AudioGraphPanel::applyCanvasDeletions()
    {
        vector<int32> listLinkID;
        vector<int32> listNodeID;
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkID;
            while ( ed::QueryDeletedLink( &linkID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    listLinkID.push_back( static_cast<int32>( linkID.Get() ) );
            }
            ed::NodeId nodeID;
            while ( ed::QueryDeletedNode( &nodeID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    listNodeID.push_back( static_cast<int32>( nodeID.Get() ) );
            }
        }
        ed::EndDelete();
        // 번호가 밀리지 않게 큰 id 부터 지운다(같은 노드의 입력 자리도 뒤 자리가 큰 id 다). 링크를 먼저 지운 뒤 노드를 지운다.
        std::sort( listLinkID.begin(), listLinkID.end(), std::greater<int32>{} );
        for ( const int32 linkID : listLinkID )
        {
            uint32 readerIndex = 0;
            uint32 inputSlot   = 0;
            if ( AudioGraphEditUtil::tryDecodeLinkID( _editGraph, linkID, readerIndex, inputSlot ) && AudioGraphEditUtil::disconnect( _editGraph, readerIndex, inputSlot ) )
                _bEditDirty = true;
        }
        std::sort( listNodeID.begin(), listNodeID.end(), std::greater<int32>{} );
        for ( const int32 nodeID : listNodeID )
        {
            uint32 nodeIndex = 0;
            if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) && AudioGraphEditUtil::removeNode( _editGraph, nodeIndex ) )
            {
                _bEditDirty   = true;
                _selectedNode = -1;
            }
        }
    }

    void AudioGraphPanel::drawCanvas( float32 canvasWidth )
    {
        ImGui::BeginChild( "##AudioGraphCanvasRegion", ImVec2{ canvasWidth, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        if ( _nodeGraph.beginCanvas( "AudioGraphCanvas", EditorPaths::kAudioGraphCanvasFileName ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorAudioGraph", "CanvasFailed" ) );
            ImGui::EndChild();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );

        const uint32 nodeCount = static_cast<uint32>( _editGraph._listNode.size() );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            drawCanvasNode( nodeIndex );
        }
        // 링크는 받는 노드의 입력 목록을 그린 것이다(링크 표를 따로 두지 않는다).
        for ( uint32 readerIndex = 0; readerIndex < nodeCount; ++readerIndex )
        {
            const vector<hashed_string>& listInput = _editGraph._listNode[readerIndex]._listInput;
            for ( uint32 inputSlot = 0; inputSlot < listInput.size(); ++inputSlot )
            {
                const int32 fromIndex = _editGraph.findNodeIndex( listInput[inputSlot] );
                if ( fromIndex < 0 )
                    continue; // 없는 노드를 가리키는 입력 — 검사가 저장을 막고 그리드에서 고친다
                ed::Link( toLinkID( AudioGraphEditUtil::makeLinkID( readerIndex, inputSlot ) ), toPinID( AudioGraphEditUtil::makeOutputPinID( static_cast<uint32>( fromIndex ) ) ),
                          toPinID( AudioGraphEditUtil::makeInputPinID( readerIndex ) ) );
            }
        }

        // 새 링크: 방향 판정 · 거절 표시는 프레임워크가 하고, 소스 입력 · 겹침 · 고리는 모델 규칙이 거절한다.
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &AudioGraphPanel::findGraphPin, this ), fromPin, toPin ) )
        {
            uint32     fromIndex = 0;
            uint32     toIndex   = 0;
            bool       bInput    = false;
            const bool bDecoded  = AudioGraphEditUtil::tryDecodePinID( _editGraph, fromPin, fromIndex, bInput ) && AudioGraphEditUtil::tryDecodePinID( _editGraph, toPin, toIndex, bInput );
            if ( bDecoded && AudioGraphEditUtil::connect( _editGraph, fromIndex, toIndex ) )
                _bEditDirty = true;
        }

        // 빈 곳 오른쪽 클릭: 노드 종류 검색. 고른 노드는 그 자리에 놓는다.
        vector<EditorGraphNodeKind> listKind;
        for ( uint32 typeIndex = 0; typeIndex <= static_cast<uint32>( AudioGraphNodeType::Gain ); ++typeIndex )
        {
            const AudioGraphNodeTypeInfo& typeInfo = AudioGraphRegistry::getNodeType( static_cast<AudioGraphNodeType>( typeIndex ) );
            listKind.push_back( EditorGraphNodeKind{ typeInfo._pName, typeInfo._bSource ? "Sources" : "Processors", typeIndex } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        uint32 newIndex{ 0 };
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) && AudioGraphEditUtil::addNode( _editGraph, static_cast<AudioGraphNodeType>( kindID ), canvasPosition, newIndex ) )
        {
            _pendingPlacedNode = static_cast<int32>( newIndex );
            _selectedNode      = static_cast<int32>( newIndex );
            _bEditDirty        = true;
        }

        applyCanvasDeletions();

        // 끌어서 옮긴 노드의 위치를 데이터에 싣는다(맞추기 프레임은 데이터를 캔버스에 넣는 쪽이라 건너뛴다).
        if ( _nodeGraph.needsContentFit() == false )
        {
            for ( uint32 nodeIndex = 0; nodeIndex < _editGraph._listNode.size(); ++nodeIndex )
            {
                float2&      stored   = _editGraph._listNode[nodeIndex]._editorPosition;
                const ImVec2 position = ed::GetNodePosition( toNodeID( AudioGraphEditUtil::makeNodeID( nodeIndex ) ) );
                const bool   bMoved   = MathUtil::abs( position.x - stored._x ) > AudioGraphPanelInternal::kMoveEpsilon || MathUtil::abs( position.y - stored._y ) > AudioGraphPanelInternal::kMoveEpsilon;
                if ( bMoved && _pendingPlacedNode != static_cast<int32>( nodeIndex ) )
                {
                    stored      = float2{ position.x, position.y };
                    _bEditDirty = true;
                }
            }
        }

        ed::NodeId arrSelected[1];
        if ( ed::GetSelectedNodes( arrSelected, 1 ) > 0 )
        {
            uint32 nodeIndex = 0;
            if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, static_cast<int32>( arrSelected[0].Get() ), nodeIndex ) )
                _selectedNode = static_cast<int32>( nodeIndex );
        }

        _nodeGraph.applyContentFitIfNeeded();
        EditorNodeGraph::noteDrawnNodeCount( static_cast<uint32>( _editGraph._listNode.size() ) );
        _nodeGraph.endCanvas();
        ed::SetCurrentEditor( nullptr );
        ImGui::EndChild();
    }

    void AudioGraphPanel::drawInspector()
    {
        ImGui::BeginChild( "##AudioGraphInspector", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const Delegate<void( const PropertyInfo& )> onEdited = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &AudioGraphPanel::onGraphEdited, this );

        // 그래프 설정(출력 · 길이). 노드는 캔버스와 아래 노드 그리드가 고친다.
        EditorPropertyGridTarget graphTarget{};
        graphTarget._pInstance = &_editGraph;
        graphTarget._pType     = AudioGraphDef::StaticType();
        graphTarget._onEdited  = onEdited;
        ImGui::PushID( "##AudioGraphSettings" );
        _graphGrid.drawProperties( graphTarget, nullptr, { hashed_string( "_listNode" ) } );
        ImGui::PopID();

        const bool bSelected = 0 <= _selectedNode && _selectedNode < static_cast<int32>( _editGraph._listNode.size() );
        if ( bSelected )
        {
            AudioGraphNodeDesc& node = _editGraph._listNode[static_cast<size_t>( _selectedNode )];
            ImGui::SeparatorText( node._name.c_str() );
            if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "SetOutput" ) ) )
            {
                _editGraph._output = node._name;
                _bEditDirty        = true;
            }
            // 이름을 고치면 다른 노드의 입력 · 그래프 출력도 같은 이름을 따라간다(이름이 연결의 기준이다).
            const hashed_string      previousName = node._name;
            EditorPropertyGridTarget nodeTarget{};
            nodeTarget._pInstance = &node;
            nodeTarget._pType     = AudioGraphNodeDesc::StaticType();
            nodeTarget._onEdited  = onEdited;
            ImGui::PushID( "##AudioGraphNode" );
            _nodeGrid.drawProperties( nodeTarget, nullptr, { hashed_string( "_editorPosition" ) } );
            ImGui::PopID();
            if ( _editGraph._listNode[static_cast<size_t>( _selectedNode )]._name.isEqual( previousName, NameCase::CaseSensitive ) == false )
                AudioGraphEditUtil::replaceNodeReferences( _editGraph, previousName, _editGraph._listNode[static_cast<size_t>( _selectedNode )]._name );
        }
        else
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "SelectNodeHint" ) );
        }

        if ( ImGui::CollapsingHeader( SW_LOCKEY( "EditorAudioGraph", "ProcessingOrder" ) ) )
        {
            if ( _editGraph.makeProcessingOrder( _listOrder ) == false )
                EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "BrokenLinks" ) );
            else
            {
                for ( const uint32 nodeIndex : _listOrder )
                {
                    ImGui::TextUnformatted( _editGraph._listNode[nodeIndex]._name.c_str() );
                }
            }
        }
        ImGui::EndChild();
    }

    void AudioGraphPanel::drawContent()
    {
        ImGui::SetNextItemWidth( -120.0f * EditorThemeUtil::getDpiScale() );
        (void)ImGui::InputTextWithHint( "##AudioGraphPath", SW_LOCKEY( "EditorAudioGraph", "PathHint" ), _pathInput.data(), _pathInput.capacity() );
        ImGui::SameLine();
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Open" ) ) )
            loadGraph();
        if ( _editPath.empty() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorAudioGraph", "NoGraph" ) );
            return;
        }
        drawToolbar();
        const float32 canvasWidth = ImGui::GetContentRegionAvail().x * AudioGraphPanelInternal::kCanvasWidthRatio;
        drawCanvas( canvasWidth );
        ImGui::SameLine();
        drawInspector();
    }

    SW_EDITOR_PANEL( AudioGraphPanel, AudioGraphPanel::kPanelID, EditorPanelCategory::Tool, 2102 );
    SW_EDITOR_COMMAND( AudioGraphOpen, "window.audioGraph", 2232, "Audio Graph", editoricon::kSettings, "Audio", "소리 그래프 창을 엽니다(노드 · 연결 캔버스, 값 편집, 저장, 미리 듣기)",
                       "Open the audio graph editor", {}, &AudioGraphPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Window" );
} // namespace sw::editor
