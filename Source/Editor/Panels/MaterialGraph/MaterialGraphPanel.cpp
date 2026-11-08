#include "pch.h"

#include "Editor/Panels/MaterialGraph/MaterialGraphPanel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorCompactControls.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Material/Model/MaterialModel.h"
#include "Editor/Panels/Material/Model/MaterialPreviewInputs.h"

#include "Engine/Graphics/Material/Dev/MaterialGraph.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace sw::editor
{
    namespace
    {
        struct MaterialGraphPanelInternal
        {
            /** @brief 캔버스가 차지하는 폭의 비율입니다(나머지는 미리보기 · 값). */
            static constexpr float32 kCanvasWidthRatio = 0.68f;
            /** @brief 미리보기의 세로 / 가로 비율입니다. */
            static constexpr float32 kPreviewAspect = 0.75f;
            /** @brief 미리보기 구의 맞춤 반지름입니다(머티리얼 창과 같다). */
            static constexpr float32 kPreviewRadius = 0.75f;

            /** @brief 형식의 핀 색 번호입니다. `any` 는 색 표 밖이라 글자색이다. */
            [[nodiscard]] static uint32 toPinColorIndex( MaterialGraphValueType type )
            {
                const uint32 count  = static_cast<uint32>( type );
                const bool   bTyped = 1 <= count && count <= 4;
                return bTyped ? count - 1 : EditorNodeGraphRules::kWildcardPinType;
            }

            /** @brief 창을 엽니다(명령 표의 실행 함수). */
            static void openPanel()
            {
                (void)EditorPanelManager::getInEditorContext().setPanelOpen<MaterialGraphPanel>( true );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    namespace ed = ax::NodeEditor;

    MaterialGraphPanel::MaterialGraphPanel()
        : EditorPreviewPanel{ EditorAssetType{ MaterialGraphDef::kAssetType }, false }
        , _model{}
        , _nodeGraph{}
        , _listKind{}
        , _listMenu{}
        , _removal{}
    {
        _nodeGraph.setCommentsEnabled( true );
        // 색은 데이터(값의 형식)에만 쓴다 — float · float2 · float3 · float4 순서다.
        _nodeGraph.setPinTypeColors( { SW_THEME_COLOR( "Graph", "FlowPin" ), SW_THEME_COLOR( "Graph", "Sensor" ), SW_THEME_COLOR( "Graph", "Operator" ), SW_THEME_COLOR( "Graph", "Actuator" ) } );
        MaterialGraphNodeRegistry::collectKinds( _listKind );
        for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( _listKind.size() ); ++kindIndex )
        {
            _listMenu.push_back( EditorGraphNodeKind{ _listKind[kindIndex]->getName(), _listKind[kindIndex]->getCategory(), kindIndex } );
        }
    }

    ToolAssetLoadResult MaterialGraphPanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( _model.load( path ) == false )
            return ToolAssetLoadResult::Malformed;
        _nodeGraph.requestContentFit();
        return ToolAssetLoadResult::Loaded;
    }

    void MaterialGraphPanel::applyDocumentText( string_view text )
    {
        _model.restore( text );
        _nodeGraph.requestContentFit(); // 되돌린 위치를 캔버스에 다시 넣는다
    }

    bool MaterialGraphPanel::saveDocument()
    {
        const optional<AbsolutePath> diskPath = getDocumentDiskPath();
        if ( diskPath.has_value() == false || _model.saveDocument( *diskPath ) == false )
            return false;
        syncDocumentUndoBaseline();
        return true;
    }

    void MaterialGraphPanel::populatePreviewScene( Scene& scene )
    {
        _model.populatePreview( scene );
        getPreviewViewport().frameSphere( float3{ 0.0f, 0.0f, 0.0f }, MaterialGraphPanelInternal::kPreviewRadius );
    }

    bool MaterialGraphPanel::findPreviewFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        outCenter = float3{ 0.0f, 0.0f, 0.0f };
        outRadius = MaterialGraphPanelInternal::kPreviewRadius;
        return true;
    }

    bool MaterialGraphPanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo )
    {
        uint32 nodeIndex = 0;
        uint32 slot      = 0;
        bool   bInput    = false;
        if ( _model.tryDecodePin( pinID, nodeIndex, slot, bInput ) == false )
            return false;
        const MaterialGraphValueType type    = bInput ? _model.getInputPins( nodeIndex )[slot]._type : _model.getOutputPins( nodeIndex )[slot]._type;
        const bool                   bWidens = bInput == false && type == MaterialGraphValueType::Float;
        outInfo._type                        = bWidens ? EditorNodeGraphRules::kWildcardPinType : MaterialGraphPanelInternal::toPinColorIndex( type );
        outInfo._bInput                      = bInput;
        return true;
    }

    void MaterialGraphPanel::drawToolbar()
    {
        if ( EditorCompactControls::drawIconButton( "##materialGraphReload", SW_EDITOR_ICON( "Refresh" ), SW_LOCKEY( "EditorMaterialGraph", "ReloadTip" ) ) )
            reloadDocument();
        ImGui::SameLine();
        if ( EditorCompactControls::drawIconButton( "##materialGraphSave", SW_EDITOR_ICON( "Save" ), SW_LOCKEY( "EditorMaterialGraph", "SaveTip" ) ) )
            (void)saveDocumentAndClearDirty(); // 저장 경로는 모두 이것을 거친다 — 막힌 까닭은 문제 목록과 로그가 알린다
    }

    void MaterialGraphPanel::drawCanvasNode( uint32 nodeIndex )
    {
        const MaterialGraphNodeDef& node   = _model.getGraph()._listNode[nodeIndex];
        const int32                 nodeID = MaterialGraphModel::makeNodeID( nodeIndex );
        _nodeGraph.beginNode( nodeID );
        ImGui::TextUnformatted( node._name.c_str() );
        EditorThemeUtil::textMuted( node._kind.c_str() );
        ImGui::BeginGroup();
        const vector<MaterialGraphPinInfo>& listInput = _model.getInputPins( nodeIndex );
        for ( uint32 slot = 0; slot < static_cast<uint32>( listInput.size() ); ++slot )
        {
            _nodeGraph.beginPin( MaterialGraphModel::makeInputPinID( nodeIndex, slot ), true );
            _nodeGraph.drawPinIcon( MaterialGraphPanelInternal::toPinColorIndex( listInput[slot]._type ), node.findLink( listInput[slot]._name ) != nullptr );
            ImGui::SameLine();
            ImGui::TextUnformatted( listInput[slot]._name.c_str() );
            _nodeGraph.endPin();
        }
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        const vector<MaterialGraphPinInfo>& listOutput = _model.getOutputPins( nodeIndex );
        for ( uint32 slot = 0; slot < static_cast<uint32>( listOutput.size() ); ++slot )
        {
            _nodeGraph.beginPin( MaterialGraphModel::makeOutputPinID( nodeIndex, slot ), false );
            ImGui::TextUnformatted( listOutput[slot]._name.c_str() );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( MaterialGraphPanelInternal::toPinColorIndex( listOutput[slot]._type ), true );
            _nodeGraph.endPin();
        }
        ImGui::EndGroup();
        _nodeGraph.endNode();

        if ( _nodeGraph.needsContentFit() || _model.isPlacingNode( nodeIndex ) )
            ed::SetNodePosition( toNodeID( nodeID ), ImVec2( node._position._x, node._position._y ) );
    }

    void MaterialGraphPanel::applyCanvasDeletions()
    {
        _nodeGraph.collectRemovals( _removal );
        if ( EditorNodeGraph::removeComments( _model.editGraph()._listComment, _removal._listCommentID ) )
            _model.markCommentsEdited( "Delete Material Graph Comment" );
        for ( const int32 linkID : _removal._listLinkID )
        {
            (void)_model.disconnectInputPin( linkID ); // 같은 프레임에 지운 노드의 링크면 이미 없다
        }
        _model.removeNodes( _removal._listNodeID );
    }

    void MaterialGraphPanel::drawCanvas( float32 canvasWidth )
    {
        ImGui::BeginChild( "##MaterialGraphCanvasRegion", ImVec2{ canvasWidth, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        if ( _nodeGraph.beginCanvas( "MaterialGraphCanvas", EditorPaths::kMaterialGraphCanvasFileName, "materialGraph" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EditorMaterialGraph", "CanvasFailed" ) );
            ImGui::EndChild();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );
        _model.beginCanvasFrame();

        vector<GraphCommentBox>& listComment = _model.editGraph()._listComment;
        if ( _nodeGraph.drawComments( listComment, _nodeGraph.needsContentFit() ) )
            _model.markCommentsEdited( "Rename Material Graph Comment" );
        if ( _nodeGraph.addRequestedComment( listComment ) )
            _model.markCommentsEdited( "Add Material Graph Comment" );

        const MaterialGraphDef& graph     = _model.getGraph();
        const uint32            nodeCount = static_cast<uint32>( graph._listNode.size() );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            drawCanvasNode( nodeIndex );
        }
        // 링크는 받는 노드의 입력 목록을 그린 것이다. 링크 id 는 입력 핀 id 다(입력 핀 하나에 링크 하나).
        for ( uint32 readerIndex = 0; readerIndex < nodeCount; ++readerIndex )
        {
            const vector<MaterialGraphPinInfo>& listInput = _model.getInputPins( readerIndex );
            for ( uint32 slot = 0; slot < static_cast<uint32>( listInput.size() ); ++slot )
            {
                const MaterialGraphLink* pLink = graph._listNode[readerIndex].findLink( listInput[slot]._name );
                const optional<uint32>   from  = pLink != nullptr ? graph.findNodeIndex( pLink->_fromNode ) : nullopt;
                if ( from.has_value() == false )
                    continue;
                const vector<MaterialGraphPinInfo>& listOutput = _model.getOutputPins( *from );
                for ( uint32 outputSlot = 0; outputSlot < static_cast<uint32>( listOutput.size() ); ++outputSlot )
                {
                    if ( listOutput[outputSlot]._name != pLink->_fromPin )
                        continue;
                    const int32 inputPin = MaterialGraphModel::makeInputPinID( readerIndex, slot );
                    ed::Link( toLinkID( inputPin ), toPinID( MaterialGraphModel::makeOutputPinID( *from, outputSlot ) ), toPinID( inputPin ) );
                }
            }
        }

        vector<EditorGraphNodeIssue> listIssue;
        for ( const MaterialGraphIssue& issue : _model.getIssues() )
        {
            // 하위 그래프 안의 문제(`바깥/안`)는 바깥 노드에 붙인다.
            const string_view      outerName{ string_view( issue._nodeName ).substr( 0, issue._nodeName.find( '/' ) ) };
            const optional<uint32> nodeIndex = graph.findNodeIndex( outerName );
            if ( nodeIndex.has_value() )
                listIssue.push_back( EditorGraphNodeIssue{ MaterialGraphModel::makeNodeID( *nodeIndex ), issue._message } );
        }
        _nodeGraph.setNodeIssues( std::move( listIssue ) );
        _nodeGraph.drawNodeIssues();

        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &MaterialGraphPanel::findGraphPin, this ), fromPin, toPin ) )
            (void)_model.connectPins( fromPin, toPin ); // 고리는 그리지 않는다

        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( _listMenu, kindID, canvasPosition ) )
            (void)_model.addNode( _listKind[kindID]->getName(), canvasPosition );

        applyCanvasDeletions();

        if ( _nodeGraph.needsContentFit() == false )
        {
            const uint32 movedNodeCount = static_cast<uint32>( graph._listNode.size() );
            for ( uint32 nodeIndex = 0; nodeIndex < movedNodeCount; ++nodeIndex )
            {
                const ImVec2 position = ed::GetNodePosition( toNodeID( MaterialGraphModel::makeNodeID( nodeIndex ) ) );
                _model.recordNodePosition( nodeIndex, float2{ position.x, position.y } );
            }
            if ( _nodeGraph.syncCommentLayout( _model.editGraph()._listComment ) )
                _model.markCommentsEdited( "Move Material Graph Comment", "materialgraph.comment.move" );
        }

        int32 selectedNodeID{ 0 };
        if ( _nodeGraph.findSelectedNode( selectedNodeID ) )
            _model.selectNodeByID( selectedNodeID );

        _nodeGraph.finishCanvas( static_cast<uint32>( graph._listNode.size() ) );
        ed::SetCurrentEditor( nullptr );
        ImGui::EndChild();
    }

    void MaterialGraphPanel::drawValueRow( uint32 nodeIndex, const utf8* pName, MaterialGraphValueType type, string_view defaultText, bool bColor )
    {
        ImGui::PushID( pName );
        const string text = _model.readValue( nodeIndex, pName, defaultText );
        float32      arrValue[4]{};
        bool         bRead    = false;
        bool         bChanged = false;
        uint32       count    = 0;
        switch ( type )
        {
            case MaterialGraphValueType::Float:
            {
                float32 arrOne[1]{};
                bRead = MaterialPreviewInputs::parseFloats( text, arrOne );
                if ( bRead )
                {
                    EditorWidgets::drawPropertyRowLabel( pName );
                    arrValue[0] = arrOne[0];
                    bChanged    = ImGui::DragFloat( "##value", arrValue, 0.01f );
                }
                count = 1;
                break;
            }
            case MaterialGraphValueType::Float2:
            {
                float32 arrTwo[2]{};
                bRead = MaterialPreviewInputs::parseFloats( text, arrTwo );
                if ( bRead )
                {
                    EditorWidgets::drawPropertyRowLabel( pName );
                    arrValue[0] = arrTwo[0];
                    arrValue[1] = arrTwo[1];
                    bChanged    = ImGui::DragFloat2( "##value", arrValue, 0.01f );
                }
                count = 2;
                break;
            }
            case MaterialGraphValueType::Float3:
            {
                float32 arrThree[3]{};
                bRead = MaterialPreviewInputs::parseFloats( text, arrThree );
                if ( bRead )
                {
                    EditorWidgets::drawPropertyRowLabel( pName );
                    arrValue[0] = arrThree[0];
                    arrValue[1] = arrThree[1];
                    arrValue[2] = arrThree[2];
                    bChanged    = ImGui::DragFloat3( "##value", arrValue, 0.01f );
                }
                count = 3;
                break;
            }
            case MaterialGraphValueType::Float4:
            {
                bRead = MaterialPreviewInputs::parseFloats( text, arrValue );
                if ( bRead && bColor )
                {
                    Color color{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
                    bChanged = EditorWidgets::drawColorEdit( pName, color );
                    if ( bChanged )
                    {
                        arrValue[0] = color._r;
                        arrValue[1] = color._g;
                        arrValue[2] = color._b;
                        arrValue[3] = color._a;
                    }
                }
                else if ( bRead )
                {
                    EditorWidgets::drawPropertyRowLabel( pName );
                    bChanged = ImGui::DragFloat4( "##value", arrValue, 0.01f );
                }
                count = 4;
                break;
            }
            case MaterialGraphValueType::Any:
            case MaterialGraphValueType::String:
            case MaterialGraphValueType::Invalid:
                break;
        }
        if ( bChanged )
        {
            string written;
            MaterialModel::writeFloats( written, arrValue, count );
            _model.setValue( nodeIndex, pName, written );
        }
        if ( bRead == false )
        {
            // 글 칸: 이름 설정이거나, 형식대로 읽지 못한 값이다 — 0 으로 메우지 않고 원문을 보여 고치게 한다.
            string edited = text;
            if ( EditorWidgets::drawTextField( pName, edited ) )
                _model.setValue( nodeIndex, pName, edited );
        }
        ImGui::PopID();
    }

    void MaterialGraphPanel::drawInspector()
    {
        const int32 selected = _model.getSelectedNode();
        if ( selected < 0 )
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorMaterialGraph", "SelectNodeHint" ) );
            return;
        }
        const uint32                 nodeIndex = static_cast<uint32>( selected );
        const MaterialGraphNodeDef&  node      = _model.getGraph()._listNode[nodeIndex];
        const MaterialGraphNodeKind* pKind     = MaterialGraphNodeRegistry::find( node._kind );
        ImGui::SeparatorText( node._name.c_str() );
        if ( pKind == nullptr )
            return;
        const bool bColorParameter = pKind->getParameterPropertyType() == "Color";
        for ( uint32 index = 0; index < pKind->getSettingCount(); ++index )
        {
            const MaterialGraphSettingDecl& setting = pKind->getSetting( index );
            const string                    name{ setting._name };
            drawValueRow( nodeIndex, name.c_str(), setting._type, setting._default, bColorParameter );
        }
        const bool bTextureNode = pKind->getParameterPropertyType() == "Texture2D";
        const bool bSubgraph    = pKind->getRole() == MaterialGraphNodeRole::Subgraph || pKind->getRole() == MaterialGraphNodeRole::Layer; // 층도 그래프 파일을 가리킨다
        if ( bTextureNode || bSubgraph )
        {
            string     assetPath = node._assetPath;
            const bool bChanged  = EditorWidgets::drawAssetSlot( SW_LOCKEY( "EditorMaterialGraph", "AssetSlot" ), assetPath, bSubgraph ? MaterialGraphDef::kExtension.data() : ".png" );
            if ( bChanged )
                _model.setAssetPath( nodeIndex, assetPath );
        }
        // 잇지 않은 입력 핀의 상수 — 언리얼 노드의 인라인 값과 같은 자리다.
        for ( uint32 slot = 0; slot < pKind->getInputCount(); ++slot )
        {
            const MaterialGraphPinDecl& pin = pKind->getInput( slot );
            if ( node.findLink( pin._name ) != nullptr )
                continue;
            const string name{ pin._name };
            // `any` 핀은 float 로 시작한다. 기본값이 식이면(메시 UV 등) 글 칸이 그 식을 보인다.
            const MaterialGraphValueType rowType = pin._type == MaterialGraphValueType::Any ? MaterialGraphValueType::Float : pin._type;
            drawValueRow( nodeIndex, name.c_str(), rowType, pin._default.empty() ? string_view{ "0" } : pin._default, false );
        }
    }

    void MaterialGraphPanel::drawIssues()
    {
        if ( _model.isSubgraphDocument() )
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorMaterialGraph", "SubgraphStatus" ) );
            return;
        }
        const vector<MaterialGraphIssue>& listIssue = _model.getIssues();
        if ( listIssue.empty() )
        {
            EditorThemeUtil::textMuted( SW_LOCKEY( "EditorMaterialGraph", "CompiledStatus" ) );
            return;
        }
        for ( const MaterialGraphIssue& issue : listIssue )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", issue._nodeName.c_str() );
            ImGui::TextWrapped( "%s", issue._message.c_str() );
        }
    }

    void MaterialGraphPanel::drawSidePanel()
    {
        ImGui::BeginChild( "##MaterialGraphSide", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_Borders );
        const float32 width = ImGui::GetContentRegionAvail().x;
        drawPreviewViewport( float2{ width, width * MaterialGraphPanelInternal::kPreviewAspect }, false ); // 기즈모가 없다
        drawInspector();
        ImGui::Separator();
        drawIssues();
        ImGui::EndChild();
    }

    void MaterialGraphPanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        beginPreviewFrame();
        drawDocumentOpenBar( kPanelID );
        if ( getLoadedAssetPath().empty() || isDocumentLoadFailed() )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorMaterialGraph", "NoGraph" ) );
            return;
        }
        drawToolbar();
        _model.refreshCompile( getLoadedAssetPath() );
        const float32 canvasWidth = ImGui::GetContentRegionAvail().x * MaterialGraphPanelInternal::kCanvasWidthRatio;
        drawCanvas( canvasWidth );
        ImGui::SameLine();
        drawSidePanel();
        _model.refreshCompile( getLoadedAssetPath() ); // 이번 그리기의 편집을 미리보기가 바로 본다
        _model.refreshPreview( getPreviewScene(), getLoadedAssetPath() );
        MaterialGraphEdit edit;
        if ( _model.takeEdit( edit ) )
            notifyDocumentEdited( edit._pLabel, edit._pCoalesceKey != nullptr ? string_view{ edit._pCoalesceKey } : string_view{} );
    }

    SW_EDITOR_PANEL( MaterialGraphPanel, MaterialGraphPanel::kPanelID, EditorPanelCategory::Tool, 1301 );
    SW_EDITOR_COMMAND( MaterialGraphOpen, "window.materialGraph", &MaterialGraphPanelInternal::openPanel, nullptr, NoScene );
} // namespace sw::editor
