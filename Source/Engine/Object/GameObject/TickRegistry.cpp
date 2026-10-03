/**
 * @file TickRegistry.cpp
 * @brief 틱 등록부 구현입니다(오브젝트 항목 재구축 · 그룹 멤버십 · 더티 표시 · 선행 조건 스테이지).
 */
#include "pch.h"

#include "Engine/Object/GameObject/TickRegistry.h"

#include "Core/Container/unordered_map.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    static_assert( TickRegistry::kGroupCount == static_cast<uint32>( TickGroup::PostUpdate ) + 1, "TickRegistry::kGroupCount must match TickGroup" );

    namespace
    {
        struct TickRegistryInternal
        {
            /** @brief 선행 조건 스테이지를 지을 때의 후보 하나입니다(등록부 항목 + 그 항목의 선행 목록). */
            struct StageCandidate
            {
                TickItem                     _item;
                uint64                       _objectId{ 0 };
                uint64                       _componentId{ 0 };
                uint32                       _originalIndex{ 0 };
                const vector<SubTickHandle>* _pListPrerequisite{ nullptr }; ///< 서브틱의 선행 목록. 주 틱은 없습니다
            };

            /** @brief 순서 키로, 같으면 등록 순서로 비교합니다. 후보 목록을 정렬할 때의 유일한 규칙입니다. */
            static bool isBefore( const StageCandidate& left, const StageCandidate& right )
            {
                if ( left._item._orderKey != right._item._orderKey )
                    return left._item._orderKey < right._item._orderKey;
                return left._originalIndex < right._originalIndex;
            }

            /** @brief 항목의 서브틱 정보에서 선행 목록을 찾습니다. 주 틱이거나 없으면 nullptr. */
            static const vector<SubTickHandle>* findPrerequisiteList( const TickItem& item )
            {
                if ( item._subTickId == 0 || item._pComponent == nullptr )
                    return nullptr;
                for ( const SubTickInfo& subTick : item._pComponent->getAllSubTicks() )
                {
                    if ( subTick._subTickId == item._subTickId )
                        return &subTick._listPrerequisite;
                }
                return nullptr;
            }

            /**
             * @brief 한 레벨을 오브젝트별 스테이지로 가릅니다. 같은 오브젝트의 항목은 0 번부터 차례로 찹니다.
             * @details 오브젝트마다 "이미 든 스테이지 수" 하나면 됩니다. 같은 오브젝트의 항목이 붙어 있으면(보통) 그 수는
             *          이어지는 동안 하나씩 오르고 오브젝트가 바뀌면 0 입니다. 맵 없이 한 번에 됩니다.
             */
            static void splitByObject( const vector<size_t>& listLevel, const vector<StageCandidate>& listCandidate, vector<TickStage>& outListStage )
            {
                unordered_map<uint64, uint32> mapNextSlot;
                mapNextSlot.reserve( listLevel.size() );
                const size_t firstStage = outListStage.size();
                for ( const size_t candidateIndex : listLevel )
                {
                    const StageCandidate& candidate = listCandidate[candidateIndex];
                    uint32&               slot      = mapNextSlot[candidate._objectId];
                    if ( firstStage + slot == outListStage.size() )
                        outListStage.emplace_back();
                    outListStage[firstStage + slot].push_back( candidate._item );
                    ++slot;
                }
            }

            /**
             * @brief 후보들의 선행 조건 그래프(인접 목록 · 진입 차수)를 짓습니다. 간선 하나라도 그룹을 넘으면 true 입니다.
             * @details 선행 조건은 늘 서브틱을 가리키므로(`SubTickHandle::isValid`) 서브틱 후보만 찾는 표에 넣습니다. 찾을 수 없는 선행
             *          조건과 자기 자신은 순서를 만들지 않습니다.
             */
            static bool buildPrerequisiteGraph( const vector<StageCandidate>& listCandidate, vector<vector<size_t>>& outListAdjacent, vector<uint32>& outListInDegree )
            {
                const size_t                                            count = listCandidate.size();
                unordered_map<SubTickHandle, size_t, SubTickHandleHash> mapLookup;
                mapLookup.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( listCandidate[index]._item._subTickId != 0 )
                        mapLookup[{ listCandidate[index]._componentId, listCandidate[index]._item._subTickId }] = index;
                }

                outListAdjacent.assign( count, vector<size_t>{} );
                outListInDegree.assign( count, 0 );
                bool bCrossesGroup = false;
                for ( size_t index = 0; index < count; ++index )
                {
                    const vector<SubTickHandle>* pListPrerequisite = listCandidate[index]._pListPrerequisite;
                    if ( pListPrerequisite == nullptr )
                        continue;
                    for ( const SubTickHandle& prerequisite : *pListPrerequisite )
                    {
                        const auto found = mapLookup.find( prerequisite );
                        if ( found == mapLookup.end() || found->second == index )
                            continue;
                        outListAdjacent[found->second].push_back( index );
                        ++outListInDegree[index];
                        bCrossesGroup = bCrossesGroup || listCandidate[found->second]._item._group != listCandidate[index]._item._group;
                    }
                }
                return bCrossesGroup;
            }

            /**
             * @brief 선행 조건이 뒤 그룹에 있는 후보를 그 그룹으로 옮깁니다. 사슬을 따라 옮깁니다(언리얼 `QueueTickFunction` 의 `ActualStartTickGroup`).
             * @details 모든 그룹의 후보를 한 그래프로 보고 위상 순서(Kahn)로 걸으며 "내 그룹 = max(내 그룹, 선행의 그룹)" 을 적습니다. 앞 그룹의
             *          선행 조건은 이미 끝났으므로 아무것도 바꾸지 않습니다. 예전에는 그룹마다 따로 DAG 를 지어 다른 그룹의 선행 조건을 "찾을 수
             *          없음" 으로 버렸습니다 — PrePhysics 의 기수가 PostPhysics 의 말보다 먼저 돌았습니다. 순환에 걸린 후보(와 그 뒤)는 위상
             *          순서가 없으니 제 그룹에 둡니다(순환은 그룹 안에서 순서 키로 방어합니다).
             */
            static void raiseGroupsToPrerequisites( vector<StageCandidate>& listCandidate )
            {
                const size_t           count = listCandidate.size();
                vector<vector<size_t>> listAdjacent;
                vector<uint32>         listInDegree;
                // 모든 선행 조건이 같은 그룹 안이면(보통) 옮길 것이 없다. 사슬이 그룹을 넘으려면 어느 간선 하나는 그룹을 넘는다.
                if ( buildPrerequisiteGraph( listCandidate, listAdjacent, listInDegree ) == false )
                    return;

                vector<size_t> listReady;
                listReady.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( listInDegree[index] == 0 )
                        listReady.push_back( index );
                }
                for ( size_t cursor = 0; cursor < listReady.size(); ++cursor )
                {
                    const size_t current = listReady[cursor];
                    const uint8  group   = listCandidate[current]._item._group;
                    for ( const size_t next : listAdjacent[current] )
                    {
                        uint8& nextGroup = listCandidate[next]._item._group;
                        if ( nextGroup < group )
                            nextGroup = group;
                        if ( --listInDegree[next] == 0 )
                            listReady.push_back( next );
                    }
                }
            }

            /** @brief 한 그룹의 후보를 선행 조건 순서로 갈라 스테이지를 붙입니다(Kahn 레벨 + 오브젝트별 스테이지). */
            static void appendGroupStages( vector<StageCandidate>& listCandidate, vector<TickStage>& outListStage )
            {
                const size_t count = listCandidate.size();
                if ( count == 0 )
                    return;

                vector<vector<size_t>> listAdjacent;
                vector<uint32>         listInDegree;
                (void)buildPrerequisiteGraph( listCandidate, listAdjacent, listInDegree );

                auto sortLevel = [&listCandidate]( vector<size_t>& listLevel )
                {
                    std::stable_sort( listLevel.begin(), listLevel.end(), [&listCandidate]( size_t left, size_t right )
                    { return isBefore( listCandidate[left], listCandidate[right] ); } );
                };

                vector<size_t> listCurrentLevel;
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( listInDegree[index] == 0 )
                        listCurrentLevel.push_back( index );
                }

                vector<bool> listVisited( count, false );
                size_t       processedCount = 0;
                while ( listCurrentLevel.empty() == false )
                {
                    sortLevel( listCurrentLevel );
                    vector<size_t> listNextLevel;
                    for ( const size_t current : listCurrentLevel )
                    {
                        listVisited[current] = true;
                        ++processedCount;
                        for ( const size_t next : listAdjacent[current] )
                        {
                            if ( --listInDegree[next] == 0 )
                                listNextLevel.push_back( next );
                        }
                    }
                    splitByObject( listCurrentLevel, listCandidate, outListStage );
                    listCurrentLevel = std::move( listNextLevel );
                }

                // 순환 방어: 닿지 못한 것은 순서 키 순으로 마지막에 붙인다. 틱이 조용히 빠지는 것보다 낫다.
                if ( processedCount < count )
                {
                    vector<size_t> listRemaining;
                    for ( size_t index = 0; index < count; ++index )
                    {
                        if ( listVisited[index] == false )
                            listRemaining.push_back( index );
                    }
                    sortLevel( listRemaining );
                    splitByObject( listRemaining, listCandidate, outListStage );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TickRegistry::TickRegistry()
        : _arrListEntry{}
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
            // 모두 훑었으니 개별 표시는 지운다. 죽어 가는 오브젝트의 id 는 어차피 해석이 비었을 것이다.
            _listProcessingObjectId.clear();
            return true;
        }

        bool bRefreshedAny = false;
        for ( const uint64 objectId : _listProcessingObjectId )
        {
            // 삭제 대기면 비어 있다. 그 오브젝트는 파괴 때 `unregisterObject` 로 빠진다.
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
        TickItemList& listItem = pObj->_listTickItem;
        listItem.clear();

        uint32 prerequisiteCount = 0;
        for ( Component* pComp : pObj->_listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingDestroy() )
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
                // 우선순위는 등록할 때 `kMaxTickPriority` 로 묶였다 — 단계(64 칸)를 넘지 않는다.
                listItem.push_back( TickItem{ pComp, subTick._subTickId, group, static_cast<uint8>( static_cast<uint8>( subTick._phase ) + subTick._priority ) } );
                prerequisiteCount += static_cast<uint32>( subTick._listPrerequisite.size() );
            }
        }

        // (그룹, 순서 키) 순으로 정렬한다. 같은 키끼리는 컴포넌트 순서 그대로다(안정 정렬). 오브젝트 하나의 항목은 몇 개뿐이다.
        std::stable_sort( listItem.begin(), listItem.end(), []( const TickItem& left, const TickItem& right )
        {
            if ( left._group != right._group )
                return left._group < right._group;
            return left._orderKey < right._orderKey;
        } );

        // 그룹마다 항목이 시작하는 자리. 칸을 짓는 데만 쓰므로 오브젝트에 들지 않는다(예전에는 오브젝트의 20 바이트였다 — 디스패치가 그것을 읽었다).
        uint32 arrGroupBegin[kGroupCount + 1] = {};
        uint32 cursor                         = 0;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            arrGroupBegin[group] = cursor;
            while ( cursor < listItem.size() && listItem[cursor]._group == group )
                ++cursor;
        }
        arrGroupBegin[kGroupCount] = cursor;

        // 계층에서 꺼진 오브젝트는 목록에 두지 않는다(언리얼 · 유니티가 꺼진 것의 틱을 목록에서 빼는 것과 같다). 켜지고 꺼질 때
        // `GameObject::refreshActiveInHierarchy` 가 표시하므로 다음 틱 전에 여기로 다시 온다. 항목 버퍼가 다시 지어졌으므로 칸도 새로 쓴다.
        const bool bActive = pObj->isActiveInHierarchy();
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            const uint32    begin = arrGroupBegin[group];
            const uint32    end   = arrGroupBegin[group + 1];
            TickObjectEntry entry{};
            if ( begin < end )
            {
                entry._pObject   = pObj;
                entry._firstItem = listItem[begin];
                entry._pItem     = listItem.data() + begin;
                entry._itemCount = end - begin;
            }
            setMembership( pObj, group, bActive && begin < end, entry );
        }

        _prerequisiteCount           = _prerequisiteCount - pObj->_tickPrerequisiteCount + prerequisiteCount;
        pObj->_tickPrerequisiteCount = prerequisiteCount;
        ++_generation;
    }

    void TickRegistry::setMembership( GameObject* pObj, uint32 group, bool bMember, const TickObjectEntry& entry )
    {
        vector<TickObjectEntry>& listEntry = _arrListEntry[group];
        uint32&                  index     = pObj->_arrTickIndex[group];
        const bool               bIsMember = index != kNotInList && index < listEntry.size() && listEntry[index]._pObject == pObj;
        if ( bMember )
        {
            // 이미 있으면 그 자리의 칸만 새로 쓴다(항목 버퍼가 다시 지어졌다).
            if ( bIsMember == false )
            {
                index = static_cast<uint32>( listEntry.size() );
                listEntry.push_back( entry );
            }
            else
                listEntry[index] = entry;
            return;
        }
        if ( bIsMember == false )
            return;
        const TickObjectEntry moved          = listEntry.back();
        listEntry[index]                     = moved;
        moved._pObject->_arrTickIndex[group] = index;
        listEntry.pop_back();
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
            setMembership( pObj, group, false, TickObjectEntry{} );
        }
        _prerequisiteCount -= pObj->_tickPrerequisiteCount;
        pObj->_tickPrerequisiteCount = 0;
        pObj->_listTickItem.clear();
        if ( bHadItem )
            ++_generation;
    }

    void TickRegistry::clear()
    {
        for ( vector<TickObjectEntry>& listEntry : _arrListEntry )
            listEntry.clear();
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listDirtyObjectId.clear();
        }
        _listProcessingObjectId.clear();
        _prerequisiteCount = 0;
        ++_generation;
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    void TickRegistry::computePrerequisiteStages( vector<TickStage>& outListStage ) const
    {
        outListStage.clear();
        // 모든 그룹의 후보를 한 번에 모은다. 선행 조건은 그룹을 넘을 수 있다.
        vector<TickRegistryInternal::StageCandidate> listCandidate;
        uint32                                       originalIndex = 0;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            for ( const TickObjectEntry& entry : _arrListEntry[group] )
            {
                GameObject* pObj = entry._pObject;
                if ( pObj == nullptr || pObj->isPendingDestroy() )
                    continue;
                for ( uint32 index = 0; index < entry._itemCount; ++index )
                {
                    const TickItem& item = entry._pItem[index];
                    if ( item._pComponent == nullptr || item._pComponent->isPendingDestroy() )
                        continue;
                    TickRegistryInternal::StageCandidate candidate{};
                    candidate._item              = item;
                    candidate._objectId          = pObj->getObjectId();
                    candidate._componentId       = item._pComponent->getComponentId();
                    candidate._originalIndex     = originalIndex++;
                    candidate._pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
                    listCandidate.push_back( candidate );
                }
            }
        }
        TickRegistryInternal::raiseGroupsToPrerequisites( listCandidate );

        vector<TickRegistryInternal::StageCandidate> listGroupCandidate;
        listGroupCandidate.reserve( listCandidate.size() );
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            listGroupCandidate.clear();
            for ( const TickRegistryInternal::StageCandidate& candidate : listCandidate )
            {
                if ( candidate._item._group == group )
                    listGroupCandidate.push_back( candidate );
            }
            TickRegistryInternal::appendGroupStages( listGroupCandidate, outListStage );
        }
    }
} // namespace sw
