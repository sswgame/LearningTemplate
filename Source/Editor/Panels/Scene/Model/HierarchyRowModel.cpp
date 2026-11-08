#include "pch.h"

#include "Editor/Panels/Scene/Model/HierarchyRowModel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Name/TagID.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct HierarchyRowModelInternal
        {
            static bool typeMatchesFilter( const GameObject* pObj, string_view typeFilter )
            {
                if ( pObj == nullptr || typeFilter.empty() )
                    return false;

                for ( Component* pComp : pObj->getComponents() )
                {
                    if ( pComp == nullptr )
                        continue;

                    if ( StringUtil::startsWith( pComp->getTypeName().view(), typeFilter, true ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace

    HierarchyRowModel::HierarchyRowModel()
        : _listRoot{}
        , _uniqueFilterMatchID{}
        , _filterText{}
        , _pManager{ nullptr }
        , _structureGeneration{ 0 }
        , _tagGeneration{ 0 }
        , _rebuildCount{ 0 }
        , _bFilterActive{ SW_FALSE }
        , _bClickedThisFrame{ SW_FALSE }
        , _listDrawnRow{}
        , _listPreviousRow{}
        , _anchorObjectID{ 0 }
        , _revealObjectID{ 0 }
        , _lastPrimaryID{ 0 }
        , _pendingRenameID{ 0 }
        , _addComponentSearch{}
    {
    }

    bool HierarchyFilter::matches( const GameObject* pObj, const utf8* pFilter )
    {
        if ( StringUtil::isNullOrEmpty( pFilter ) )
            return true;
        if ( pObj == nullptr )
            return false;

        // 1) 타입 문법 "t:ComponentName"
        if ( pFilter[0] == 't' && pFilter[1] == ':' )
        {
            return HierarchyRowModelInternal::typeMatchesFilter( pObj, string_view{ pFilter + 2 } );
        }

        // 2) 태그 문법 "tag:TagName"
        if ( StringUtil::startsWith( pFilter, "tag:", true ) )
        {
            // ID 의 원본은 `TagID::computeID` 다. `computeStringHash64` 를 기본 인자로 호출하면 대소문자를 무시해서
            // 대소문자를 구별하는 태그 쪽과 어긋나고, 대문자로 시작하는 태그를 하나도 찾지 못한다.
            //
            // 문자열도 함께 넘긴다. `isSubtagOf` 가 문자열을 보므로 `tag:Faction` 이 `Faction.Player` 까지
            // 찾는다. 리터럴 태그는 역조회 테이블에 등록되지 않아, ID 만 가진 TagID 로는 계층 비교를 할 수
            // 없다. 필터 버퍼는 이 호출 동안 유효하다.
            const string_view tagFilter{ pFilter + 4 };
            return pObj->hasTag( TagID{ TagID::computeID( tagFilter.data(), tagFilter.size() ), tagFilter.data() } );
        }

        // 3) 일반 이름 매칭
        return EditorListFilter{ pFilter }.matches( pObj->getName().view() );
    }

    HierarchyRowModel& HierarchyRowModel::get()
    {
        return getEditorState<HierarchyRowModel>();
    }

    void HierarchyFilter::collectComponentTypeNames( const GameObjectManager& manager, vector<string>& outListName )
    {
        outListName.clear();
        manager.forEachGameObject( [&outListName]( GameObject* pObj )
        {
            for ( Component* pComp : pObj->getComponents() )
            {
                if ( pComp != nullptr )
                    outListName.emplace_back( pComp->getTypeName().view() );
            }
        } );
        std::sort( outListName.begin(), outListName.end() );
        outListName.erase( std::unique( outListName.begin(), outListName.end() ), outListName.end() );
    }

    string_view HierarchyFilter::findTypeFilter( string_view filter )
    {
        if ( filter.size() < 2 || filter[0] != 't' || filter[1] != ':' )
            return {};
        return filter.substr( 2 );
    }

    void HierarchyRowModel::refresh( GameObjectManager& manager, string_view filterText )
    {
        const uint64 structureGeneration = manager.getStructureGeneration();
        const uint64 tagGeneration       = manager.getTagIndex().getGeneration();
        const bool   bUpToDate           = _pManager == &manager && _structureGeneration == structureGeneration && _tagGeneration == tagGeneration &&
                               _filterText.view() == filterText;
        if ( bUpToDate )
            return;

        _pManager            = &manager;
        _structureGeneration = structureGeneration;
        _tagGeneration       = tagGeneration;
        _filterText          = filterText;
        ++_rebuildCount;
        EditorSceneCommands::collectRootsInOrder( manager, _listRoot ); // 루트만 형제 순서로 모은다. 지우고 되돌려도 위치가 그대로다
        _uniqueFilterMatchID.clear();
        _bFilterActive = EditorListFilter{ _filterText.c_str() }.isActive();
        if ( _bFilterActive == false )
            return;
        const utf8*            pFilter     = _filterText.c_str();
        unordered_set<uint64>& uniqueMatch = _uniqueFilterMatchID;
        manager.forEachGameObject( [pFilter, &uniqueMatch]( GameObject* pObj )
        {
            if ( HierarchyFilter::matches( pObj, pFilter ) == false )
                return;
            for ( const GameObject* pCursor = pObj; pCursor != nullptr; pCursor = pCursor->getParent() )
            {
                if ( uniqueMatch.insert( pCursor->getObjectID() ).second == false )
                    break;
            }
        } );
    }

    bool HierarchyRowModel::isRowVisible( const GameObject* pObj ) const
    {
        return _bFilterActive == false || _uniqueFilterMatchID.contains( pObj->getObjectID() );
    }

    void HierarchyRowModel::beginFrame()
    {
        _listPreviousRow.swap( _listDrawnRow );
        _listDrawnRow.clear();

        const GameObject* pPrimary             = editor::getMainSceneSelection().getPrimaryObject();
        const uint64      primaryID            = pPrimary != nullptr ? pPrimary->getObjectID() : 0;
        const bool        bRevealPrimaryObject = primaryID != _lastPrimaryID && _bClickedThisFrame == false && primaryID != 0;
        if ( bRevealPrimaryObject )
            _revealObjectID = primaryID;
        _lastPrimaryID     = primaryID;
        _bClickedThisFrame = SW_FALSE;
    }

    optional<uint32> HierarchyRowModel::findDrawnRowIndex( uint64 objectID ) const
    {
        for ( size_t rowIndex = 0; rowIndex < _listDrawnRow.size(); ++rowIndex )
        {
            if ( _listDrawnRow[rowIndex] == objectID )
                return static_cast<uint32>( rowIndex );
        }
        return nullopt;
    }

    void HierarchyRowModel::clickRow( GameObjectManager& manager, GameObject* pObj, bool bCtrl, bool bShift )
    {
        EditorWorkspace& workspace = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
        const uint64     objectID  = pObj->getObjectID();
        _bClickedThisFrame         = SW_TRUE;
        if ( bShift == false || _anchorObjectID == 0 )
        {
            workspace.selectGameObject( pObj, bCtrl ? SelectionMode::Toggle : SelectionMode::Replace );
            _anchorObjectID = objectID;
            return;
        }

        // Shift 는 시작 행부터 이 행까지(지난 프레임에 그린 순서) 모두 선택한다. 유니티, 언리얼, 윈도 탐색기와 같다.
        const vector<uint64>& listRow     = _listPreviousRow;
        size_t                anchorIndex = listRow.size();
        size_t                clickIndex  = listRow.size();
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
            GameObject* pRow = manager.findGameObjectByID( listRow[rowIndex] );
            if ( pRow != nullptr )
                workspace.selectGameObject( pRow, SelectionMode::Add );
        }
        workspace.selectGameObject( pObj, SelectionMode::Add ); // 누른 행이 주 선택이다
    }

    void HierarchyRowModel::selectAllRows( GameObjectManager& manager ) const
    {
        EditorSelection& editorSelection = editor::getMainSceneSelection();
        editorSelection.clearObjectSelection();
        for ( const uint64 rowID : _listPreviousRow )
        {
            GameObject* pRow = manager.findGameObjectByID( rowID );
            if ( pRow != nullptr )
                editorSelection.selectObject( pRow, SelectionMode::Add );
        }
    }

    void HierarchyRowModel::selectStep( GameObjectManager& manager, bool bUp )
    {
        const vector<uint64>& listRow = _listPreviousRow;
        if ( listRow.empty() )
            return;
        const GameObject* pPrimary = editor::getMainSceneSelection().getPrimaryObject();
        size_t            current  = listRow.size();
        for ( size_t rowIndex = 0; pPrimary != nullptr && rowIndex < listRow.size(); ++rowIndex )
        {
            if ( listRow[rowIndex] == pPrimary->getObjectID() )
                current = rowIndex;
        }
        const size_t next  = current >= listRow.size() ? 0 : ( bUp ? ( current > 0 ? current - 1 : 0 ) : MathUtil::min( current + 1, listRow.size() - 1 ) );
        GameObject*  pNext = manager.findGameObjectByID( listRow[next] );
        if ( pNext == nullptr )
            return;
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().selectGameObject( pNext, SelectionMode::Replace );
        _anchorObjectID    = pNext->getObjectID();
        _bClickedThisFrame = SW_TRUE; // 키로 선택한 것이므로 펼쳐 보이기 대상이 아니다
    }

    uint64 HierarchyRowModel::takePendingRename()
    {
        const uint64 objectID = _pendingRenameID;
        _pendingRenameID      = 0;
        return objectID;
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
        GameObjectManager* pManager = editor::getActiveObjectManager();
        GameObject*        pObj     = pManager != nullptr ? pManager->findGameObjectByID( objectID ) : nullptr;
        if ( pObj != nullptr )
            editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().selectGameObject( pObj, SelectionMode::Replace );
    }

    vector<uint64> HierarchyCommands::collectSelectedObjectIDs()
    {
        vector<GameObject*> listSelected;
        editor::getMainSceneSelection().getSelectedObjects( listSelected );
        vector<uint64> listID;
        listID.reserve( listSelected.size() );
        for ( const GameObject* pSelected : listSelected )
        {
            listID.push_back( pSelected->getObjectID() );
        }
        return listID;
    }

    bool HierarchyCommands::isAncestorOf( const GameObject* pObj, const GameObject* pTarget )
    {
        for ( const GameObject* pParent = pTarget != nullptr ? pTarget->getParent() : nullptr; pParent != nullptr; pParent = pParent->getParent() )
        {
            if ( pParent == pObj )
                return true;
        }
        return false;
    }

    const GameObject* HierarchyCommands::findEditorCameraObject()
    {
        const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
        return pEditorCamera != nullptr ? pEditorCamera->getOwner() : nullptr;
    }
} // namespace sw::editor
