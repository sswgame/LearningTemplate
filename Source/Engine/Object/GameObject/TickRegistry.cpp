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
            /** @brief 물리 뒤 단계의 첫 그룹입니다. 같은 단계의 그룹끼리는 그 사이에 틱 결과 적용이 없습니다. */
            static constexpr uint8 kPostPhysicsGroup = static_cast<uint8>( TickGroup::PostPhysics );
            /** @brief 바깥(보통 길) 선행 조건이 없다는 표시입니다. */
            static constexpr uint8 kNoExternalGroup = 0xFF;

            /** @brief 선행 조건 스테이지를 지을 때의 후보 하나입니다(선행 조건을 가진 항목 + 그 선행 목록). */
            struct StageCandidate
            {
                TickItem                     _item;
                uint64                       _objectId{ 0 };
                uint64                       _componentId{ 0 };
                uint32                       _originalIndex{ 0 };
                const vector<SubTickHandle>* _pListPrerequisite{ nullptr };      ///< 서브틱의 선행 목록
                uint8                        _externalGroup{ kNoExternalGroup }; ///< 보통 길에서 도는 선행 조건 중 가장 늦은 그룹
            };

            /** @brief 그룹의 단계 첫 그룹입니다(물리 앞이면 0, 뒤면 PostPhysics). */
            static uint8 getPhaseFirstGroup( uint8 group ) { return group >= kPostPhysicsGroup ? kPostPhysicsGroup : 0; }

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
             * @brief 보통 길에서 도는 선행 조건 항목의 그룹을 찾습니다. 오브젝트가 없거나 꺼졌거나 그 항목이 없으면 `kNoExternalGroup` 입니다.
             * @details 핸들이 든 오브젝트 id 로 찾고 그 오브젝트의 항목만 훑습니다(씬 전체를 보지 않는다).
             */
            static uint8 findExternalGroup( GameObjectManager& manager, const SubTickHandle& handle )
            {
                GameObject* pObj = manager.findGameObjectById( handle._objectId );
                if ( pObj == nullptr || pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false )
                    return kNoExternalGroup;
                for ( const TickItem& item : pObj->getTickItems() )
                {
                    if ( item._pComponent != nullptr && item._pComponent->getComponentId() == handle._componentId && item._subTickId == handle._subTickId &&
                         item._pComponent->isPendingDestroy() == false )
                        return item._group;
                }
                return kNoExternalGroup;
            }

            /**
             * @brief 한 레벨을 오브젝트별 스테이지로 가릅니다. 같은 오브젝트의 항목은 0 번부터 차례로 찹니다.
             * @details 오브젝트마다 "이미 든 스테이지 수" 하나면 됩니다. 같은 오브젝트의 항목이 붙어 있으면(보통) 그 수는
             *          이어지는 동안 하나씩 오르고 오브젝트가 바뀌면 0 입니다. 맵 없이 한 번에 됩니다.
             *          @p bWaits 면 레벨의 첫 스테이지에만 "돌기 전에 적용" 을 세웁니다 — 같은 레벨의 항목은 서로 기다리지 않으므로 둘째부터는
             *          이미 적용된 값을 봅니다.
             */
            static void splitByObject( const vector<size_t>& listLevel, const vector<StageCandidate>& listCandidate, bool bWaits, vector<TickStage>& outListStage )
            {
                unordered_map<uint64, uint32> mapNextSlot;
                mapNextSlot.reserve( listLevel.size() );
                const size_t firstStage = outListStage.size();
                for ( const size_t candidateIndex : listLevel )
                {
                    const StageCandidate& candidate = listCandidate[candidateIndex];
                    uint32&               slot      = mapNextSlot[candidate._objectId];
                    if ( firstStage + slot == outListStage.size() )
                    {
                        TickStage& stage    = outListStage.emplace_back();
                        stage._group        = candidate._item._group;
                        stage._bApplyBefore = ( bWaits && slot == 0 ) ? SW_TRUE : SW_FALSE;
                    }
                    outListStage[firstStage + slot]._listItem.push_back( candidate._item );
                    ++slot;
                }
            }

            /**
             * @brief 후보들 사이의 선행 조건 그래프(인접 목록 · 진입 차수)를 짓습니다. 후보에 없는 선행 조건은 여기서 보지 않습니다(보통 길 — `findExternalGroup`).
             * @details 찾을 수 없는 선행 조건과 자기 자신은 순서를 만들지 않습니다.
             */
            static void buildPrerequisiteGraph( const vector<StageCandidate>& listCandidate, vector<vector<size_t>>& outListAdjacent, vector<uint32>& outListInDegree )
            {
                const size_t                                            count = listCandidate.size();
                unordered_map<SubTickHandle, size_t, SubTickHandleHash> mapLookup;
                mapLookup.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    mapLookup[SubTickHandle{ listCandidate[index]._componentId, listCandidate[index]._item._subTickId, 0 }] = index;
                }

                outListAdjacent.assign( count, vector<size_t>{} );
                outListInDegree.assign( count, 0 );
                for ( size_t index = 0; index < count; ++index )
                {
                    for ( const SubTickHandle& prerequisite : *listCandidate[index]._pListPrerequisite )
                    {
                        const auto found = mapLookup.find( prerequisite );
                        if ( found == mapLookup.end() || found->second == index )
                            continue;
                        outListAdjacent[found->second].push_back( index );
                        ++outListInDegree[index];
                    }
                }
            }

            /**
             * @brief 선행 조건이 뒤 그룹에 있는 후보를 그 그룹으로 옮깁니다. 사슬을 따라 옮깁니다(언리얼 `QueueTickFunction` 의 `ActualStartTickGroup`).
             * @details 보통 길의 선행 조건은 그 그룹(`_externalGroup`)으로 먼저 올리고, 후보 사이의 간선은 위상 순서(Kahn)로 걸으며 "내 그룹 =
             *          max(내 그룹, 선행의 그룹)" 을 적습니다. 앞 그룹의 선행 조건은 이미 끝났으므로 아무것도 바꾸지 않습니다. 순환에 걸린 후보(와 그
             *          뒤)는 위상 순서가 없으니 제 그룹에 둡니다(순환은 그룹 안에서 순서 키로 방어합니다).
             */
            static void raiseGroupsToPrerequisites( vector<StageCandidate>& listCandidate, const vector<vector<size_t>>& listAdjacent, vector<uint32> listInDegree )
            {
                const size_t count = listCandidate.size();
                for ( StageCandidate& candidate : listCandidate )
                {
                    if ( candidate._externalGroup != kNoExternalGroup && candidate._item._group < candidate._externalGroup )
                        candidate._item._group = candidate._externalGroup;
                }

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

            /**
             * @brief 한 그룹의 후보를 선행 조건 순서로 갈라 스테이지를 붙입니다(Kahn 레벨 + 오브젝트별 스테이지).
             * @details 레벨 0 은 후보 사이의 선행 조건이 없지만, 보통 길의 선행 조건이 같은 단계에서 돌았으면(그 쓰기가 아직 대기 중이다) 그 레벨도
             *          돌기 전에 적용합니다. 레벨 1 부터는 늘 앞 레벨을 기다립니다.
             */
            static void appendGroupStages( vector<StageCandidate>& listCandidate, vector<TickStage>& outListStage )
            {
                const size_t count = listCandidate.size();
                if ( count == 0 )
                    return;

                vector<vector<size_t>> listAdjacent;
                vector<uint32>         listInDegree;
                buildPrerequisiteGraph( listCandidate, listAdjacent, listInDegree );

                auto sortLevel = [&listCandidate]( vector<size_t>& listLevel )
                {
                    std::stable_sort( listLevel.begin(), listLevel.end(), [&listCandidate]( size_t left, size_t right )
                    { return isBefore( listCandidate[left], listCandidate[right] ); } );
                };
                // 보통 길의 선행 조건이 이 단계(같은 단계의 이 그룹 또는 앞 그룹)에서 돌았으면 그 쓰기를 먼저 적용해야 한다.
                auto waitsOnObjectPath = [&listCandidate]( const vector<size_t>& listLevel )
                {
                    for ( const size_t index : listLevel )
                    {
                        const StageCandidate& candidate = listCandidate[index];
                        if ( candidate._externalGroup != kNoExternalGroup && candidate._externalGroup >= getPhaseFirstGroup( candidate._item._group ) )
                            return true;
                    }
                    return false;
                };

                vector<size_t> listCurrentLevel;
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( listInDegree[index] == 0 )
                        listCurrentLevel.push_back( index );
                }

                vector<bool> listVisited( count, false );
                size_t       processedCount = 0;
                bool         bWaits         = false; // 레벨 1 부터는 모두 앞 레벨의 선행 조건을 기다린다
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
                    splitByObject( listCurrentLevel, listCandidate, bWaits || waitsOnObjectPath( listCurrentLevel ), outListStage );
                    bWaits           = true;
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
                    splitByObject( listRemaining, listCandidate, true, outListStage );
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
        , _stageItemCount{ 0 }
        , _stageBuildCount{ 0 }
        , _stageGeneration{ 1 }
        , _builtStageGeneration{ 0 }
        , _uniqueDependentObjectId{}
        , _uniqueReferencedObjectId{}
        , _listStage{}
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
        bool bRefreshedAny = false;
        if ( bAll )
        {
            manager.forEachGameObject( [this]( GameObject* pObj )
            {
                pObj->_bTickDirty.store( SW_FALSE, std::memory_order_relaxed );
                refreshObject( pObj );
            } );
            // 모두 훑었으니 개별 표시는 지운다. 죽어 가는 오브젝트의 id 는 어차피 해석이 비었을 것이다.
            _listProcessingObjectId.clear();
            bRefreshedAny = true;
        }
        else
        {
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
        }

        // 선행 조건에 걸린 오브젝트(가진 쪽 · 가리켜진 쪽)가 바뀌었을 때만 스테이지를 다시 짓는다 — 스폰이 잦아도 사슬 밖이면 오르지 않는다.
        if ( _builtStageGeneration != _stageGeneration )
            buildStages( manager );
        return bRefreshedAny;
    }

    void TickRegistry::refreshObject( GameObject* pObj )
    {
        const uint64  objectId   = pObj->getObjectId();
        const bool    bWasLinked = _uniqueDependentObjectId.contains( objectId ) || _uniqueReferencedObjectId.contains( objectId );
        TickItemList& listItem   = pObj->_listTickItem;
        listItem.clear();

        uint32 prerequisiteCount = 0;
        uint32 dependentCount    = 0;
        for ( Component* pComp : pObj->_listComponent )
        {
            if ( pComp == nullptr || pComp->isPendingDestroy() )
                continue;
            if ( pComp->canEverTick() )
            {
                const uint8 group = static_cast<uint8>( pComp->getTickGroup() );
                if ( group < kGroupCount )
                    listItem.push_back( TickItem{ pComp, 0, group, static_cast<uint8>( TickPhase::Normal ), SW_FALSE } );
            }
            for ( const SubTickInfo& subTick : pComp->getAllSubTicks() )
            {
                if ( subTick._bActive != SW_TRUE )
                    continue;
                const uint8 group = static_cast<uint8>( subTick._group );
                if ( group >= kGroupCount )
                    continue;
                const bool bHasPrerequisite = subTick._listPrerequisite.empty() == false;
                // 우선순위는 등록할 때 `kMaxTickPriority` 로 묶였다 — 단계(64 칸)를 넘지 않는다.
                listItem.push_back( TickItem{ pComp, subTick._subTickId, group, static_cast<uint8>( static_cast<uint8>( subTick._phase ) + subTick._priority ),
                                              static_cast<uint8>( bHasPrerequisite ? SW_TRUE : SW_FALSE ) } );
                prerequisiteCount += static_cast<uint32>( subTick._listPrerequisite.size() );
                dependentCount += bHasPrerequisite ? 1u : 0u;
            }
        }

        // (그룹, 선행 조건 여부, 순서 키) 순으로 정렬한다. 같은 키끼리는 컴포넌트 순서 그대로다(안정 정렬). 선행 조건을 가진 항목은 그룹의
        // 끝에 모인다 — 칸은 그 앞까지만 들고, 뒤는 스테이지가 돈다. 오브젝트 하나의 항목은 몇 개뿐이다.
        std::stable_sort( listItem.begin(), listItem.end(), []( const TickItem& left, const TickItem& right )
        {
            if ( left._group != right._group )
                return left._group < right._group;
            if ( left._bHasPrerequisite != right._bHasPrerequisite )
                return left._bHasPrerequisite < right._bHasPrerequisite;
            return left._orderKey < right._orderKey;
        } );

        // 계층에서 꺼진 오브젝트는 목록에 두지 않는다(언리얼 · 유니티가 꺼진 것의 틱을 목록에서 빼는 것과 같다). 켜지고 꺼질 때
        // `GameObject::refreshActiveInHierarchy` 가 표시하므로 다음 틱 전에 여기로 다시 온다. 항목 버퍼가 다시 지어졌으므로 칸도 새로 쓴다.
        const bool bActive = pObj->isActiveInHierarchy();
        uint32     cursor  = 0;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            const uint32 begin = cursor;
            while ( cursor < listItem.size() && listItem[cursor]._group == group && listItem[cursor]._bHasPrerequisite == SW_FALSE )
            {
                ++cursor;
            }
            const uint32 end = cursor;
            while ( cursor < listItem.size() && listItem[cursor]._group == group )
            {
                ++cursor;
            }
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
        if ( dependentCount > 0 )
            _uniqueDependentObjectId.insert( objectId );
        else
            _uniqueDependentObjectId.erase( objectId );
        if ( bWasLinked || dependentCount > 0 )
            ++_stageGeneration;
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
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            setMembership( pObj, group, false, TickObjectEntry{} );
        }
        const uint64 objectId = pObj->getObjectId();
        // 스테이지가 이 오브젝트의 항목을 들고 있거나(선행 조건을 가진 쪽) 이 오브젝트를 기다린다(가리켜진 쪽) — 다음 틱 전에 다시 짓는다.
        if ( _uniqueDependentObjectId.erase( objectId ) > 0 || _uniqueReferencedObjectId.contains( objectId ) )
            ++_stageGeneration;
        _prerequisiteCount -= pObj->_tickPrerequisiteCount;
        pObj->_tickPrerequisiteCount = 0;
        pObj->_listTickItem.clear();
    }

    void TickRegistry::clear()
    {
        for ( vector<TickObjectEntry>& listEntry : _arrListEntry )
        {
            listEntry.clear();
        }
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listDirtyObjectId.clear();
        }
        _listProcessingObjectId.clear();
        _prerequisiteCount = 0;
        _uniqueDependentObjectId.clear();
        _uniqueReferencedObjectId.clear();
        _listStage.clear();
        _stageItemCount = 0;
        ++_stageGeneration;
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    void TickRegistry::buildStages( GameObjectManager& manager )
    {
        _builtStageGeneration = _stageGeneration;
        ++_stageBuildCount;
        _listStage.clear();
        _uniqueReferencedObjectId.clear();
        _stageItemCount = 0;
        if ( _uniqueDependentObjectId.empty() )
            return;

        // 집합의 순서는 정해져 있지 않다 — 오브젝트 id(만든 순서) 순으로 모아 같은 씬이 늘 같은 스테이지를 짓게 한다.
        vector<uint64> listDependentId( _uniqueDependentObjectId.begin(), _uniqueDependentObjectId.end() );
        std::sort( listDependentId.begin(), listDependentId.end() );

        vector<TickRegistryInternal::StageCandidate> listCandidate;
        uint32                                       originalIndex = 0;
        for ( const uint64 objectId : listDependentId )
        {
            GameObject* pObj = manager.findGameObjectById( objectId );
            if ( pObj == nullptr || pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false )
                continue;
            for ( const TickItem& item : pObj->_listTickItem )
            {
                if ( item._bHasPrerequisite == SW_FALSE || item._pComponent == nullptr || item._pComponent->isPendingDestroy() )
                    continue;
                const vector<SubTickHandle>* pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
                if ( pListPrerequisite == nullptr )
                    continue;
                TickRegistryInternal::StageCandidate candidate{};
                candidate._item              = item;
                candidate._objectId          = objectId;
                candidate._componentId       = item._pComponent->getComponentId();
                candidate._originalIndex     = originalIndex++;
                candidate._pListPrerequisite = pListPrerequisite;
                listCandidate.push_back( candidate );
            }
        }

        // 후보 사이가 아닌 선행 조건은 보통 길에서 돈다 — 그 그룹을 찾아 둔다. 가리켜진 오브젝트는 모두 적어, 그것이 바뀌면 다시 짓는다.
        unordered_map<SubTickHandle, size_t, SubTickHandleHash> mapCandidate;
        mapCandidate.reserve( listCandidate.size() );
        for ( size_t index = 0; index < listCandidate.size(); ++index )
        {
            mapCandidate[SubTickHandle{ listCandidate[index]._componentId, listCandidate[index]._item._subTickId, 0 }] = index;
        }
        for ( TickRegistryInternal::StageCandidate& candidate : listCandidate )
        {
            for ( const SubTickHandle& prerequisite : *candidate._pListPrerequisite )
            {
                _uniqueReferencedObjectId.insert( prerequisite._objectId );
                if ( mapCandidate.contains( prerequisite ) )
                    continue;
                const uint8 externalGroup = TickRegistryInternal::findExternalGroup( manager, prerequisite );
                if ( externalGroup != TickRegistryInternal::kNoExternalGroup &&
                     ( candidate._externalGroup == TickRegistryInternal::kNoExternalGroup || candidate._externalGroup < externalGroup ) )
                    candidate._externalGroup = externalGroup;
            }
        }

        {
            vector<vector<size_t>> listAdjacent;
            vector<uint32>         listInDegree;
            TickRegistryInternal::buildPrerequisiteGraph( listCandidate, listAdjacent, listInDegree );
            TickRegistryInternal::raiseGroupsToPrerequisites( listCandidate, listAdjacent, std::move( listInDegree ) );
        }

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
            TickRegistryInternal::appendGroupStages( listGroupCandidate, _listStage );
        }
        _stageItemCount = static_cast<uint32>( listCandidate.size() );
    }
} // namespace sw
