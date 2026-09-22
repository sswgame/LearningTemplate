/**
 * @file TickRegistry.cpp
 * @brief 틱 등록부 구현 — 오브젝트 항목 재구축 · 그룹 멤버십 · 더티 표시.
 */
#include "pch.h"

#include "Engine/Object/GameObject/TickRegistry.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    TickRegistry::TickRegistry()
        : _arrListObject{}
        , _listDirtyObjectId{}
        , _listProcessingObjectId{}
        , _dirtyMutex{}
        , _bAllDirty{ SW_TRUE }
        , _prerequisiteCount{ 0 }
        , _generation{ 1 }
    {
    }

    void TickRegistry::markObjectDirty( GameObject* pObj )
    {
        if ( pObj == nullptr || pObj->_bTickDirty.exchange( SW_TRUE, std::memory_order_acq_rel ) != SW_FALSE )
            return;
        std::scoped_lock<mutex> lock{ _dirtyMutex };
        _listDirtyObjectId.push_back( pObj->getObjectId() );
    }

    void TickRegistry::markAllDirty()
    {
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    bool TickRegistry::refresh( GameObjectManager& manager )
    {
        const bool bAll = _bAllDirty.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE;
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listProcessingObjectId.swap( _listDirtyObjectId );
        }
        if ( bAll == false && _listProcessingObjectId.empty() )
            return false;

        if ( bAll )
        {
            manager.forEachGameObject( [this]( GameObject* pObj )
            {
                pObj->_bTickDirty.store( SW_FALSE, std::memory_order_relaxed );
                refreshObject( pObj );
            } );
            // 전부 훑었으니 개별 표시는 지운다 — 죽어 가는 오브젝트의 id 는 어차피 해석이 비었을 것이다.
            _listProcessingObjectId.clear();
            return true;
        }

        bool bRefreshedAny = false;
        for ( const uint64 objectId : _listProcessingObjectId )
        {
            // 삭제 대기면 비어 있다 — 그 오브젝트는 파괴 때 `unregisterObject` 로 빠진다.
            GameObject* pObj = manager.findGameObjectById( objectId );
            if ( pObj == nullptr )
                continue;
            pObj->_bTickDirty.store( SW_FALSE, std::memory_order_relaxed );
            refreshObject( pObj );
            bRefreshedAny = true;
        }
        _listProcessingObjectId.clear();
        return bRefreshedAny;
    }

    void TickRegistry::refreshObject( GameObject* pObj )
    {
        vector<TickItem>& listItem = pObj->_listTickItem;
        listItem.clear();

        uint32 prerequisiteCount = 0;
        for ( Component* pComp : pObj->_listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingKill() )
                continue;
            if ( pComp->canEverTick() )
            {
                const uint8 group = static_cast<uint8>( pComp->getTickGroup() );
                if ( group < kGroupCount )
                    listItem.push_back( TickItem{ pComp, 0, group, static_cast<uint8>( TickPhase::Normal ) } );
            }
            for ( const SubTickInfo& subTick : pComp->getAllSubTicks() )
            {
                if ( subTick._bActive != SW_TRUE )
                    continue;
                const uint8 group = static_cast<uint8>( subTick._group );
                if ( group >= kGroupCount )
                    continue;
                listItem.push_back( TickItem{ pComp, subTick._subTickId, group, static_cast<uint8>( static_cast<uint8>( subTick._phase ) + ( subTick._priority & 63 ) ) } );
                prerequisiteCount += static_cast<uint32>( subTick._listPrerequisite.size() );
            }
        }

        // (그룹, 순서 키) 순 — 같은 키끼리는 컴포넌트 순서 그대로(안정 정렬). 오브젝트 하나의 항목은 몇 개뿐이다.
        std::stable_sort( listItem.begin(), listItem.end(), []( const TickItem& left, const TickItem& right )
        {
            if ( left._group != right._group )
                return left._group < right._group;
            return left._orderKey < right._orderKey;
        } );

        uint32 cursor = 0;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            pObj->_arrTickGroupBegin[group] = cursor;
            while ( cursor < listItem.size() && listItem[cursor]._group == group )
                ++cursor;
        }
        pObj->_arrTickGroupBegin[kGroupCount] = cursor;
        for ( uint32 group = 0; group < kGroupCount; ++group )
            setMembership( pObj, group, pObj->_arrTickGroupBegin[group] < pObj->_arrTickGroupBegin[group + 1] );

        _prerequisiteCount           = _prerequisiteCount - pObj->_tickPrerequisiteCount + prerequisiteCount;
        pObj->_tickPrerequisiteCount = prerequisiteCount;
        ++_generation;
    }

    void TickRegistry::setMembership( GameObject* pObj, uint32 group, bool bMember )
    {
        vector<GameObject*>& listObject = _arrListObject[group];
        uint32&              index      = pObj->_arrTickIndex[group];
        const bool           bIsMember  = index != kNotInList && index < listObject.size() && listObject[index] == pObj;
        if ( bMember == bIsMember )
            return;
        if ( bMember )
        {
            index = static_cast<uint32>( listObject.size() );
            listObject.push_back( pObj );
            return;
        }
        GameObject* pMoved           = listObject.back();
        listObject[index]            = pMoved;
        pMoved->_arrTickIndex[group] = index;
        listObject.pop_back();
        index = kNotInList;
    }

    void TickRegistry::unregisterObject( GameObject* pObj )
    {
        if ( pObj == nullptr )
            return;
        bool bHadItem = false;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            bHadItem = bHadItem || pObj->_arrTickIndex[group] != kNotInList;
            setMembership( pObj, group, false );
        }
        _prerequisiteCount -= pObj->_tickPrerequisiteCount;
        pObj->_tickPrerequisiteCount = 0;
        pObj->_listTickItem.clear();
        if ( bHadItem )
            ++_generation;
    }

    void TickRegistry::clear()
    {
        for ( vector<GameObject*>& listObject : _arrListObject )
            listObject.clear();
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listDirtyObjectId.clear();
        }
        _listProcessingObjectId.clear();
        _prerequisiteCount = 0;
        ++_generation;
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }
} // namespace sw
