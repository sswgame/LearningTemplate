#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreePanel.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Runtime/Character/AI/IBehaviorTreeDebugSource.h"
#include "GameFramework/Runtime/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include <imgui.h>
#include <imgui-node-editor/imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        struct BehaviorTreePanelInternal
        {
            static constexpr const utf8* kCanvasID           = "BehaviorTreeCanvas";
            static constexpr string_view kClipboardKind      = "BehaviorTree"; ///< 그래프 복사 버퍼의 종류 열쇠 — 다른 그래프에는 붙지 않는다
            static constexpr const utf8* kCanvasSettingsFile = "BehaviorTreeCanvas.json";
            static constexpr float32     kSidePanelWidth     = 340.0f; ///< 96 DPI 기준입니다
            static constexpr float32     kColumnSpacing      = 260.0f; ///< 깊이마다 오른쪽으로 벌리는 간격(캔버스 좌표)
            static constexpr float32     kRowSpacing         = 110.0f; ///< 잎 하나마다 아래로 벌리는 간격(캔버스 좌표)

            // 핀 번호 = 노드 번호 × 2 + (나가는 핀이면 1). 노드 하나에 들어오는 핀(부모 쪽)과 나가는 핀(자식 쪽)이 하나씩이다.
            static int32 makeInputPin( int32 nodeID ) { return nodeID * 2; }
            static int32 makeOutputPin( int32 nodeID ) { return nodeID * 2 + 1; }
            static int32 decodeNode( int32 pinID ) { return pinID / 2; }
            static bool  isOutputPin( int32 pinID ) { return pinID % 2 == 1; }

            static ImVec4 getTypeColor( BehaviorNodeKind type )
            {
                if ( BehaviorTreeNames::isLeaf( type ) )
                    return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Warning" ) );
                if ( BehaviorTreeNames::isDecorator( type ) )
                    return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Base", "Accent" ) );
                return EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Info" ) );
            }

            static const utf8* getCategoryName( BehaviorNodeKind type )
            {
                if ( BehaviorTreeNames::isLeaf( type ) )
                    return "Leaf";
                if ( BehaviorTreeNames::isDecorator( type ) )
                    return "Decorator";
                return "Composite";
            }

            static constexpr uint32 kNodeTypeCount = static_cast<uint32>( BehaviorNodeKind::SubTree ) + 1;
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_PANEL( BehaviorTreePanel, BehaviorTreePanel::kPanelID, EditorPanelCategory::Custom, 9320 );

    BehaviorTreePanel::BehaviorTreePanel()
        : EditorDocumentPanel{ EditorAssetType{ EditorAssetTypeIDs::kData }, false }
        , _model{}
        , _nodeGraph{}
        , _listIssue{}
        , _listPlacement{}
        , _listPreorder{}
        , _listTaskName{}
        , _grid{}
        , _statusText{}
        , _selectedID{ -1 }
        , _bLayoutPending{ SW_TRUE }
        , _bSelectPending{ SW_FALSE }
        , _bGridEdited{ SW_FALSE }
    {
        _nodeGraph.setPinTypeColors( { SW_THEME_COLOR( "Base", "Text" ) } ); // 부모 → 자식 흐름 핀 하나
        _nodeGraph.setAutoLayout( true );                                    // 노드 자리는 트리 배치(layoutTree)가 정한다 — 정렬 메뉴를 감춘다(언리얼 BT 에디터도 자동 배치)
    }

    void BehaviorTreePanel::shutdown()
    {
        _nodeGraph.shutdown();
    }

    bool BehaviorTreePanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, BehaviorTreeAsset::kExtension );
    }

    ToolAssetLoadResult BehaviorTreePanel::loadDocument()
    {
        const string path = acceptDocumentToLoad();
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;

        _model.clear();
        _selectedID     = -1;
        _bLayoutPending = SW_TRUE;
        _nodeGraph.requestContentFit();
        const auto diskPath = getDocumentDiskPath();
        string     text;
        if ( diskPath.has_value() == false || FileUtil::readTextFile( diskPath->str(), text ) == false )
        {
            _statusText = "The file could not be read";
            SW_LOG_ERROR_ID( "BehaviorTreePanel.BehaviorTreeFileCould", "Behavior tree file '%#' could not be read", path.c_str() );
            return ToolAssetLoadResult::Malformed;
        }
        if ( const expected<void> parsed = _model.parse( text ); parsed.has_value() == false )
        {
            // 읽지 못한 파일은 덮어쓰지 않도록 저장을 막는다(Malformed).
            _statusText = EditorWidgets::makeStatusText( parsed.error() );
            SW_LOG_ERROR_ID( "BehaviorTreePanel.BehaviorTreeFileCould2", "Behavior tree file '%#' could not be opened: %# (%#)", path.c_str(), parsed.error().message(), parsed.error().detail() );
            return ToolAssetLoadResult::Malformed;
        }
        _statusText = "Loaded";
        return ToolAssetLoadResult::Loaded;
    }

    bool BehaviorTreePanel::saveDocument()
    {
        const string& path = getLoadedAssetPath();
        if ( path.empty() )
            return false;
        const auto diskPath = getDocumentDiskPath(); // 읽은 그 자리
        if ( diskPath.has_value() == false )
            return false; // 이유는 toDiskPath 가 남겼다
        _model.collectIssues( _listIssue );
        if ( _listIssue.empty() == false )
            SW_LOG_WARNING_ID( "BehaviorTreePanel.SavingBehaviorTreeProblems", "Saving behavior tree '%#' with %# problems - the game will not load it until they are fixed", path.c_str(), _listIssue.size() );
        const expected<string> saveText = _model.makeSaveText(); // 변형(`_base`)은 부모와의 차이로 쓴다
        if ( saveText.has_value() == false )
        {
            _statusText = EditorWidgets::makeStatusText( saveText.error() );
            SW_LOG_ERROR_ID( "BehaviorTreePanel.BehaviorTreeVariantNotSaved", "Behavior tree '%#' was not saved: %# (%#)", path.c_str(), saveText.error().message(), saveText.error().detail() );
            return false;
        }
        if ( ResourceComposer::saveText( diskPath->str(), *saveText ) == false )
        {
            _statusText = "The file could not be written";
            SW_LOG_ERROR_ID( "BehaviorTreePanel.BehaviorTreeFileCould3", "Behavior tree file '%#' could not be written", diskPath->c_str() );
            return false;
        }
        clearDocumentDirty();
        syncDocumentUndoBaseline();
        _statusText = _listIssue.empty() ? "Saved" : "Saved with problems - the game will not load this tree";
        return true;
    }

    string BehaviorTreePanel::captureDocumentText() const
    {
        return _model.write();
    }

    void BehaviorTreePanel::applyDocumentText( string_view text )
    {
        const string baseID = _model.getBaseID(); // 되돌리기 글은 펼친 한 벌이다 — 문서의 부모는 그대로 둬야 저장이 차이로 쓴다
        if ( const expected<void> parsed = _model.parse( text ); parsed.has_value() == false )
        {
            // 되돌리기 글은 이 창이 쓴 XML 이다. 읽지 못하면 결함이므로 알리고 지금 모델을 둔다.
            SW_LOG_WARNING_ID( "BehaviorTreePanel.BehaviorTreeUndoSnapshot", "Behavior tree undo snapshot could not be read: %# (%#)", parsed.error().message(), parsed.error().detail() );
            return;
        }
        _model.setBaseID( baseID );
        _selectedID     = -1;
        _bLayoutPending = SW_TRUE;
        _nodeGraph.requestContentFit();
    }

    void BehaviorTreePanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawToolbar();

        if ( getLoadedAssetPath().empty() )
        {
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "OpenABehaviorTreeFileBtXmlWith" ) );
            return;
        }
        if ( isDocumentLoadFailed() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "ThisFileCouldNot", TextArgumentList().addText( "statusText", _statusText.c_str() ) ).c_str() );
            return;
        }

        _model.collectIssues( _listIssue );
        _model.collectPreorder( _listPreorder );
        drawCanvasArea();
        ImGui::SameLine();
        drawSidePanel();
    }

    void BehaviorTreePanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##BehaviorTreeToolbar" ) )
        {
            drawDocumentOpenBar( "behaviorTree" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Save" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Save" ) ) )
                (void)saveDocument(); // 실패는 상태 줄과 로그가 알린다
            EditorSelfTestMarks::record( "behaviorTree.save" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Reload" ) ) );
            if ( ImGui::Button( SW_LOCKEY( "EdPlugin", "Reload" ) ) )
                reloadDocument();
            ImGui::SameLine();
            const bool bEditable = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
            if ( bEditable )
            {
                if ( ImGui::BeginCombo( "##behaviorTree.add", "Add node..." ) )
                {
                    for ( uint32 index = 0; index < BehaviorTreePanelInternal::kNodeTypeCount; ++index )
                    {
                        const BehaviorNodeKind type = static_cast<BehaviorNodeKind>( index );
                        if ( ImGui::Selectable( BehaviorTreeNames::toString( type ) ) )
                            (void)addNodeFromSelection( type ); // 거절하면 상태 줄이 이유를 알린다
                    }
                    ImGui::EndCombo();
                }
                EditorSelfTestMarks::record( "behaviorTree.add" );
                ImGui::SameLine();
                const bool bHasSelection = _model.find( _selectedID ) != nullptr;
                const bool bMoveUp       = ImGui::Button( SW_LOCKEY( "EdPlugin", "Up" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Down" ) ) );
                const bool bMoveDown = ImGui::Button( SW_LOCKEY( "EdPlugin", "Down" ) );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdPlugin", "Delete" ) ) );
                const bool bDelete = ImGui::Button( SW_LOCKEY( "EdPlugin", "Delete" ) );
                if ( bHasSelection )
                {
                    if ( bMoveUp && _model.moveChild( _selectedID, -1 ) )
                        markStructureChanged( "Move Behavior Node" );
                    if ( bMoveDown && _model.moveChild( _selectedID, 1 ) )
                        markStructureChanged( "Move Behavior Node" );
                    if ( bDelete && _model.removeNode( _selectedID ) )
                    {
                        _selectedID = -1;
                        markStructureChanged( "Delete Behavior Node" );
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "NodesProblems", TextArgumentList().addInteger( "nodeCount", static_cast<int64>( _model.getNodeCount() ) ).addInteger( "issueCount", static_cast<int64>( _listIssue.size() ) ) ).c_str() );
            }
            if ( _statusText.empty() == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "| %s", _statusText.c_str() );
            }
        }
        EditorChrome::endToolbar();
    }

    void BehaviorTreePanel::drawCanvasArea()
    {
        const float32     sideWidth = BehaviorTreePanelInternal::kSidePanelWidth * EditorThemeUtil::getDPIScale();
        EditorSectionDesc canvasDesc{};
        canvasDesc._pID       = "BehaviorTreeCanvasRegion";
        canvasDesc._kind      = EditorSectionKind::Child;
        canvasDesc._childSize = float2{ ImGui::GetContentRegionAvail().x - sideWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f };
        canvasDesc._flags     = EditorSectionFlags::NoScrollbar | EditorSectionFlags::NoScrollWithMouse;
        EditorChrome::beginSection( canvasDesc );

        string                    liveRunnerName;
        const BehaviorTreeRunner* pRunner = findLiveRunner( liveRunnerName );
        if ( liveRunnerName.empty() == false )
            ImGui::TextDisabled( "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "Live", TextArgumentList().addText( "liveRunnerName", liveRunnerName.c_str() ) ).c_str() );

        // 확장 모듈은 imgui-node-editor 를 자기 사본으로 링크하므로, 캔버스를 연 뒤 이 사본에도 지금 편집기를 건다(`EditorGraphDocumentPanel::beginGraphCanvas` 와 같다).
        if ( _nodeGraph.beginCanvas( BehaviorTreePanelInternal::kCanvasID, BehaviorTreePanelInternal::kCanvasSettingsFile, "behaviorTree" ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "TheNodeEditorCouldNotBeCreated" ) );
            EditorChrome::endSection();
            return;
        }
        ed::SetCurrentEditor( _nodeGraph.getContext() );

        for ( size_t index = 0; index < _listPreorder.size(); ++index )
        {
            const BehaviorTreeEditNode* pNode = _model.find( _listPreorder[index] );
            if ( pNode == nullptr )
                continue;
            const bool bActive = pRunner != nullptr && pRunner->isNodeActive( static_cast<int32>( index ) );
            drawNode( *pNode, bActive );
        }
        for ( const int32 id : _listPreorder )
        {
            const BehaviorTreeEditNode* pNode = _model.find( id );
            if ( pNode == nullptr || pNode->_parent < 0 )
                continue;
            ed::Link( toLinkID( id ), toPinID( BehaviorTreePanelInternal::makeOutputPin( pNode->_parent ) ), toPinID( BehaviorTreePanelInternal::makeInputPin( id ) ) );
        }

        if ( _bLayoutPending )
        {
            layoutTree();
            for ( const Placement& placement : _listPlacement )
            {
                ed::SetNodePosition( toNodeID( placement._id ), ImVec2{ placement._position._x, placement._position._y } );
            }
            _bLayoutPending = SW_FALSE;
            _nodeGraph.requestContentFit();
        }
        if ( _bSelectPending )
        {
            ed::SelectNode( toNodeID( _selectedID ), false );
            _bSelectPending = SW_FALSE;
        }

        vector<EditorGraphNodeIssue> listNodeIssue;
        for ( const BehaviorTreeEditIssue& issue : _listIssue )
        {
            if ( issue._nodeID.has_value() == false )
                continue;
            EditorGraphNodeIssue nodeIssue;
            nodeIssue._nodeID  = *issue._nodeID;
            nodeIssue._message = issue._message;
            listNodeIssue.push_back( std::move( nodeIssue ) );
        }
        _nodeGraph.setNodeIssues( std::move( listNodeIssue ) );
        _nodeGraph.drawNodeIssues();

        handleCanvasInteractions();

        if ( _nodeGraph.findSelectedNode( _selectedID ) == false )
            _selectedID = -1;

        _nodeGraph.finishCanvas( _model.getNodeCount() );
        ed::SetCurrentEditor( nullptr );
        EditorChrome::endSection();
    }

    void BehaviorTreePanel::drawNode( const BehaviorTreeEditNode& node, bool bActive )
    {
        using Internal = BehaviorTreePanelInternal;
        _nodeGraph.beginNode( node._id );
        if ( node._parent >= 0 )
        {
            _nodeGraph.beginPin( Internal::makeInputPin( node._id ), true );
            _nodeGraph.drawPinIcon( 0, true );
            _nodeGraph.endPin();
            ImGui::SameLine();
        }
        ImGui::TextColored( Internal::getTypeColor( node._type ), "%s", BehaviorTreeNames::toString( node._type ) );
        const string display = _model.makeDisplayName( node._id );
        if ( display != BehaviorTreeNames::toString( node._type ) )
            ImGui::TextUnformatted( display.c_str() );
        // 캔버스의 한 줄 요약. 칸은 종류의 스키마 값에 있다.
        if ( const BehaviorConditionDecoratorParams* pDecorator = node._params.find<BehaviorConditionDecoratorParams>(); pDecorator != nullptr )
            ImGui::TextDisabled( "%s", pDecorator->_test.c_str() );
        else if ( const BehaviorConditionParams* pCondition = node._params.find<BehaviorConditionParams>(); pCondition != nullptr )
            ImGui::TextDisabled( "%s", pCondition->_test.c_str() );
        else if ( const BehaviorTimedParams* pTimed = node._params.find<BehaviorTimedParams>(); pTimed != nullptr )
            ImGui::TextDisabled( "%.2f s", pTimed->_seconds );
        else if ( const BehaviorRepeatParams* pRepeat = node._params.find<BehaviorRepeatParams>(); pRepeat != nullptr )
            ImGui::TextDisabled( "x%d", pRepeat->_count );
        else if ( const BehaviorMoveToParams* pMove = node._params.find<BehaviorMoveToParams>(); pMove != nullptr )
            ImGui::TextDisabled( "-> %s", pMove->_key.c_str() );
        else if ( const BehaviorSubTreeParams* pSubTree = node._params.find<BehaviorSubTreeParams>(); pSubTree != nullptr )
            ImGui::TextDisabled( "%s", pSubTree->_tree.empty() ? pSubTree->_slot.c_str() : pSubTree->_tree.c_str() );
        if ( bActive )
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "running" );
        if ( BehaviorTreeNames::isLeaf( node._type ) == false )
        {
            _nodeGraph.beginPin( Internal::makeOutputPin( node._id ), false );
            ImGui::TextUnformatted( "children" );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( 0, node._listChild.empty() == false );
            _nodeGraph.endPin();
        }
        _nodeGraph.endNode();
    }

    bool BehaviorTreePanel::findGraphPin( int32 pinID, EditorGraphPinInfo& outInfo ) const
    {
        const BehaviorTreeEditNode* pNode = _model.find( BehaviorTreePanelInternal::decodeNode( pinID ) );
        if ( pNode == nullptr )
            return false;
        const bool bOutput = BehaviorTreePanelInternal::isOutputPin( pinID );
        if ( bOutput && BehaviorTreeNames::isLeaf( pNode->_type ) )
            return false;
        outInfo._type   = 0;
        outInfo._bInput = bOutput == false;
        return true;
    }

    void BehaviorTreePanel::handleCanvasInteractions()
    {
        // 새 링크 = 부모 바꾸기. 방향(나가는 핀에서 들어오는 핀)과 타입 판정, 거절 표시는 프레임워크(`EditorNodeGraph::queryNewLink`)가 한다.
        int32 fromPin{ 0 };
        int32 toPin{ 0 };
        if ( _nodeGraph.queryNewLink( SW_DELEGATE_METHOD( Delegate<bool( int32, EditorGraphPinInfo& )>, &BehaviorTreePanel::findGraphPin, this ), fromPin, toPin ) )
        {
            const int32 parentID = BehaviorTreePanelInternal::decodeNode( fromPin );
            const int32 childID  = BehaviorTreePanelInternal::decodeNode( toPin );
            if ( _model.reparent( childID, parentID ) )
                markStructureChanged( "Reparent Behavior Node" );
            else
                _statusText = "That link is not allowed (the root cannot move; a leaf or a full decorator cannot take a child; no loops)";
        }

        // 빈 곳을 오른쪽 클릭하면 노드 검색 팝업이 열린다. 고른 노드는 선택한 노드의 맨 뒤 자식(트리가 비었으면 루트)이다.
        vector<EditorGraphNodeKind> listKind;
        for ( uint32 index = 0; index < BehaviorTreePanelInternal::kNodeTypeCount; ++index )
        {
            const BehaviorNodeKind type = static_cast<BehaviorNodeKind>( index );
            listKind.push_back( EditorGraphNodeKind{ BehaviorTreeNames::toString( type ), BehaviorTreePanelInternal::getCategoryName( type ), index } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) )
            (void)addNodeFromSelection( static_cast<BehaviorNodeKind>( kindID ) ); // 거절하면 상태 줄이 이유를 알린다

        handleEditCommands(); // 잘라내기가 지우므로 아래 지우기 처리보다 먼저

        // 삭제: 노드는 그 아래까지 지우고, 링크만 지우는 것은 거절한다(링크를 끊으면 루트가 둘이 된다 - 노드를 지우거나 다른 부모로 이어 옮긴다).
        EditorGraphRemoval removal;
        _nodeGraph.collectRemovals( removal, EditorGraphLinkRemoval::Reject );
        if ( removal._rejectedLinkCount > 0 )
            _statusText = "Links cannot be deleted - delete the node or link it to another parent";
        for ( const int32 nodeID : removal._listNodeID )
        {
            if ( _model.removeNode( nodeID ) )
            {
                _selectedID = -1;
                markStructureChanged( "Delete Behavior Node" );
            }
        }
    }

    void BehaviorTreePanel::attachPastedSubtree( const expected<int32>& newID, const utf8* pLabel )
    {
        if ( newID.has_value() == false )
        {
            _statusText = EditorWidgets::makeStatusText( newID.error() );
            return;
        }
        _selectedID     = *newID;
        _bSelectPending = SW_TRUE;
        markStructureChanged( pLabel );
    }

    void BehaviorTreePanel::handleEditCommands()
    {
        // 노드 오른쪽 클릭 메뉴(복제 · 지우기) — 애니메이션 그래프 창과 같은 항목이다.
        ed::NodeId contextNode{};
        int32      duplicateID = -1;
        ed::Suspend();
        if ( ed::ShowNodeContextMenu( &contextNode ) )
        {
            _selectedID = static_cast<int32>( contextNode.Get() );
            EditorInputGUI::openPopup( "BehaviorNodeContext" );
        }
        if ( ImGui::BeginPopup( "BehaviorNodeContext" ) )
        {
            if ( ImGui::MenuItem( SW_LOCKEY( "EdPlugin", "Duplicate" ) ) )
                duplicateID = _selectedID;
            if ( ImGui::MenuItem( SW_LOCKEY( "EdPlugin", "Delete" ) ) )
                _nodeGraph.requestNodeDeletion( _selectedID );
            ImGui::EndPopup();
        }
        ed::Resume();

        vector<int32>                listNodeID;
        float2                       pastePosition{};
        const EditorGraphEditCommand command = _nodeGraph.queryEditCommand( listNodeID, pastePosition );
        // 트리는 루트가 하나라 한 번에 하위 트리 하나만 다룬다 — 여럿 골랐으면 첫 노드다.
        const int32 targetID = listNodeID.empty() ? -1 : listNodeID.front();
        if ( command == EditorGraphEditCommand::Duplicate )
            duplicateID = targetID;
        if ( duplicateID >= 0 )
        {
            attachPastedSubtree( _model.duplicateSubtree( duplicateID ), "Duplicate Behavior Subtree" );
            return;
        }
        if ( command == EditorGraphEditCommand::Paste )
        {
            string text;
            if ( EditorNodeGraph::readClipboard( BehaviorTreePanelInternal::kClipboardKind, text ) == false )
                return;
            const int32 parentID = _model.getRootID() < 0 ? -1 : _selectedID;
            if ( parentID < 0 && _model.getRootID() >= 0 )
            {
                _statusText = "Select the parent node first";
                return;
            }
            attachPastedSubtree( _model.pasteSubtree( parentID, text, -1 ), "Paste Behavior Subtree" );
            return;
        }
        if ( ( command != EditorGraphEditCommand::Copy && command != EditorGraphEditCommand::Cut ) || _model.find( targetID ) == nullptr )
            return;
        EditorNodeGraph::storeClipboard( BehaviorTreePanelInternal::kClipboardKind, _model.writeSubtree( targetID ) );
        if ( command == EditorGraphEditCommand::Cut )
            _nodeGraph.requestNodeDeletion( targetID ); // 지우기는 아래 지우기 처리가 같은 프레임에 받는다
    }

    int32 BehaviorTreePanel::addNodeFromSelection( BehaviorNodeKind type )
    {
        int32 parentID = _selectedID;
        if ( _model.getRootID() < 0 )
        {
            parentID = -1;
        }
        else if ( _model.find( parentID ) == nullptr )
        {
            _statusText = "Select the parent node first";
            return -1;
        }
        const int32 id = _model.addNode( parentID, type );
        if ( id < 0 )
        {
            _statusText = "That node cannot take a child here (a leaf, a decorator that already has one, or the depth limit)";
            return -1;
        }
        _selectedID     = id;
        _bSelectPending = SW_TRUE;
        markStructureChanged( "Add Behavior Node" );
        return id;
    }

    void BehaviorTreePanel::markStructureChanged( const utf8* pLabel )
    {
        _bLayoutPending = SW_TRUE;
        _statusText.clear();
        notifyDocumentEdited( pLabel );
    }

    float32 BehaviorTreePanel::layoutNode( int32 id, uint32 depth, float32& inoutNextRow )
    {
        const BehaviorTreeEditNode* pNode = _model.find( id );
        if ( pNode == nullptr )
            return 0.0f;
        float32 row = 0.0f;
        if ( pNode->_listChild.empty() )
        {
            row = inoutNextRow;
            inoutNextRow += 1.0f;
        }
        else
        {
            const vector<int32> listChild = pNode->_listChild; // 재귀 안에서 모델은 바뀌지 않지만 참조를 오래 쥐지 않는다
            float32             firstRow  = 0.0f;
            float32             lastRow   = 0.0f;
            for ( size_t index = 0; index < listChild.size(); ++index )
            {
                const float32 childRow = layoutNode( listChild[index], depth + 1, inoutNextRow );
                if ( index == 0 )
                    firstRow = childRow;
                lastRow = childRow;
            }
            row = ( firstRow + lastRow ) * 0.5f;
        }
        Placement placement;
        placement._id       = id;
        placement._position = float2{ static_cast<float32>( depth ) * BehaviorTreePanelInternal::kColumnSpacing, row * BehaviorTreePanelInternal::kRowSpacing };
        _listPlacement.push_back( placement );
        return row;
    }

    void BehaviorTreePanel::layoutTree()
    {
        _listPlacement.clear();
        if ( _model.getRootID() < 0 )
            return;
        float32 nextRow = 0.0f;
        (void)layoutNode( _model.getRootID(), 0, nextRow ); // 반환은 부모 줄 계산용이라 루트에서는 쓸 곳이 없다
    }

    const BehaviorTreeRunner* BehaviorTreePanel::findLiveRunner( string& outName ) const
    {
        outName.clear();
        const EditorSelection* pSelection = editor::getFocusedSelection();
        if ( pSelection == nullptr )
            return nullptr;
        vector<GameObject*> listSelected;
        pSelection->getSelectedObjects( listSelected );
        vector<BehaviorTreeDebugRunner> listDebugRunner;
        for ( GameObject* pObject : listSelected )
        {
            if ( pObject == nullptr )
                continue;
            AIControllerComponent*    pController = pObject->getComponent<AIControllerComponent>();
            const BehaviorTreeRunner* pRunner     = pController != nullptr ? pController->getBehaviorTreeRunner() : nullptr;
            if ( pRunner != nullptr )
                return pRunner;

            // 컴포넌트에 안 붙은 실행기(지휘관 AI 등) — 노드 번호는 깊이 우선 번호라 창에 연 파일과 같은 트리 파일을 쥔 실행기만 맞다.
            listDebugRunner.clear();
            pObject->forEachComponentOfType<GameModeComponent>(
                [&listDebugRunner]( GameModeComponent* pGameMode )
            {
                const IBehaviorTreeDebugSource* pSource = pGameMode->findBehaviorTreeDebugSource();
                if ( pSource != nullptr )
                    pSource->collectBehaviorTreeRunners( listDebugRunner );
            } );
            for ( const BehaviorTreeDebugRunner& entry : listDebugRunner )
            {
                if ( entry._pRunner != nullptr && entry._treePath == getLoadedAssetPath() )
                {
                    outName = entry._name;
                    return entry._pRunner;
                }
            }
        }
        return nullptr;
    }

    void BehaviorTreePanel::drawSidePanel()
    {
        EditorSectionDesc sideDesc{};
        sideDesc._pID   = "BehaviorTreeSide";
        sideDesc._kind  = EditorSectionKind::Child;
        sideDesc._flags = EditorSectionFlags::Border;
        EditorChrome::beginSection( sideDesc );
        BehaviorTreeEditNode* pNode = _model.find( _selectedID );
        if ( pNode != nullptr )
            drawInspector( *pNode );
        else
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdPlugin", "SelectANodeToEditItRightClick" ) );
        ImGui::Separator();
        drawIssueList();
        EditorChrome::endSection();
    }

    void BehaviorTreePanel::drawInspector( BehaviorTreeEditNode& node )
    {
        using Internal = BehaviorTreePanelInternal;
        ImGui::TextColored( Internal::getTypeColor( node._type ), "%s", BehaviorTreeNames::toString( node._type ) );
        bool bEdited = false;
        ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "Name" ) );
        bEdited = EditorWidgets::drawTextField( "##behaviorTree.name", node._name, -1.0f ) || bEdited;
        ImGui::TextUnformatted( "Id" );
        bEdited = EditorWidgets::drawTextField( "##behaviorTree.id", node._inheritID, -1.0f ) || bEdited; // 자식 트리가 이 노드를 짚는 열쇠

        // 칸은 스키마와 등록에서 온다 — 칸이나 동작을 더해도 이 패널을 고치지 않는다.
        _bGridEdited                      = SW_FALSE;
        const BehaviorActionParams* pTask = node._params.find<BehaviorActionParams>();
        if ( pTask != nullptr )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdPlugin", "Task" ) );
            if ( ImGui::BeginCombo( "##behaviorTree.task", pTask->_task.empty() ? "(pick a task)" : pTask->_task.c_str() ) )
            {
                _listTaskName.clear();
                ActionRegistry::collectNames( _listTaskName );
                for ( const string& name : _listTaskName )
                {
                    if ( ImGui::Selectable( name.c_str(), name == pTask->_task ) )
                    {
                        BehaviorTreeXML::selectTask( node, name );
                        bEdited = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            EditorPropertyGridTarget target{};
            target._pInstance = node._params.getInstance();
            target._pType     = node._params.getType();
            target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &BehaviorTreePanel::onGridEdited, this );
            _grid.drawProperties( target, nullptr, {} );
        }
        if ( node._actionParams.isValid() )
        {
            EditorPropertyGridTarget target{};
            target._pInstance = node._actionParams.getInstance();
            target._pType     = node._actionParams.getType();
            target._onEdited  = SW_DELEGATE_METHOD( Delegate<void( const PropertyInfo& )>, &BehaviorTreePanel::onGridEdited, this );
            _grid.drawProperties( target, nullptr, {} );
        }
        bEdited = bEdited || _bGridEdited != SW_FALSE;
        if ( bEdited )
        {
            _statusText.clear();
            notifyDocumentEdited( "Edit Behavior Node", "behavior-tree-node-edit" ); // 연속 편집은 한 번의 되돌리기로 합친다
        }
    }

    void BehaviorTreePanel::onGridEdited( const PropertyInfo& /*prop*/ )
    {
        _bGridEdited = SW_TRUE;
    }

    void BehaviorTreePanel::drawIssueList()
    {
        if ( _listIssue.empty() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Success" ) ), "No problems" );
            return;
        }
        ImGui::TextColored( EditorColorUtil::toImVec4( SW_THEME_COLOR( "Signal", "Error" ) ), "%s", SW_LOCKEYFORMAT( "EdPluginFormat", "Problems", TextArgumentList().addInteger( "issueCount", static_cast<int64>( _listIssue.size() ) ) ).c_str() );
        for ( const BehaviorTreeEditIssue& issue : _listIssue )
        {
            const string where = issue._nodeID.has_value() ? _model.makeDisplayName( *issue._nodeID ) : string{ "Tree" };
            ImGui::BulletText( "%s: %s", where.c_str(), issue._message.c_str() );
        }
    }
} // namespace sw::editor
