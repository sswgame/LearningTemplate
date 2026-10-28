#include "pch.h"

#include "GameFramework/Plugin/Editor/Base/AI/BehaviorTreePanel.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNodeGraphID.h"
#include "Editor/Common/Widgets/EditorNodeGraphRules.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Character/AI/BehaviorTaskRegistry.h"
#include "GameFramework/Base/Character/AI/BehaviorTreeAsset.h"
#include "GameFramework/Base/Character/AI/IBehaviorTreeDebugSource.h"
#include "GameFramework/Base/Character/Control/Controller/AIControllerComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameDirectorComponent.h"

#include <imgui.h>

namespace ed = ax::NodeEditor;

namespace sw::editor
{
    namespace
    {
        struct BehaviorTreePanelInternal
        {
            static constexpr const utf8* kCanvasID           = "BehaviorTreeCanvas";
            static constexpr const utf8* kCanvasSettingsFile = "BehaviorTreeCanvas.json";
            static constexpr float32     kSidePanelWidth     = 340.0f; ///< 96 DPI 기준입니다
            static constexpr float32     kColumnSpacing      = 260.0f; ///< 깊이마다 오른쪽으로 벌리는 간격(캔버스 좌표)
            static constexpr float32     kRowSpacing         = 110.0f; ///< 잎 하나마다 아래로 벌리는 간격(캔버스 좌표)

            // 핀 번호 = 노드 번호 × 2 + (나가는 핀이면 1). 노드 하나에 들어오는 핀(부모 쪽)과 나가는 핀(자식 쪽)이 하나씩이다.
            static int32 makeInputPin( int32 nodeID ) { return nodeID * 2; }
            static int32 makeOutputPin( int32 nodeID ) { return nodeID * 2 + 1; }
            static int32 decodeNode( int32 pinID ) { return pinID / 2; }
            static bool  isOutputPin( int32 pinID ) { return pinID % 2 == 1; }

            static ImVec4 getTypeColor( BehaviorNodeType type )
            {
                if ( BehaviorTreeNames::isLeaf( type ) )
                    return EditorColorUtil::toImVec4( EditorThemeUtil::getWarningColor() );
                if ( BehaviorTreeNames::isDecorator( type ) )
                    return EditorColorUtil::toImVec4( EditorThemeUtil::getAccentColor() );
                return EditorColorUtil::toImVec4( EditorThemeUtil::getInfoColor() );
            }

            static const utf8* getCategoryName( BehaviorNodeType type )
            {
                if ( BehaviorTreeNames::isLeaf( type ) )
                    return "Leaf";
                if ( BehaviorTreeNames::isDecorator( type ) )
                    return "Decorator";
                return "Composite";
            }

            static constexpr uint32 kNodeTypeCount = static_cast<uint32>( BehaviorNodeType::MoveTo ) + 1;
            static constexpr uint32 kCompareCount  = static_cast<uint32>( BlackboardCompare::GreaterOrEqual ) + 1;
            static constexpr uint32 kAbortCount    = static_cast<uint32>( BehaviorAbortMode::Both ) + 1;

            /** @brief 노드 종류가 받는 속성인지입니다(속성 표는 로더와 같은 `BehaviorTreeNames::getAttributeNames`). */
            static bool hasAttribute( BehaviorNodeType type, string_view name )
            {
                const BehaviorTreeNames::AttributeList list = BehaviorTreeNames::getAttributeNames( type );
                for ( uint32 index = 0; index < list._count; ++index )
                {
                    if ( name == list._ppName[index] )
                        return true;
                }
                return false;
            }

            /** @brief 열거자 하나를 콤보로 고릅니다. 바꿨으면 true 입니다. */
            template <typename E>
            static bool drawEnumCombo( const utf8* pLabel, E& inoutValue, uint32 count )
            {
                bool bChanged = false;
                if ( ImGui::BeginCombo( pLabel, BehaviorTreeNames::toString( inoutValue ) ) )
                {
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        const E    candidate = static_cast<E>( index );
                        const bool bSelected = candidate == inoutValue;
                        if ( ImGui::Selectable( BehaviorTreeNames::toString( candidate ), bSelected ) )
                        {
                            inoutValue = candidate;
                            bChanged   = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                return bChanged;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "BehaviorTreePanel" );
    SW_EDITOR_PANEL( BehaviorTreePanel, BehaviorTreePanel::kPanelID, EditorPanelCategory::Custom, 9320 );

    BehaviorTreePanel::BehaviorTreePanel()
        : EditorDocumentPanel{ EditorAssetType::Data, false }
        , _model{}
        , _nodeGraph{}
        , _listIssue{}
        , _listPlacement{}
        , _listPreorder{}
        , _listTaskName{}
        , _statusText{}
        , _selectedID{ -1 }
        , _bLayoutPending{ true }
        , _bSelectPending{ false }
    {
        _nodeGraph.setPinTypeColors( { EditorThemeUtil::getTextColor() } ); // 부모 → 자식 흐름 핀 하나
    }

    void BehaviorTreePanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        _nodeGraph.shutdown();
    }

    bool BehaviorTreePanel::acceptsDocumentPath( string_view assetPath ) const
    {
        return StringUtil::endsWith( assetPath, BehaviorTreeAsset::kExtension );
    }

    ToolAssetLoadResult BehaviorTreePanel::loadDocument()
    {
        string path = getLoadedAssetPath();
        if ( path.empty() )
            path = string{ getMatchingFocusedPath() };
        if ( path.empty() )
            return ToolAssetLoadResult::Missing;
        if ( getLoadedAssetPath().empty() )
            acceptFocusedDocument();

        _model.clear();
        _selectedID     = -1;
        _bLayoutPending = true;
        _nodeGraph.requestContentFit();
        string diskPath = AssetPaths::findLooseFile( path ).getDiskPath();
        if ( diskPath.empty() )
            diskPath = path;
        string text;
        if ( FileUtil::readTextFile( diskPath, text ) == false )
        {
            _statusText = "The file could not be read";
            SW_LOG_ERROR( "Behavior tree file '%#' could not be read", diskPath.c_str() );
            return ToolAssetLoadResult::Malformed;
        }
        string error;
        if ( _model.parse( text, error ) == false )
        {
            // 읽지 못한 파일은 덮어쓰지 않도록 저장을 막는다(Malformed).
            _statusText = error;
            SW_LOG_ERROR( "Behavior tree file '%#' could not be opened: %#", diskPath.c_str(), error.c_str() );
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
        string diskPath = AssetPaths::findLooseFile( path ).getDiskPath();
        if ( diskPath.empty() )
            diskPath = path;
        _model.collectIssues( _listIssue );
        if ( _listIssue.empty() == false )
            SW_LOG_WARNING( "Saving behavior tree '%#' with %# problems - the game will not load it until they are fixed", path.c_str(), _listIssue.size() );
        if ( FileUtil::writeTextFile( diskPath, _model.write() ) == false )
        {
            _statusText = "The file could not be written";
            SW_LOG_ERROR( "Behavior tree file '%#' could not be written", diskPath.c_str() );
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
        string error;
        if ( _model.parse( text, error ) == false )
        {
            // 되돌리기 글은 이 창이 쓴 XML 이다. 읽지 못하면 결함이므로 알리고 지금 모델을 둔다.
            SW_LOG_WARNING( "Behavior tree undo snapshot could not be read: %#", error.c_str() );
            return;
        }
        _selectedID     = -1;
        _bLayoutPending = true;
        _nodeGraph.requestContentFit();
    }

    void BehaviorTreePanel::drawDocumentContent()
    {
        updateFocusedDocument();
        ensureDocumentLoaded();
        drawToolbar();

        if ( getLoadedAssetPath().empty() )
        {
            ImGui::TextDisabled( "Open a behavior tree file (*.bt.xml) with Open..." );
            return;
        }
        if ( isDocumentLoadFailed() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "This file could not be opened: %s", _statusText.c_str() );
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
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Save" ) );
            if ( ImGui::Button( "Save" ) )
                (void)saveDocument(); // 실패는 상태 줄과 로그가 알린다
            EditorSelfTestMarks::note( "behaviorTree.save" );
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reload" ) );
            if ( ImGui::Button( "Reload" ) )
                reloadDocument();
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Zoom fit" ) );
            if ( ImGui::Button( "Zoom fit" ) )
                _nodeGraph.requestContentFit();
            ImGui::SameLine();
            const bool bEditable = getLoadedAssetPath().empty() == false && isDocumentLoadFailed() == false;
            if ( bEditable )
            {
                if ( ImGui::BeginCombo( "##behaviorTree.add", "Add node..." ) )
                {
                    for ( uint32 index = 0; index < BehaviorTreePanelInternal::kNodeTypeCount; ++index )
                    {
                        const BehaviorNodeType type = static_cast<BehaviorNodeType>( index );
                        if ( ImGui::Selectable( BehaviorTreeNames::toString( type ) ) )
                            (void)addNodeFromSelection( type ); // 거절하면 상태 줄이 이유를 알린다
                    }
                    ImGui::EndCombo();
                }
                EditorSelfTestMarks::note( "behaviorTree.add" );
                ImGui::SameLine();
                const bool bHasSelection = _model.find( _selectedID ) != nullptr;
                const bool bMoveUp       = ImGui::Button( "Up" );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Down" ) );
                const bool bMoveDown = ImGui::Button( "Down" );
                EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Delete" ) );
                const bool bDelete = ImGui::Button( "Delete" );
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
                ImGui::TextDisabled( "%u nodes | %u problems", _model.getNodeCount(), static_cast<uint32>( _listIssue.size() ) );
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
            ImGui::TextDisabled( "Live: %s", liveRunnerName.c_str() );

        // 확장 모듈은 imgui-node-editor 를 자기 사본으로 링크하므로, 캔버스를 연 뒤 이 사본에도 지금 편집기를 건다(`EditorGraphDocumentPanel::beginGraphCanvas` 와 같다).
        if ( _nodeGraph.beginCanvas( BehaviorTreePanelInternal::kCanvasID, BehaviorTreePanelInternal::kCanvasSettingsFile ) == false )
        {
            if ( _nodeGraph.hasContext() == false )
                ImGui::TextUnformatted( "The node editor could not be created" );
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
            _bLayoutPending = false;
            _nodeGraph.requestContentFit();
        }
        if ( _bSelectPending )
        {
            ed::SelectNode( toNodeID( _selectedID ), false );
            _bSelectPending = false;
        }

        vector<EditorGraphNodeIssue> listNodeIssue;
        for ( const BehaviorTreeEditIssue& issue : _listIssue )
        {
            if ( issue._nodeID < 0 )
                continue;
            EditorGraphNodeIssue nodeIssue;
            nodeIssue._nodeID  = issue._nodeID;
            nodeIssue._message = issue._message;
            listNodeIssue.push_back( std::move( nodeIssue ) );
        }
        _nodeGraph.setNodeIssues( std::move( listNodeIssue ) );
        _nodeGraph.drawNodeIssues();

        handleCanvasInteractions();

        ed::NodeId  arrSelected[1];
        const int32 selectedCount = ed::GetSelectedNodes( arrSelected, 1 );
        _selectedID               = selectedCount > 0 ? static_cast<int32>( arrSelected[0].Get() ) : -1;

        _nodeGraph.applyContentFitIfNeeded();
        EditorNodeGraph::noteDrawnNodeCount( _model.getNodeCount() );
        _nodeGraph.endCanvas();
        ed::SetCurrentEditor( nullptr );
        EditorChrome::endSection();
    }

    void BehaviorTreePanel::drawNode( const BehaviorTreeEditNode& node, bool bActive )
    {
        using Internal = BehaviorTreePanelInternal;
        ed::BeginNode( toNodeID( node._id ) );
        if ( node._parent >= 0 )
        {
            ed::BeginPin( toPinID( Internal::makeInputPin( node._id ) ), ed::PinKind::Input );
            _nodeGraph.drawPinIcon( 0, true );
            ed::EndPin();
            ImGui::SameLine();
        }
        ImGui::TextColored( Internal::getTypeColor( node._type ), "%s", BehaviorTreeNames::toString( node._type ) );
        const string display = _model.makeDisplayName( node._id );
        if ( display != BehaviorTreeNames::toString( node._type ) )
            ImGui::TextUnformatted( display.c_str() );
        switch ( node._type )
        {
            case BehaviorNodeType::BlackboardCondition:
            {
                ImGui::TextDisabled( "%s %s", node._key.c_str(), BehaviorTreeNames::toString( node._compare ) );
                break;
            }
            case BehaviorNodeType::Cooldown:
            case BehaviorNodeType::TimeLimit:
            case BehaviorNodeType::Wait:
            {
                ImGui::TextDisabled( "%.2f s", node._seconds );
                break;
            }
            case BehaviorNodeType::Repeat:
            {
                ImGui::TextDisabled( "x%d", node._count );
                break;
            }
            case BehaviorNodeType::MoveTo:
            {
                ImGui::TextDisabled( "-> %s", node._key.c_str() );
                break;
            }
            case BehaviorNodeType::Selector:
            case BehaviorNodeType::Sequence:
            case BehaviorNodeType::Inverter:
            case BehaviorNodeType::ForceSuccess:
            case BehaviorNodeType::Action:
            case BehaviorNodeType::Condition:
            {
                break;
            }
        }
        if ( bActive )
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getSuccessColor() ), "running" );
        if ( BehaviorTreeNames::isLeaf( node._type ) == false )
        {
            ed::BeginPin( toPinID( Internal::makeOutputPin( node._id ) ), ed::PinKind::Output );
            ImGui::TextUnformatted( "children" );
            ImGui::SameLine();
            _nodeGraph.drawPinIcon( 0, node._listChild.empty() == false );
            ed::EndPin();
        }
        ed::EndNode();
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
            const BehaviorNodeType type = static_cast<BehaviorNodeType>( index );
            listKind.push_back( EditorGraphNodeKind{ BehaviorTreeNames::toString( type ), BehaviorTreePanelInternal::getCategoryName( type ), index } );
        }
        uint32 kindID{ 0 };
        float2 canvasPosition{};
        if ( _nodeGraph.drawAddNodePopup( listKind, kindID, canvasPosition ) )
            (void)addNodeFromSelection( static_cast<BehaviorNodeType>( kindID ) ); // 거절하면 상태 줄이 이유를 알린다

        // 삭제: 노드는 그 아래까지 지우고, 링크만 지우는 것은 거절한다(링크를 끊으면 루트가 둘이 된다 - 노드를 지우거나 다른 부모로 이어 옮긴다).
        if ( ed::BeginDelete() )
        {
            ed::LinkId linkID;
            while ( ed::QueryDeletedLink( &linkID ) )
            {
                ed::RejectDeletedItem();
                _statusText = "Links cannot be deleted - delete the node or link it to another parent";
            }
            ed::NodeId nodeID;
            while ( ed::QueryDeletedNode( &nodeID ) )
            {
                if ( ed::AcceptDeletedItem() == false )
                    continue;
                if ( _model.removeNode( static_cast<int32>( nodeID.Get() ) ) )
                {
                    _selectedID = -1;
                    markStructureChanged( "Delete Behavior Node" );
                }
            }
            ed::EndDelete();
        }
    }

    int32 BehaviorTreePanel::addNodeFromSelection( BehaviorNodeType type )
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
        _bSelectPending = true;
        markStructureChanged( "Add Behavior Node" );
        return id;
    }

    void BehaviorTreePanel::markStructureChanged( const utf8* pLabel )
    {
        _bLayoutPending = true;
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
            pObject->forEachComponentOfType<GameDirectorComponent>(
                [&listDebugRunner]( GameDirectorComponent* pDirector )
            {
                const IBehaviorTreeDebugSource* pSource = pDirector->findBehaviorTreeDebugSource();
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
            ImGui::TextDisabled( "Select a node to edit it. Right-click the canvas to add one." );
        ImGui::Separator();
        drawIssueList();
        EditorChrome::endSection();
    }

    void BehaviorTreePanel::drawInspector( BehaviorTreeEditNode& node )
    {
        using Internal = BehaviorTreePanelInternal;
        ImGui::TextColored( Internal::getTypeColor( node._type ), "%s", BehaviorTreeNames::toString( node._type ) );
        bool bEdited = false;
        if ( Internal::hasAttribute( node._type, "name" ) )
        {
            ImGui::TextUnformatted( "Name" );
            bEdited = EditorWidgets::drawTextField( "##behaviorTree.name", node._name, -1.0f ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "reactive" ) )
            bEdited = ImGui::Checkbox( "Reactive (re-check higher priority children every tick)", &node._bReactive ) || bEdited;
        if ( Internal::hasAttribute( node._type, "task" ) )
        {
            ImGui::TextUnformatted( "Task" );
            if ( ImGui::BeginCombo( "##behaviorTree.task", node._task.empty() ? "(pick a task)" : node._task.c_str() ) )
            {
                _listTaskName.clear();
                BehaviorTaskRegistry::get().collectNames( _listTaskName );
                for ( const string& name : _listTaskName )
                {
                    if ( ImGui::Selectable( name.c_str(), name == node._task ) )
                    {
                        node._task = name;
                        bEdited    = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if ( Internal::hasAttribute( node._type, "key" ) )
        {
            ImGui::TextUnformatted( "Blackboard key" );
            bEdited = EditorWidgets::drawTextField( "##behaviorTree.key", node._key, -1.0f ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "compare" ) )
        {
            ImGui::TextUnformatted( "Compare" );
            bEdited = Internal::drawEnumCombo( "##behaviorTree.compare", node._compare, Internal::kCompareCount ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "value" ) )
        {
            ImGui::TextUnformatted( "Value" );
            bEdited = ImGui::DragFloat( "##behaviorTree.value", &node._value, 0.05f ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "abort" ) )
        {
            ImGui::TextUnformatted( "Abort" );
            bEdited = Internal::drawEnumCombo( "##behaviorTree.abort", node._abort, Internal::kAbortCount ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "seconds" ) )
        {
            ImGui::TextUnformatted( "Seconds" );
            bEdited = ImGui::DragFloat( "##behaviorTree.seconds", &node._seconds, 0.05f, 0.0f, 3600.0f ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "count" ) )
        {
            ImGui::TextUnformatted( "Count (0 = until it fails)" );
            bEdited = ImGui::DragInt( "##behaviorTree.count", &node._count, 0.2f, 0, 100000 ) || bEdited;
        }
        if ( Internal::hasAttribute( node._type, "repathDistance" ) )
        {
            ImGui::TextUnformatted( "Repath distance" );
            bEdited = ImGui::DragFloat( "##behaviorTree.repath", &node._repathDistance, 0.05f, 0.0f, 1000.0f ) || bEdited;
        }
        if ( bEdited )
        {
            _statusText.clear();
            notifyDocumentEdited( "Edit Behavior Node", "behavior-tree-node-edit" ); // 연속 편집은 한 번의 되돌리기로 합친다
        }
    }

    void BehaviorTreePanel::drawIssueList()
    {
        if ( _listIssue.empty() )
        {
            ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getSuccessColor() ), "No problems" );
            return;
        }
        ImGui::TextColored( EditorColorUtil::toImVec4( EditorThemeUtil::getErrorColor() ), "%u problems", static_cast<uint32>( _listIssue.size() ) );
        for ( const BehaviorTreeEditIssue& issue : _listIssue )
        {
            const string where = issue._nodeID >= 0 ? _model.makeDisplayName( issue._nodeID ) : string{ "Tree" };
            ImGui::BulletText( "%s: %s", where.c_str(), issue._message.c_str() );
        }
    }
} // namespace sw::editor
