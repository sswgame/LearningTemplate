#include "pch.h"

#include "Editor/Panels/Scene/Model/HierarchyRowModel.h"

#include "Core/Name/TagID.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/TagIndex.h"

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

            /** @brief @p pSceneComponent 에 같은 오브젝트의 자식 씬 컴포넌트가 있으면 true 입니다(다른 오브젝트의 것은 그 오브젝트 행 아래에 있다). */
            static bool hasChildOnOwner( const GameObject* pObj, const SceneComponent* pSceneComponent )
            {
                for ( const SceneComponent* pChild : pSceneComponent->getChildren() )
                {
                    if ( pChild != nullptr && pChild->getOwner() == pObj )
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
        , _hiddenRootID{ 0 }
        , _rebuildCount{ 0 }
        , _uniqueExpandedObjectID{}
        , _uniqueExpandedComponentID{}
        , _listRow{}
        , _objectRowCount{ 0 }
        , _visibleRootCount{ 0 }
        , _bFilterActive{ SW_FALSE }
        , _bRowsDirty{ SW_TRUE }
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

    void HierarchyRowModel::refresh( GameObjectManager& manager, string_view filterText, uint64 hiddenRootID )
    {
        const uint64 structureGeneration = manager.getStructureGeneration();
        const uint64 tagGeneration       = manager.getTagIndex().getGeneration();
        const bool   bSameManager        = _pManager == &manager;
        const bool   bUpToDate           = bSameManager && _structureGeneration == structureGeneration && _tagGeneration == tagGeneration &&
                               _filterText.view() == filterText && _hiddenRootID == hiddenRootID;
        if ( bUpToDate )
        {
            if ( _bRowsDirty )
                rebuildRows();
            return;
        }

        // 다른 매니저(다른 씬)의 펼침은 다른 오브젝트의 id 다.
        if ( bSameManager == false )
        {
            _uniqueExpandedObjectID.clear();
            _uniqueExpandedComponentID.clear();
        }
        _pManager            = &manager;
        _structureGeneration = structureGeneration;
        _tagGeneration       = tagGeneration;
        _filterText          = filterText;
        _hiddenRootID        = hiddenRootID;
        ++_rebuildCount;
        EditorSceneCommands::collectRootsInOrder( manager, _listRoot ); // 루트만 형제 순서로 모은다. 지우고 되돌려도 위치가 그대로다
        _uniqueFilterMatchID.clear();
        _bFilterActive = EditorListFilter{ _filterText.c_str() }.isActive();
        if ( _bFilterActive )
        {
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
        rebuildRows();
    }

    bool HierarchyRowModel::isRowVisible( const GameObject* pObj ) const
    {
        return _bFilterActive == false || _uniqueFilterMatchID.contains( pObj->getObjectID() );
    }

    bool HierarchyRowModel::isExpanded( HierarchyRowKind kind, uint64 id ) const
    {
        return kind == HierarchyRowKind::Object ? _uniqueExpandedObjectID.contains( id ) : _uniqueExpandedComponentID.contains( id );
    }

    void HierarchyRowModel::setExpanded( HierarchyRowKind kind, uint64 id, bool bExpanded )
    {
        unordered_set<uint64>& uniqueExpanded = kind == HierarchyRowKind::Object ? _uniqueExpandedObjectID : _uniqueExpandedComponentID;
        const bool             bChanged       = bExpanded ? uniqueExpanded.insert( id ).second : uniqueExpanded.erase( id ) > 0;
        if ( bChanged )
            _bRowsDirty = SW_TRUE;
    }

    void HierarchyRowModel::expandAncestors( const GameObject& object )
    {
        for ( const GameObject* pParent = object.getParent(); pParent != nullptr; pParent = pParent->getParent() )
        {
            setExpanded( HierarchyRowKind::Object, pParent->getObjectID(), true );
        }
    }

    void HierarchyRowModel::rebuildRows()
    {
        _bRowsDirty       = SW_FALSE;
        _objectRowCount   = 0;
        _visibleRootCount = 0;
        _listRow.clear();
        for ( GameObject* pRoot : _listRoot )
        {
            if ( pRoot->getObjectID() == _hiddenRootID || isRowVisible( pRoot ) == false )
                continue;
            ++_visibleRootCount;
            appendObjectRows( pRoot, kNoRow, 0 );
        }
    }

    uint32 HierarchyRowModel::pushRow( HierarchyTreeRow row )
    {
        row._objectRowsBefore = _objectRowCount;
        if ( row._kind == HierarchyRowKind::Object )
            ++_objectRowCount;
        _listRow.push_back( row );
        return static_cast<uint32>( _listRow.size() - 1 );
    }

    void HierarchyRowModel::appendObjectRows( GameObject* pObj, uint32 parentRow, uint16 depth )
    {
        HierarchyTreeRow row{};
        row._pObject          = pObj;
        row._id               = pObj->getObjectID();
        row._objectID         = row._id;
        row._parentRow        = parentRow;
        row._depth            = depth;
        row._kind             = HierarchyRowKind::Object;
        row._bLeaf            = pObj->hasChildren() == false && pObj->getComponentCount() == 0;
        row._bExpanded        = row._bLeaf == false && _uniqueExpandedObjectID.contains( row._id );
        const uint32 rowIndex = pushRow( row );
        if ( row._bExpanded == false )
            return;

        vector<GameObject*> listChild; // 펼친 오브젝트에서만, 다시 펼 때만 만든다
        EditorSiblingOrder::collectChildrenInOrder( *pObj, listChild );
        for ( GameObject* pChild : listChild )
        {
            if ( isRowVisible( pChild ) )
                appendObjectRows( pChild, rowIndex, static_cast<uint16>( depth + 1 ) );
        }
        for ( Component* pComp : pObj->getComponents() )
        {
            if ( pComp == nullptr )
                continue;
            // 행마다 확인하는 곳이라 리플렉션 캐스트 대신 생성자에서 설정한 비트를 본다.
            if ( pComp->isSceneComponent() )
            {
                const SceneComponent* pParent = static_cast<SceneComponent*>( pComp )->getParent();
                if ( pParent == nullptr || pParent->getOwner() != pObj ) // 같은 오브젝트의 부모 아래 것은 그 부모 행이 편다
                    appendSceneComponentRows( pObj, pComp, rowIndex, static_cast<uint16>( depth + 1 ) );
                continue;
            }
            HierarchyTreeRow componentRow{};
            componentRow._pObject    = pObj;
            componentRow._pComponent = pComp;
            componentRow._id         = pComp->getComponentID();
            componentRow._objectID   = row._id;
            componentRow._parentRow  = rowIndex;
            componentRow._depth      = static_cast<uint16>( depth + 1 );
            componentRow._kind       = HierarchyRowKind::Component;
            componentRow._bLeaf      = SW_TRUE;
            (void)pushRow( componentRow ); // 잎이라 번호를 쓰지 않는다
        }
    }

    void HierarchyRowModel::appendSceneComponentRows( GameObject* pObj, Component* pSceneComponent, uint32 parentRow, uint16 depth )
    {
        SceneComponent* const pScene = static_cast<SceneComponent*>( pSceneComponent );
        HierarchyTreeRow      row{};
        row._pObject          = pObj;
        row._pComponent       = pSceneComponent;
        row._id               = pSceneComponent->getComponentID();
        row._objectID         = pObj->getObjectID();
        row._parentRow        = parentRow;
        row._depth            = depth;
        row._kind             = HierarchyRowKind::SceneComponent;
        row._bLeaf            = HierarchyRowModelInternal::hasChildOnOwner( pObj, pScene ) == false;
        row._bExpanded        = row._bLeaf == false && _uniqueExpandedComponentID.contains( row._id );
        const uint32 rowIndex = pushRow( row );
        if ( row._bExpanded == false )
            return;
        for ( SceneComponent* pChild : pScene->getChildren() )
        {
            if ( pChild != nullptr && pChild->getOwner() == pObj )
                appendSceneComponentRows( pObj, pChild, rowIndex, static_cast<uint16>( depth + 1 ) );
        }
    }

    optional<uint32> HierarchyRowModel::findObjectRowIndex( uint64 objectID ) const
    {
        const optional<uint32> row = findRowOfObject( objectID );
        if ( row.has_value() == false )
            return nullopt;
        return _listRow[*row]._objectRowsBefore;
    }

    optional<uint32> HierarchyRowModel::findRowOfObject( uint64 objectID ) const
    {
        for ( size_t rowIndex = 0; rowIndex < _listRow.size(); ++rowIndex )
        {
            const HierarchyTreeRow& row = _listRow[rowIndex];
            if ( row._kind == HierarchyRowKind::Object && row._id == objectID )
                return static_cast<uint32>( rowIndex );
        }
        return nullopt;
    }

    void HierarchyRowModel::collectObjectRowIDs( vector<uint64>& outListID ) const
    {
        outListID.clear();
        outListID.reserve( _objectRowCount );
        for ( const HierarchyTreeRow& row : _listRow )
        {
            if ( row._kind == HierarchyRowKind::Object )
                outListID.push_back( row._id );
        }
    }

    float32 HierarchyRowModel::computeRowOffset( uint32 row, const HierarchyRowMetrics& stride ) const
    {
        const uint32 objectRows    = countObjectRowsBefore( row );
        const uint32 componentRows = row - objectRows;
        return static_cast<float32>( objectRows ) * stride._object + static_cast<float32>( componentRows ) * stride._component;
    }

    uint32 HierarchyRowModel::findRowAtOffset( float32 offset, const HierarchyRowMetrics& stride ) const
    {
        if ( _listRow.empty() )
            return 0;
        // 위치는 행 번호에 따라 늘어난다 — offset 이하인 마지막 행을 찾는다.
        uint32 low  = 0;
        uint32 high = static_cast<uint32>( _listRow.size() - 1 );
        while ( low < high )
        {
            const uint32 mid = low + ( high - low + 1 ) / 2;
            if ( computeRowOffset( mid, stride ) <= offset )
                low = mid;
            else
                high = mid - 1;
        }
        return low;
    }
} // namespace sw::editor
