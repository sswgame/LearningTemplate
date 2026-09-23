/**
 * @file TickRegistry.cpp
 * @brief 틱 등록부 구현 — 오브젝트 항목 재구축 · 그룹 멤버십 · 더티 표시 · 선행 종속성 웨이브.
 */
#include "pch.h"

#include "Engine/Object/GameObject/TickRegistry.h"

#include "Core/Container/unordered_map.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct TickRegistryInternal
        {
            /** @brief 선행 종속성 웨이브를 지을 때의 후보 하나 — 등록부 항목 + 그 항목의 선행 목록. */
            struct WaveCandidate
            {
                TickItem                     _item;
                uint64                       _objectId{ 0 };
                uint64                       _componentId{ 0 };
                uint32                       _originalIndex{ 0 };
                const vector<SubTickHandle>* _pListPrerequisite{ nullptr }; ///< 서브틱의 선행 목록. 주 틱은 없다
            };

            /** @brief 순서 키, 같으면 등록 순서. 후보 목록을 정렬할 때의 유일한 규칙이다. */
            static bool isBefore( const WaveCandidate& left, const WaveCandidate& right )
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
             * @brief 한 웨이브(같은 레벨)를 오브젝트별 서브웨이브로 가릅니다 — 같은 오브젝트의 항목은 0 번부터 차례로 찬다.
             * @details 오브젝트마다 "이미 든 서브웨이브 수" 하나면 된다. 같은 오브젝트의 항목이 붙어 있으면(보통) 그 수는
             *          이어지는 동안 하나씩 오르고 오브젝트가 바뀌면 0 이다 — 맵 없이 한 번에 된다.
             */
            static void splitByObject( const vector<size_t>& listLevel, const vector<WaveCandidate>& listCandidate, vector<TickWave>& outListWave )
            {
                unordered_map<uint64, uint32> mapNextSlot;
                mapNextSlot.reserve( listLevel.size() );
                const size_t firstWave = outListWave.size();
                for ( const size_t candidateIndex : listLevel )
                {
                    const WaveCandidate& candidate = listCandidate[candidateIndex];
                    uint32&              slot      = mapNextSlot[candidate._objectId];
                    if ( firstWave + slot == outListWave.size() )
                        outListWave.emplace_back();
                    outListWave[firstWave + slot].push_back( candidate._item );
                    ++slot;
                }
            }

            /** @brief 한 그룹의 후보를 선행 종속성 순서로 갈라 웨이브를 붙입니다 (Kahn 레벨 + 오브젝트별 서브웨이브). */
            static void appendGroupWaves( vector<WaveCandidate>& listCandidate, vector<TickWave>& outListWave )
            {
                const size_t count = listCandidate.size();
                if ( count == 0 )
                    return;

                // SubTickHandle -> 후보 인덱스. 선행이 가리키는 상대를 찾는다.
                unordered_map<SubTickHandle, size_t, SubTickHandleHash> mapLookup;
                mapLookup.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    mapLookup[{ listCandidate[index]._componentId, listCandidate[index]._item._subTickId }] = index;
                }

                vector<vector<size_t>> listAdjacent( count );
                vector<uint32>         listInDegree( count, 0 );
                for ( size_t index = 0; index < count; ++index )
                {
                    const vector<SubTickHandle>* pListPrerequisite = listCandidate[index]._pListPrerequisite;
                    if ( pListPrerequisite == nullptr )
                        continue;
                    for ( const SubTickHandle& prerequisite : *pListPrerequisite )
                    {
                        const auto found = mapLookup.find( prerequisite );
                        if ( found == mapLookup.end() || found->second == index )
                            continue; // 없는 상대(적혀는 있지만 아무도 못 찾는 종속성)와 자기 자신은 순서를 만들지 않는다
                        listAdjacent[found->second].push_back( index );
                        ++listInDegree[index];
                    }
                }

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
                    splitByObject( listCurrentLevel, listCandidate, outListWave );
                    listCurrentLevel = std::move( listNextLevel );
                }

                // 순환 방어: 못 온 것은 순서 키 순으로 마지막에 붙인다 — 틱이 조용히 빠지는 것보다 낫다.
                if ( processedCount < count )
                {
                    vector<size_t> listRemaining;
                    for ( size_t index = 0; index < count; ++index )
                    {
                        if ( listVisited[index] == false )
                            listRemaining.push_back( index );
                    }
                    sortLevel( listRemaining );
                    splitByObject( listRemaining, listCandidate, outListWave );
                }
            }
        };
    } // namespace
} // namespace sw

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
        TickItemList& listItem = pObj->_listTickItem;
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

    void TickRegistry::buildPrerequisiteWaves( vector<TickWave>& outListWave ) const
    {
        outListWave.clear();
        vector<TickRegistryInternal::WaveCandidate> listCandidate;
        uint32                                      originalIndex = 0;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            listCandidate.clear();
            for ( GameObject* pObj : _arrListObject[group] )
            {
                if ( pObj == nullptr || pObj->isPendingKill() )
                    continue;
                const TickItemList& listItem = pObj->getTickItems();
                const uint32        end      = pObj->getTickGroupBegin( group + 1 );
                for ( uint32 index = pObj->getTickGroupBegin( group ); index < end; ++index )
                {
                    const TickItem& item = listItem[index];
                    if ( item._pComponent == nullptr || item._pComponent->isPendingKill() )
                        continue;
                    TickRegistryInternal::WaveCandidate candidate{};
                    candidate._item              = item;
                    candidate._objectId          = pObj->getObjectId();
                    candidate._componentId       = item._pComponent->getComponentId();
                    candidate._originalIndex     = originalIndex++;
                    candidate._pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
                    listCandidate.push_back( candidate );
                }
            }
            TickRegistryInternal::appendGroupWaves( listCandidate, outListWave );
        }
    }
} // namespace sw
