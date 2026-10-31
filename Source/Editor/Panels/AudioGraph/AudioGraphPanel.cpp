#include "pch.h"

#include "Editor/Panels/AudioGraph/AudioGraphPanel.h"

#include "Core/Log/LogOnce.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
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

            /** @brief 창을 엽니다(명령 표의 실행 함수). */
            static void openPanel()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                (void)editorContext.getSubsystem<EditorPanelManager>().setPanelOpen<AudioGraphPanel>( true ); // 패널은 이 파일에 등록되어 있다
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace ed = ax::NodeEditor;

    AudioGraphPanel::AudioGraphPanel()
        : IEditorPanel( false )
        , _model{}
        , _nodeGraph{}
        , _graphGrid{}
        , _nodeGrid{}
        , _listLinkID{}
        , _listNodeID{}
        , _pathInput{}
    {
    }

    void AudioGraphPanel::loadGraph()
    {
        if ( _model.load( string_view{ _pathInput.c_str() } ) )
            _nodeGraph.requestContentFit();
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
        ImGui::BeginDisabled( _model.isDirty() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Save" ) ) )
            _model.save();
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "SaveTip" ) );

        IAudioService& audio       = editor::getSubsystem<IAudioService>(); // 전용 서버도 널 서비스가 있어 늘 있다
        const bool     bAudioReady = audio.isInitialized();
        if ( bAudioReady )
            _model.refreshPreview( audio.getEngine() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) );
        ImGui::BeginDisabled( bAudioReady == false || _model.isDirty() );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Preview" ) ) )
            _model.playPreview( audio.getEngine() );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorAudioGraph", "PreviewTip" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) );
        ImGui::BeginDisabled( bAudioReady == false || _model.isPreviewPlaying() == false );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "StopPreview" ) ) )
            _model.stopPreview( audio.getEngine() );
        ImGui::EndDisabled();
        if ( _model.isDirty() )
        {
            ImGui::SameLine();
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorAudioGraph", "Unsaved" ) );
        }
    }

    void AudioGraphPanel::drawCanvasNode( uint32 nodeIndex )
    {
        const AudioGraphDef&          graph    = _model.getGraph();
        const AudioGraphNodeDesc&     node     = graph._listNode[nodeIndex];
        const AudioGraphNodeTypeInfo& typeInfo = AudioGraphRegistry::getNodeType( node._type );
        const int32                   nodeID   = AudioGraphEditUtil::makeNodeID( nodeIndex );
        ed::BeginNode( toNodeID( nodeID ) );
        ImGui::TextUnformatted( node._name.c_str() );
        ImGui::TextDisabled( "%s%s", typeInfo._pName, node._name == graph._output ? "  [OUT]" : "" );
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

        const bool bPlaceNow = _nodeGraph.needsContentFit() || _model.isPlacingNode( nodeIndex );
        if ( bPlaceNow )
            ed::SetNodePosition( toNodeID( nodeID ), ImVec2( node._editorPosition._x, node._editorPosition._y ) );
    }

    void AudioGraphPanel::applyCanvasDeletions()
    {
        // 캔버스가 지운 id 를 모두 모은 뒤에 모델이 지운다(묻는 동안 그래프를 고치지 않는다). 버퍼는 멤버로 두어 프레임마다 할당하지 않는다.
        _listLinkID.clear();
        _listNodeID.clear();
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkID;
            while ( ed::QueryDeletedLink( &linkID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    _listLinkID.push_back( static_cast<int32>( linkID.Get() ) );
            }
            ed::NodeId nodeID;
            while ( ed::QueryDeletedNode( &nodeID ) )
            {
                if ( ed::AcceptDeletedItem() )
                    _listNodeID.push_back( static_cast<int32>( nodeID.Get() ) );
            }
        }
        ed::EndDelete();
        _model.applyDeletions( _listLinkID, _listNodeID );
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
                const int32 fromIndex = graph.findNodeIndex( listInput[inputSlot] );
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
            (void)_model.connectPins( fromPin, toPin ); // 규칙이 거절한 링크는 그리지 않는다

        // 빈 곳 오른쪽 클릭: 노드 종류 검색. 고른 노드는 그 자리에 놓는다.
        vector<EditorGraphNodeKind> listKind;
        for ( uint32 typeIndex = 0; typeIndex <= static_cast<uint32>( AudioGraphNodeType::Gain ); ++typeIndex )
        {
            const AudioGraphNodeTypeInfo& typeInfo = AudioGraphRegistry::getNodeType( static_cast<AudioGraphNodeType>( typeIndex ) );
            listKind.push_back( EditorGraphNodeKind{ typeInfo._pName, typeInfo._bSource ? "Sources" : "Processors", typeIndex } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) )
            (void)_model.addNode( static_cast<AudioGraphNodeType>( kindID ), canvasPosition ); // 노드 수 한도면 넣지 않는다

        applyCanvasDeletions();

        // 끌어서 옮긴 노드의 위치를 데이터에 싣는다(맞추기 프레임은 데이터를 캔버스에 넣는 쪽이라 건너뛴다).
        // 지우기 뒤라 노드 수를 다시 읽는다.
        if ( _nodeGraph.needsContentFit() == false )
        {
            const uint32 movedNodeCount = static_cast<uint32>( graph._listNode.size() );
            for ( uint32 nodeIndex = 0; nodeIndex < movedNodeCount; ++nodeIndex )
            {
                const ImVec2 position = ed::GetNodePosition( toNodeID( AudioGraphEditUtil::makeNodeID( nodeIndex ) ) );
                _model.noteNodePosition( nodeIndex, float2{ position.x, position.y } );
            }
        }

        ed::NodeId arrSelected[1];
        if ( ed::GetSelectedNodes( arrSelected, 1 ) > 0 )
            _model.selectNodeByID( static_cast<int32>( arrSelected[0].Get() ) );

        _nodeGraph.applyContentFitIfNeeded();
        EditorNodeGraph::noteDrawnNodeCount( static_cast<uint32>( graph._listNode.size() ) );
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
        graphTarget._pInstance = &_model.editGraph();
        graphTarget._pType     = AudioGraphDef::StaticType();
        graphTarget._onEdited  = onEdited;
        ImGui::PushID( "##AudioGraphSettings" );
        _graphGrid.drawProperties( graphTarget, nullptr, { hashed_string( "_listNode" ) } );
        ImGui::PopID();

        if ( AudioGraphNodeDesc* pNode = _model.editSelectedNode(); pNode != nullptr )
        {
            ImGui::SeparatorText( pNode->_name.c_str() );
            if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "SetOutput" ) ) )
                _model.setSelectedAsOutput();
            // 이름을 고치면 다른 노드의 입력 · 그래프 출력도 같은 이름을 따라간다(이름이 연결의 기준이다).
            const hashed_string      previousName = pNode->_name;
            EditorPropertyGridTarget nodeTarget{};
            nodeTarget._pInstance = pNode;
            nodeTarget._pType     = AudioGraphNodeDesc::StaticType();
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

    void AudioGraphPanel::drawContent()
    {
        EditorChrome::setNextItemWidthColumns( -16.0f );
        (void)ImGui::InputTextWithHint( "##AudioGraphPath", SW_LOCKEY( "EditorAudioGraph", "PathHint" ), _pathInput.data(), _pathInput.capacity() );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EditorAudioGraph", "Open" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EditorAudioGraph", "Open" ) ) )
            loadGraph();
        if ( _model.hasGraph() == false )
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
                       "Open the audio graph editor", nullptr, &AudioGraphPanelInternal::openPanel, nullptr, NoScene, "MainMenu/Window", None, 0 );
} // namespace sw::editor
