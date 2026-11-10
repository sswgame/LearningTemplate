#include "pch.h"

#include "Editor/Panels/Scene/Model/HierarchyViewModel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Name/TagID.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneObjectCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorComponentIconRow.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    namespace
    {
        struct HierarchyViewModelInternal
        {
            static EditorWorkspace& getWorkspace() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>(); }

            static GameObject* findObject( uint64 objectID )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                return pManager != nullptr && objectID != 0 ? pManager->findGameObjectByID( objectID ) : nullptr;
            }

            /**
             * @brief 계층 라벨에 붙일 `[Category]` 뱃지를 덧붙입니다.
             * @param category 컴포넌트 타입의 리플렉션 Category (`TypeInfo::getCategory`).
             * @param inoutBadge 누적 중인 뱃지 문자열. 비어 있지 않으면 앞에 공백이 붙습니다. 줄마다 프레임마다 만들어 힙을 쓰지 않는 고정 버퍼다(넘치면 자른다).
             * @details 타입 이름이 아니라 Category 로 고르므로 게임이 넣은 컴포넌트도 뱃지가 붙고, 엔진이 컴포넌트를 늘려도 패널을
             *          고칠 필요가 없습니다("컴포넌트 추가" 메뉴와 같은 기준). 같은 Category 는 한 번만 넣습니다.
             */
            static void appendCategoryBadge( string_view category, fixed_string<constant::buffer::kMax256>& inoutBadge )
            {
                if ( category.empty() )
                    return;

                // 같은 Category 의 컴포넌트가 여럿 붙어 있으면 뱃지도 여럿이 된다. 그래서 한 번만 넣는다.
                fixed_string<constant::buffer::kMax256> token{ "[" };
                token.append( category );
                token.append( "]" );
                if ( inoutBadge.view().find( token.view() ) != string_view::npos )
                    return;

                if ( inoutBadge.empty() == false )
                    inoutBadge.append( " " );
                inoutBadge.append( token );
            }

            /**
             * @brief 오브젝트 행의 글(아이콘 · 이름 · ID)과 범주 배지를 따로 만듭니다. 배지는 행 글에 넣지 않고 창이 그린 뒤 회색으로 얹는다 — 이름과 같은 색 · 같은 글에 붙여
             *        좁은 하이어라키에서 이름 뒤가 "[Rendering 3" 처럼 잘리고 이름과 구분되지 않았다(uiaudit 00, 언리얼 Outliner 는 종류를 회색 열로 둔다).
             */
            static void buildObjectLabel( const GameObject& object, HierarchyRowEntry& outEntry )
            {
                // 배지는 리플렉션 Category 에서 가져온다. 컴포넌트 추가 메뉴가 이미 쓰는 데이터다. 타입 이름을 비교하면 게임이 추가한 컴포넌트는 배지가 없다.
                for ( const Component* pComp : object.getComponents() )
                {
                    const TypeInfo* pTypeInfo = pComp != nullptr ? pComp->getTypeInfo() : nullptr;
                    if ( pTypeInfo != nullptr )
                        appendCategoryBadge( pTypeInfo->getCategory(), outEntry._badge );
                }
                // 오브젝트 아이콘은 빌보드 종류 컴포넌트(라이트, 카메라, 오디오)가 있으면 그 아이콘이고, 없으면 기본 오브젝트 아이콘이다.
                const utf8* pIcon = EditorSiblingOrder::isFolder( object ) ? SW_EDITOR_ICON( "Folder" ) : EditorComponentIconRow::findObjectRow( object )._pGlyph;
                formatstring( outEntry._label.data(), outEntry._label.capacity(), "%# %###go%#", pIcon, object.getName().c_str(), object.getObjectID() );
            }

            /** @brief "모두 선택" 하위 메뉴의 종류 항목(이 오브젝트의 컴포넌트 종류, 붙은 순서)입니다. 메뉴와 명령이 같은 순서를 쓴다. */
            static void collectSelectSameTypes( const GameObject& object, vector<const TypeInfo*>& outListType )
            {
                outListType.clear();
                for ( const Component* pComponent : object.getComponents() )
                {
                    const TypeInfo* pType = pComponent != nullptr ? pComponent->getTypeInfo() : nullptr;
                    if ( pType != nullptr )
                        outListType.push_back( pType );
                }
            }
        };
    } // namespace

    HierarchyViewModel::HierarchyViewModel()
        : _rowModel{}
        , _listDetailRow{}
        , _listRowIDScratch{}
        , _anchorObjectID{ 0 }
        , _revealObjectID{ 0 }
        , _lastPrimaryID{ 0 }
        , _pendingRenameID{ 0 }
        , _renamingObjectID{ 0 }
        , _addComponentSearch{}
        , _bClickedThisFrame{ SW_FALSE }
    {
    }

    HierarchyViewModel& HierarchyViewModel::get()
    {
        return getEditorState<HierarchyViewModel>();
    }

    bool HierarchyViewModel::hasActiveScene()
    {
        return editor::getActiveObjectManager() != nullptr;
    }

    HierarchyDropZone HierarchyViewModel::computeDropZone( float32 cursorY, float32 rowMinY, float32 rowMaxY )
    {
        const float32 height = rowMaxY - rowMinY;
        if ( height <= 0.0f )
            return HierarchyDropZone::Onto;
        const float32 fraction = ( cursorY - rowMinY ) / height;
        if ( fraction < kDropEdgeFraction )
            return HierarchyDropZone::Before;
        if ( fraction > 1.0f - kDropEdgeFraction )
            return HierarchyDropZone::After;
        return HierarchyDropZone::Onto;
    }

    void HierarchyViewModel::gather( string_view filterText, const HierarchyRowWindow& window, HierarchyViewState& outState )
    {
        outState._listEntry.clear();
        outState._listSelectedObjectID.clear();
        outState._renameStartText.clear();
        outState._selectedComponentID = 0;
        outState._visibleRootCount    = 0;
        outState._bHasScene           = SW_FALSE;
        outState._bFilterActive       = SW_FALSE;
        outState._bRenameStarted      = SW_FALSE;
        GameObjectManager* pManager   = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        outState._bHasScene = SW_TRUE;

        revealExternalSelection(); // 조상을 펼친다 — 아래 refresh 가 따라갈 행까지 편다
        const GameObject* pEditorCamera = HierarchyCommands::findEditorCameraObject();
        _rowModel.refresh( *pManager, filterText, pEditorCamera != nullptr ? pEditorCamera->getObjectID() : 0 );

        // 이름 바꾸기 명령(맥락 메뉴 · F2)이 남긴 요청을 그 행의 입력 칸으로 연다.
        if ( _pendingRenameID != 0 )
        {
            const GameObject* pRenamed = pManager->findGameObjectByID( _pendingRenameID );
            if ( pRenamed != nullptr )
            {
                _renamingObjectID         = _pendingRenameID;
                outState._renameStartText = pRenamed->getName().view();
                outState._bRenameStarted  = SW_TRUE;
            }
            _pendingRenameID = 0;
        }

        HierarchyCommands::collectSelectedObjectIDs( outState._listSelectedObjectID );
        outState._selectedComponentID = HierarchyViewModelInternal::getWorkspace().getSelectedComponentID();
        outState._visibleRootCount    = _rowModel.getVisibleRootCount();
        outState._bFilterActive       = _rowModel.isFilterActive();

        collectDetailRows( window, _listDetailRow );
        uint32 nextRow = 0;
        for ( const uint32 row : _listDetailRow )
        {
            appendSkippedRun( nextRow, row, outState._listEntry );
            outState._listEntry.emplace_back();
            fillRowDetail( row, outState._listEntry.back() );
            nextRow = row + 1;
        }
        appendSkippedRun( nextRow, static_cast<uint32>( _rowModel.getRows().size() ), outState._listEntry );
        _revealObjectID = 0; // 따라갈 행을 넘겼다. 필터 밖이라 행이 없으면 따라갈 것도 없다
    }

    void HierarchyViewModel::revealExternalSelection()
    {
        const GameObject* pPrimary             = editor::getMainSceneSelection().getPrimaryObject();
        const uint64      primaryID            = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
        const bool        bRevealPrimaryObject = primaryID != _lastPrimaryID && _bClickedThisFrame == false && primaryID != 0;
        if ( bRevealPrimaryObject )
        {
            _revealObjectID = primaryID;
            _rowModel.expandAncestorsOf( *pPrimary );
        }
        _lastPrimaryID     = primaryID;
        _bClickedThisFrame = SW_FALSE;
    }

    void HierarchyViewModel::collectDetailRows( const HierarchyRowWindow& window, vector<uint32>& outListRow ) const
    {
        outListRow.clear();
        const vector<HierarchyTreeRow>& listRow  = _rowModel.getRows();
        const uint32                    rowCount = static_cast<uint32>( listRow.size() );
        if ( rowCount == 0 )
            return;
        if ( window._bSkipOffscreenRows == false )
        {
            outListRow.reserve( rowCount );
            for ( uint32 row = 0; row < rowCount; ++row )
            {
                outListRow.push_back( row );
            }
            return;
        }

        // 화면 범위의 앞뒤 한 행씩 더 둔다 — 창의 안쪽 여백만큼 걸친 행을 놓치지 않는다.
        const uint32 firstVisible = _rowModel.findRowAtOffset( window._scrollY, window._stride );
        const uint32 lastVisible  = _rowModel.findRowAtOffset( window._scrollY + window._viewHeight, window._stride );
        const uint32 firstRow     = firstVisible > 0 ? firstVisible - 1 : 0;
        const uint32 lastRow      = MathUtil::min( lastVisible + 1, rowCount - 1 );
        // 행의 조상도 세부가 있어야 창이 트리를 그 깊이까지 연다. 범위 안 행의 조상은 범위 안에 있거나 첫 행의 조상이다(행이 미리 순회 순서라 서브트리가 이어져 있다).
        auto pushWithAncestors = [&listRow, &outListRow]( uint32 row )
        {
            for ( uint32 cursor = row; cursor != HierarchyRowModel::kNoRow; cursor = listRow[cursor]._parentRow )
            {
                outListRow.push_back( cursor );
            }
        };
        pushWithAncestors( firstRow );
        for ( uint32 row = firstRow + 1; row <= lastRow; ++row )
        {
            outListRow.push_back( row );
        }
        // 이름 바꾸는 행은 화면 밖이어도 입력 칸이 살아 있어야 확정 · 취소가 닿고, 따라갈 행은 창이 그 자리로 스크롤한다.
        // 행 찾기는 행 수에 비례하므로 그 일이 있을 때만 한다(이름 바꾸는 동안 · 따라가는 한 프레임).
        for ( const uint64 forcedObjectID : { _renamingObjectID, _revealObjectID } )
        {
            if ( forcedObjectID == 0 )
                continue;
            const optional<uint32> forcedRow = _rowModel.findRowOfObject( forcedObjectID );
            if ( forcedRow.has_value() )
                pushWithAncestors( *forcedRow );
        }
        std::sort( outListRow.begin(), outListRow.end() );
        outListRow.erase( std::unique( outListRow.begin(), outListRow.end() ), outListRow.end() );
    }

    void HierarchyViewModel::appendSkippedRun( uint32 firstRow, uint32 endRow, vector<HierarchyRowEntry>& outListEntry ) const
    {
        if ( endRow <= firstRow )
            return;
        const uint32 objectRows = _rowModel.countObjectRowsBefore( endRow ) - _rowModel.countObjectRowsBefore( firstRow );
        outListEntry.emplace_back();
        HierarchyRowEntry& entry    = outListEntry.back();
        entry._skippedObjectRows    = objectRows;
        entry._skippedComponentRows = ( endRow - firstRow ) - objectRows;
    }

    void HierarchyViewModel::fillRowDetail( uint32 row, HierarchyRowEntry& outEntry ) const
    {
        const HierarchyTreeRow& treeRow = _rowModel.getRows()[row];
        GameObject&             object  = *treeRow._pObject; // refresh 직후라 유효하다(HierarchyTreeRow 설명)
        EditorWorkspace&        ws      = HierarchyViewModelInternal::getWorkspace();
        outEntry._id                    = treeRow._id;
        outEntry._objectID              = treeRow._objectID;
        outEntry._depth                 = treeRow._depth;
        outEntry._kind                  = treeRow._kind;
        outEntry._bExpanded             = treeRow._bExpanded;
        outEntry._bLeaf                 = treeRow._bLeaf;
        outEntry._name                  = object.getName().view();

        if ( treeRow._kind == HierarchyRowKind::Object )
        {
            HierarchyViewModelInternal::buildObjectLabel( object, outEntry );
            outEntry._bSelected = editor::getMainSceneSelection().hasObject( &object );
            // 프리팹 인스턴스는 이름을 파랗게 표시한다(유니티 Hierarchy). 씬 인스턴스의 앵커와 멤버도 같다 — 저장이 차이로 쓰는 오브젝트다.
            const Scene* pScene         = editor::getActiveScene();
            const bool   bSceneInstance = pScene != nullptr && ( pScene->getSceneInstancePath( treeRow._id ).empty() == false || pScene->isSceneInstanceMember( treeRow._id ) );
            outEntry._bPrefabInstance   = bSceneInstance || ws.findPrefabPath( &object ).empty() == false;
            outEntry._bHiddenInEditor   = object.isRenderHidden();
            outEntry._bLocked           = ws.isObjectLocked( treeRow._id );
            outEntry._bRenaming         = treeRow._id == _renamingObjectID;
            outEntry._bScrollTo         = treeRow._id == _revealObjectID;
            return;
        }

        Component& component = *treeRow._pComponent;
        outEntry._bSelected  = ws.getSelectedObjectID() == treeRow._objectID && ws.getSelectedComponentID() == treeRow._id;
        if ( treeRow._kind == HierarchyRowKind::SceneComponent )
        {
            // 행 앞에 컴포넌트 아이콘을 그린다. 언리얼의 Outliner 나 Godot 의 씬 트리처럼 종류를 한눈에 알 수 있다.
            const utf8* pName = component.getComponentName().empty() == false ? component.getComponentName().c_str() : "SceneComponent";
            const utf8* pIcon = EditorComponentIconRow::findRow( component.getTypeInfo() )._pGlyph;
            formatstring( outEntry._label.data(), outEntry._label.capacity(), "%# %###sc%#", pIcon, pName, treeRow._id );
            return;
        }
        const utf8* pName = component.getComponentName().empty() == false ? component.getComponentName().c_str() : "Component";
        formatstring( outEntry._label.data(), outEntry._label.capacity(), "%###c%#", pName, treeRow._id );
    }

    void HierarchyViewModel::focusMainSelection()
    {
        editor::setFocusedSelection( nullptr );
    }

    void HierarchyViewModel::setHiddenInEditor( uint64 objectID, bool bHidden )
    {
        GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj != nullptr )
            EditorSceneCommands::setHiddenInEditor( pObj, bHidden );
    }

    void HierarchyViewModel::setObjectLocked( uint64 objectID, bool bLocked )
    {
        HierarchyViewModelInternal::getWorkspace().setObjectLocked( objectID, bLocked );
    }

    void HierarchyViewModel::clickRow( uint64 objectID, bool bCtrl, bool bShift )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObj     = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj == nullptr )
            return;
        EditorWorkspace& workspace = HierarchyViewModelInternal::getWorkspace();
        _bClickedThisFrame         = SW_TRUE;
        if ( bShift == false || _anchorObjectID == 0 )
        {
            workspace.selectGameObject( pObj, bCtrl ? SelectionMode::Toggle : SelectionMode::Replace );
            _anchorObjectID = objectID;
            return;
        }

        // Shift 는 시작 행부터 이 행까지(행 순서) 모두 선택한다. 유니티, 언리얼, 윈도 탐색기와 같다.
        vector<uint64>& listRow = _listRowIDScratch;
        _rowModel.collectObjectRowIDs( listRow );
        size_t anchorIndex = listRow.size();
        size_t clickIndex  = listRow.size();
        for ( size_t rowIndex = 0; rowIndex < listRow.size(); ++rowIndex )
        {
            if ( listRow[rowIndex] == _anchorObjectID )
                anchorIndex = rowIndex;
            if ( listRow[rowIndex] == objectID )
                clickIndex = rowIndex;
        }
        if ( anchorIndex >= listRow.size() || clickIndex >= listRow.size() )
        {
            workspace.selectGameObject( pObj, SelectionMode::Add );
            return;
        }

        const size_t first = MathUtil::min( anchorIndex, clickIndex );
        const size_t last  = MathUtil::max( anchorIndex, clickIndex );
        if ( bCtrl == false )
            editor::getMainSceneSelection().clearObjectSelection();
        for ( size_t rowIndex = first; rowIndex <= last; ++rowIndex )
        {
            GameObject* pRow = pManager->findGameObjectByID( listRow[rowIndex] );
            if ( pRow != nullptr )
                workspace.selectGameObject( pRow, SelectionMode::Add );
        }
        workspace.selectGameObject( pObj, SelectionMode::Add ); // 누른 행이 주 선택이다
    }

    void HierarchyViewModel::selectComponent( uint64 objectID, uint64 componentID )
    {
        GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj == nullptr )
            return;
        for ( Component* pComp : pObj->getComponents() )
        {
            if ( pComp != nullptr && pComp->getComponentID() == componentID )
            {
                _bClickedThisFrame = SW_TRUE; // 이 창에서 고른 것이라 따라가기 대상이 아니다
                HierarchyViewModelInternal::getWorkspace().selectComponent( pObj, pComp );
                return;
            }
        }
    }

    void HierarchyViewModel::selectAllRows()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        EditorSelection& editorSelection = editor::getMainSceneSelection();
        editorSelection.clearObjectSelection();
        _rowModel.collectObjectRowIDs( _listRowIDScratch );
        for ( const uint64 rowID : _listRowIDScratch )
        {
            GameObject* pRow = pManager->findGameObjectByID( rowID );
            if ( pRow != nullptr )
                editorSelection.selectObject( pRow, SelectionMode::Add );
        }
    }

    void HierarchyViewModel::selectStep( bool bUp )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        vector<uint64>& listRow = _listRowIDScratch;
        _rowModel.collectObjectRowIDs( listRow );
        if ( listRow.empty() )
            return;
        const uint64 primaryID = editor::getMainSceneSelection().getPrimaryObjectID();
        size_t       current   = listRow.size();
        for ( size_t rowIndex = 0; primaryID != 0 && rowIndex < listRow.size(); ++rowIndex )
        {
            if ( listRow[rowIndex] == primaryID )
                current = rowIndex;
        }
        const size_t next  = current >= listRow.size() ? 0 : ( bUp ? ( current > 0 ? current - 1 : 0 ) : MathUtil::min( current + 1, listRow.size() - 1 ) );
        GameObject*  pNext = pManager->findGameObjectByID( listRow[next] );
        if ( pNext == nullptr )
            return;
        HierarchyViewModelInternal::getWorkspace().selectGameObject( pNext, SelectionMode::Replace );
        _anchorObjectID    = pNext->getObjectID();
        _bClickedThisFrame = SW_TRUE; // 키로 선택한 것이므로 따라가기 대상이 아니다
    }

    void HierarchyViewModel::createObject()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager != nullptr )
            (void)EditorSceneCommands::create( pManager, nullptr ); // 편집이 막혔으면 nullptr 이고 아무것도 바뀌지 않는다
    }

    void HierarchyViewModel::renameObject( uint64 objectID, const utf8* pName )
    {
        GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj == nullptr )
            return;
        EditorTransaction transaction;                                 // 이름 칸 확정 — 명령(`scene.object.rename`)은 칸만 열었다
        (void)EditorSceneCommands::rename( transaction, pObj, pName ); // 빈 이름은 거절하므로 이름이 그대로 남는다
    }

    void HierarchyViewModel::dropObject( uint64 draggedID, uint64 targetID, HierarchyDropZone zone )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pDragged = HierarchyViewModelInternal::findObject( draggedID );
        GameObject*        pTarget  = HierarchyViewModelInternal::findObject( targetID );
        if ( pManager == nullptr || pDragged == nullptr || pTarget == nullptr )
            return;
        if ( zone == HierarchyDropZone::Onto )
            (void)EditorSceneCommands::reparent( pDragged, pTarget ); // 순환 · 편집 잠금이면 아무것도 바꾸지 않는다
        else
            (void)EditorSiblingOrder::moveBeside( *pManager, pDragged, pTarget, zone == HierarchyDropZone::After, HierarchyCommands::findEditorCameraObject() ); // 실패는 아무것도 바꾸지 않는다
    }

    void HierarchyViewModel::detachToRoot( uint64 draggedID )
    {
        GameObject* pDragged = HierarchyViewModelInternal::findObject( draggedID );
        if ( pDragged != nullptr && pDragged->getParent() != nullptr )
            (void)EditorSceneCommands::unparent( pDragged, "Detach GameObject to Root" ); // 편집 잠금이면 아무것도 바꾸지 않는다
    }

    void HierarchyViewModel::spawnPrefab( const utf8* pPath, uint64 parentID )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager != nullptr )
            (void)EditorAssetCommands::spawnPrefab( pManager, pPath, HierarchyViewModelInternal::findObject( parentID ) ); // 실패는 명령이 알린다
    }

    void HierarchyViewModel::addComponent( uint64 objectID, const hashed_string& typeName )
    {
        GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj == nullptr )
            return;
        EditorTransaction transaction;                                          // 하위 메뉴(`scene.object.addComponent`)에서 고른 끝
        (void)EditorSceneCommands::addComponent( transaction, pObj, typeName ); // 더했는지는 되돌리기가 기록하고 실패는 명령이 알린다
    }

    void HierarchyViewModel::selectAllWith( uint64 objectID, uint32 itemIndex )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObj     = HierarchyViewModelInternal::findObject( objectID );
        if ( pManager == nullptr || pObj == nullptr )
            return;
        vector<const TypeInfo*> listType;
        HierarchyViewModelInternal::collectSelectSameTypes( *pObj, listType );
        vector<GameObject*>  listMatch;
        const vector<TagID>& listTag = pObj->getTags().getTags();
        if ( itemIndex < listType.size() )
            EditorSceneCommands::collectObjectsWithComponent( *pManager, listType[itemIndex], listMatch );
        else if ( itemIndex - listType.size() < listTag.size() )
            EditorSceneCommands::collectObjectsWithTag( *pManager, listTag[itemIndex - listType.size()], listMatch );
        else
            return;
        (void)EditorSceneCommands::selectObjects( listMatch ); // 이 오브젝트가 늘 들어 있어 0 이 아니다
    }

    void HierarchyViewModel::collectTypeFilterNames( vector<string>& outListName ) const
    {
        outListName.clear();
        const GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager != nullptr )
            HierarchyFilter::collectComponentTypeNames( *pManager, outListName );
    }

    void HierarchyViewModel::collectSelectSameMenu( uint64 objectID, HierarchySelectSameMenu& outMenu ) const
    {
        outMenu._listLabel.clear();
        outMenu._typeCount     = 0;
        const GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj == nullptr )
            return;
        vector<const TypeInfo*> listType;
        HierarchyViewModelInternal::collectSelectSameTypes( *pObj, listType );
        for ( const TypeInfo* pType : listType )
        {
            outMenu._listLabel.emplace_back( pType->_name.c_str() );
        }
        outMenu._typeCount = static_cast<uint32>( listType.size() );
        for ( const TagID tag : pObj->getTags().getTags() )
        {
            fixed_string<constant::buffer::kMax128> label;
            formatstring( label.data(), label.capacity(), "Tag %s", tag.getString() );
            outMenu._listLabel.emplace_back( label.view() );
        }
    }

    optional<uint32> HierarchyViewModel::findObjectRowIndexByName( string_view objectName ) const
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        const string       name{ objectName };
        const GameObject*  pObject = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( name.c_str() ) ) : nullptr;
        if ( pObject == nullptr )
            return nullopt;
        return _rowModel.findObjectRowIndex( pObject->getObjectID() );
    }

    void HierarchyCommands::moveSelectionStep( bool bDown )
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObject  = editor::getMainSceneSelection().getPrimaryObject();
        if ( pManager != nullptr && pObject != nullptr )
            (void)EditorSiblingOrder::moveStep( *pManager, pObject, bDown, findEditorCameraObject() ); // 끝이면 아무것도 하지 않는다
    }

    void HierarchyCommands::createFolder()
    {
        GameObjectManager* pManager = editor::getActiveObjectManager();
        if ( pManager == nullptr )
            return;
        GameObject* pSelected = editor::getMainSceneSelection().getPrimaryObject();
        GameObject* pParent   = pSelected != nullptr && EditorSiblingOrder::isFolder( *pSelected ) ? pSelected : nullptr;
        (void)EditorSiblingOrder::createFolder( *pManager, pParent ); // 편집이 막혔으면 nullptr 이고 아무것도 바뀌지 않는다
    }

    void HierarchyCommands::selectOnlyObject( uint64 objectID )
    {
        GameObject* pObj = HierarchyViewModelInternal::findObject( objectID );
        if ( pObj != nullptr )
            HierarchyViewModelInternal::getWorkspace().selectGameObject( pObj, SelectionMode::Replace );
    }

    void HierarchyCommands::collectSelectedObjectIDs( vector<uint64>& outListID )
    {
        outListID.clear();
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        outListID.reserve( listSelected.size() );
        for ( const GameObject* pSelected : listSelected )
        {
            outListID.push_back( pSelected->getObjectID() );
        }
    }

    const GameObject* HierarchyCommands::findEditorCameraObject()
    {
        const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
        return pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
    }

    uint64 HierarchyCommands::findTargetObjectID( const EditorCommandTarget& target )
    {
        const GameObject* pObj = EditorSceneObjectCommands::findTargetObject( target );
        return pObj != nullptr ? pObj->getObjectID() : 0;
    }

    void HierarchyCommands::requestRename( const EditorCommandTarget& target )
    {
        const uint64 objectID = findTargetObjectID( target );
        if ( objectID != 0 )
            HierarchyViewModel::get().requestRename( objectID );
    }
} // namespace sw::editor
