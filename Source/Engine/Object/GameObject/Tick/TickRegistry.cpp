/**
 * @file TickRegistry.cpp
 * @brief 틱 레지스트리 구현입니다(오브젝트 항목 재구축 · 그룹 멤버십 · 더티 표시 · 선행 조건 스테이지).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/TickRegistry.h"

#include "Core/Common/HashUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"

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
                uint64                       _objectID{ 0 };
                uint32                       _objectOrdinal{ 0 }; ///< 후보를 낸 오브젝트의 순번(0 부터, 후보가 있는 오브젝트만)
                uint64                       _componentID{ 0 };
                uint32                       _originalIndex{ 0 };
                const vector<SubTickHandle>* _pListPrerequisite{ nullptr };      ///< 서브틱의 선행 목록
                uint8                        _externalGroup{ kNoExternalGroup }; ///< 보통 길에서 도는 선행 조건 중 가장 늦은 그룹
            };

            /** @brief 정렬된 id 목록에 @p id 를 넣습니다(이미 있으면 그대로). 새 오브젝트는 id 가 가장 커서 보통 끝에 붙습니다. */
            static void insertSorted( vector<uint64>& inoutListSorted, uint64 id )
            {
                const auto position = std::lower_bound( inoutListSorted.begin(), inoutListSorted.end(), id );
                if ( position == inoutListSorted.end() || *position != id )
                    inoutListSorted.insert( position, id );
            }
            /** @brief 정렬된 id 목록에서 @p id 를 뺍니다. 있었으면 true 입니다. */
            static bool eraseSorted( vector<uint64>& inoutListSorted, uint64 id )
            {
                const auto position = std::lower_bound( inoutListSorted.begin(), inoutListSorted.end(), id );
                if ( position == inoutListSorted.end() || *position != id )
                    return false;
                inoutListSorted.erase( position );
                return true;
            }

            /** @brief 그룹의 단계 첫 그룹입니다(물리 앞이면 0, 뒤면 PostPhysics). */
            static uint8 getPhaseFirstGroup( uint8 group ) { return group >= kPostPhysicsGroup ? kPostPhysicsGroup : 0; }

            /** @brief 스테이지 안의 순서를 정하는 키입니다(순서 키, 오브젝트 id). 전체 재구축과 증분 끼움이 같은 키와 같은 비교를 쓰므로 두 길의 순서가 어긋날 수 없다. */
            struct StageOrder
            {
                uint8  _orderKey{ 0 };
                uint64 _objectID{ 0 };
            };

            /** @brief 스테이지 안의 순서 규칙의 유일한 정의입니다. @p left 가 @p right 보다 엄격하게 앞이면 true 입니다(같은 키면 둘 다 false). */
            static bool isOrderedBefore( const StageOrder& left, const StageOrder& right )
            {
                if ( left._orderKey != right._orderKey )
                    return left._orderKey < right._orderKey;
                return left._objectID < right._objectID;
            }

            /** @brief `isOrderedBefore` 로, 같은 키(한 오브젝트의 항목끼리)면 등록 순서로 비교합니다. 후보 목록을 정렬할 때의 규칙입니다. */
            static bool isBefore( const StageCandidate& left, const StageCandidate& right )
            {
                const StageOrder leftOrder{ left._item._orderKey, left._objectID };
                const StageOrder rightOrder{ right._item._orderKey, right._objectID };
                if ( isOrderedBefore( leftOrder, rightOrder ) )
                    return true;
                if ( isOrderedBefore( rightOrder, leftOrder ) )
                    return false;
                return left._originalIndex < right._originalIndex;
            }

            /** @brief 항목의 서브틱 정보에서 선행 목록을 찾습니다. 주 틱이거나 없으면 nullptr. */
            static const vector<SubTickHandle>* findPrerequisiteList( const TickItem& item )
            {
                if ( item._subTickID == 0 || item._pComponent == nullptr )
                    return nullptr;
                for ( const SubTickInfo& subTick : item._pComponent->getAllSubTicks() )
                {
                    if ( subTick._subTickID == item._subTickID )
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
                GameObject* pObj            = manager.findGameObjectByID( handle._objectID );
                const bool  bInactiveObject = pObj == nullptr || pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false;
                if ( bInactiveObject )
                    return kNoExternalGroup;
                for ( const TickItem& item : pObj->getTickItems() )
                {
                    const bool bMatchingTickItem = item._pComponent != nullptr && item._pComponent->getComponentID() == handle._componentID && item._subTickID == handle._subTickID &&
                                                   item._pComponent->isPendingDestroy() == false;
                    if ( bMatchingTickItem )
                        return item._group;
                }
                return kNoExternalGroup;
            }

            /** @brief 후보를 (구성요소 id, 서브틱 id) 로 찾는 평평한 열린 주소 표입니다. 해시맵의 노드 할당 없이 한 번 짓고 그래프와 바깥 선행 조건 검사가 함께 씁니다. */
            struct CandidateIndex
            {
                static constexpr uint32 kNone = invalid_index::kUint32;

                struct Slot
                {
                    uint64 _componentID{ 0 };
                    uint32 _subTickID{ 0 };
                    uint32 _index{ kNone };
                };

                vector<Slot> _listSlot;
                uint64       _mask{ 0 };

                static uint64 hash( uint64 componentID, uint32 subTickID )
                {
                    return HashUtil::mix64( componentID ^ ( ( static_cast<uint64>( subTickID ) + 1 ) * HashUtil::kGoldenRatio64 ) );
                }

                void build( const vector<StageCandidate>& listCandidate )
                {
                    uint64 capacity = 16;
                    while ( capacity < listCandidate.size() * 2 )
                    {
                        capacity <<= 1;
                    }
                    _listSlot.assign( static_cast<size_t>( capacity ), Slot{} );
                    _mask = capacity - 1;
                    for ( uint32 index = 0; index < listCandidate.size(); ++index )
                    {
                        const uint64 componentID = listCandidate[index]._componentID;
                        const uint32 subTickID   = listCandidate[index]._item._subTickID;
                        uint64       cursor      = hash( componentID, subTickID ) & _mask;
                        while ( _listSlot[static_cast<size_t>( cursor )]._index != kNone )
                        {
                            cursor = ( cursor + 1 ) & _mask;
                        }
                        _listSlot[static_cast<size_t>( cursor )] = Slot{ componentID, subTickID, index };
                    }
                }

                /** @brief 후보 번호를 돌려줍니다. 없으면 `kNone` 입니다. */
                uint32 find( uint64 componentID, uint32 subTickID ) const
                {
                    uint64 cursor = hash( componentID, subTickID ) & _mask;
                    while ( true )
                    {
                        const Slot& slot = _listSlot[static_cast<size_t>( cursor )];
                        if ( slot._index == kNone )
                            return kNone;
                        if ( slot._componentID == componentID && slot._subTickID == subTickID )
                            return slot._index;
                        cursor = ( cursor + 1 ) & _mask;
                    }
                }
            };

            /** @brief 후보 사이의 선행 조건 그래프입니다(CSR). `_listTarget[_listOffset[i] .. _listOffset[i + 1])` 이 후보 i 를 기다리는 후보들입니다. */
            struct PrerequisiteGraph
            {
                vector<uint32> _listOffset;
                vector<uint32> _listTarget;
                vector<uint32> _listInDegree;
            };

            /** @brief 순환에 든 항목이 모이는 스테이지의 레벨입니다(어느 정상 레벨보다 뒤이고, 오류 로그와 함께 마지막에 돈다). */
            static constexpr uint32 kCycleLevel = invalid_index::kUint32;

            /**
             * @brief 한 레벨을 오브젝트별 스테이지로 가릅니다. 같은 오브젝트의 항목은 0 번부터 차례로 찹니다.
             * @details 오브젝트마다 "이미 든 스테이지 수" 하나면 됩니다. 후보가 오브젝트 순번(`_objectOrdinal`)을 가지므로 맵 없이 칸 배열 하나로 셉니다. 레벨을 마치면 쓴 칸만 0 으로 되돌립니다.
             *          @p bWaits 면 레벨의 첫 스테이지에만 "돌기 전에 적용" 을 세웁니다 — 같은 레벨의 항목은 서로 기다리지 않으므로 둘째부터는
             *          이미 적용된 값을 봅니다.
             */
            static void splitByObject( const vector<uint32>& listLevel, const vector<StageCandidate>& listCandidate, bool bWaits, uint32 level, vector<uint32>& inOutListSlot,
                                       vector<TickStage>& outListStage )
            {
                const size_t firstStage = outListStage.size();
                for ( const uint32 candidateIndex : listLevel )
                {
                    const StageCandidate& candidate = listCandidate[candidateIndex];
                    uint32&               slot      = inOutListSlot[candidate._objectOrdinal];
                    if ( firstStage + slot == outListStage.size() )
                    {
                        TickStage& stage    = outListStage.emplace_back();
                        stage._group        = candidate._item._group;
                        stage._level        = level;
                        stage._bApplyBefore = ( bWaits && slot == 0 ) ? SW_TRUE : SW_FALSE;
                    }
                    outListStage[firstStage + slot]._listItem.push_back( candidate._item );
                    outListStage[firstStage + slot]._listItemObjectID.push_back( candidate._objectID );
                    ++slot;
                }
                for ( const uint32 candidateIndex : listLevel )
                {
                    inOutListSlot[listCandidate[candidateIndex]._objectOrdinal] = 0;
                }
            }

            /**
             * @brief 후보들 사이의 선행 조건 그래프를 짓습니다. 후보에 없는 선행 조건은 여기서 보지 않습니다(보통 길 — `findExternalGroup`).
             * @details 찾을 수 없는 선행 조건과 자기 자신은 순서를 만들지 않습니다. 간선을 한 번 모아 세어 나누는 정렬로 CSR 을 만듭니다(후보마다 벡터 할당 없음).
             */
            static void buildPrerequisiteGraph( const vector<StageCandidate>& listCandidate, const CandidateIndex& index, PrerequisiteGraph& outGraph )
            {
                const size_t count = listCandidate.size();
                outGraph._listOffset.assign( count + 1, 0 );
                outGraph._listInDegree.assign( count, 0 );
                vector<uint64> listEdge; // (선행 후보 << 32) | 기다리는 후보
                for ( uint32 dependent = 0; dependent < count; ++dependent )
                {
                    for ( const SubTickHandle& prerequisite : *listCandidate[dependent]._pListPrerequisite )
                    {
                        const uint32 source = index.find( prerequisite._componentID, prerequisite._subTickID );
                        if ( source == CandidateIndex::kNone || source == dependent )
                            continue;
                        listEdge.push_back( ( static_cast<uint64>( source ) << 32 ) | dependent );
                        ++outGraph._listOffset[source + 1];
                        ++outGraph._listInDegree[dependent];
                    }
                }
                for ( size_t node = 0; node < count; ++node )
                {
                    outGraph._listOffset[node + 1] += outGraph._listOffset[node];
                }
                outGraph._listTarget.assign( listEdge.size(), 0 );
                vector<uint32> listCursor( outGraph._listOffset.begin(), outGraph._listOffset.end() - 1 );
                for ( const uint64 edge : listEdge )
                {
                    outGraph._listTarget[listCursor[static_cast<uint32>( edge >> 32 )]++] = static_cast<uint32>( edge );
                }
            }

            /**
             * @brief 선행 조건이 뒤 그룹에 있는 후보를 그 그룹으로 옮깁니다. 사슬을 따라 옮깁니다(언리얼 `QueueTickFunction` 의 `ActualStartTickGroup`).
             * @details 보통 길의 선행 조건은 그 그룹(`_externalGroup`)으로 먼저 올리고, 후보 사이의 간선은 위상 순서(Kahn)로 걸으며 "내 그룹 =
             *          max(내 그룹, 선행의 그룹)" 을 적습니다. 앞 그룹의 선행 조건은 이미 끝났으므로 아무것도 바꾸지 않습니다. 순환에 걸린 후보(와 그
             *          뒤)는 위상 순서가 없으니 제 그룹에 둡니다(순환은 그룹 안에서 순서 키로 방어합니다).
             */
            static void raiseGroupsToPrerequisites( vector<StageCandidate>& listCandidate, const PrerequisiteGraph& graph )
            {
                const size_t count = listCandidate.size();
                for ( StageCandidate& candidate : listCandidate )
                {
                    if ( candidate._externalGroup != kNoExternalGroup && candidate._item._group < candidate._externalGroup )
                        candidate._item._group = candidate._externalGroup;
                }

                vector<uint32> listInDegree = graph._listInDegree;
                vector<uint32> listReady;
                listReady.reserve( count );
                for ( uint32 index = 0; index < count; ++index )
                {
                    if ( listInDegree[index] == 0 )
                        listReady.push_back( index );
                }
                for ( size_t cursor = 0; cursor < listReady.size(); ++cursor )
                {
                    const uint32 current = listReady[cursor];
                    const uint8  group   = listCandidate[current]._item._group;
                    for ( uint32 edge = graph._listOffset[current]; edge < graph._listOffset[current + 1]; ++edge )
                    {
                        const uint32 next      = graph._listTarget[edge];
                        uint8&       nextGroup = listCandidate[next]._item._group;
                        if ( nextGroup < group )
                            nextGroup = group;
                        if ( --listInDegree[next] == 0 )
                            listReady.push_back( next );
                    }
                }
            }

            /** @brief 스테이지를 짓는 동안 쓰는 작업 칸입니다(후보 수만큼 한 번 잡고 그룹마다 되씁니다). */
            struct StageScratch
            {
                vector<uint32> _listInDegree; ///< 같은 그룹 후보 사이의 진입 차수
                vector<uint8>  _listVisited;  ///< 레벨에 든 후보
                vector<uint32> _listSlot;     ///< 오브젝트 순번마다 이미 든 스테이지 수(`splitByObject`)
            };

            /**
             * @brief 한 그룹의 후보(@p listGroupMember — 후보 번호)를 선행 조건 순서로 갈라 스테이지를 붙입니다(Kahn 레벨 + 오브젝트별 스테이지).
             * @details 레벨 0 은 후보 사이의 선행 조건이 없지만, 보통 길의 선행 조건이 같은 단계에서 돌았으면(그 쓰기가 아직 대기 중이다) 그 레벨도
             *          돌기 전에 적용합니다. 레벨 1 부터는 늘 앞 레벨을 기다립니다. 다른 그룹에 있는 후보와의 간선은 그룹 사이 장벽이 지키므로 세지 않습니다.
             */
            static void appendGroupStages( const vector<StageCandidate>& listCandidate, const vector<uint32>& listGroupMember, const PrerequisiteGraph& graph, uint8 group,
                                           StageScratch& scratch, vector<TickStage>& outListStage )
            {
                const size_t count = listGroupMember.size();
                if ( count == 0 )
                    return;

                for ( const uint32 member : listGroupMember )
                {
                    scratch._listInDegree[member] = 0;
                    scratch._listVisited[member]  = 0;
                }
                for ( const uint32 member : listGroupMember )
                {
                    for ( uint32 edge = graph._listOffset[member]; edge < graph._listOffset[member + 1]; ++edge )
                    {
                        const uint32 target = graph._listTarget[edge];
                        if ( listCandidate[target]._item._group == group )
                            ++scratch._listInDegree[target];
                    }
                }

                auto sortLevel = [&listCandidate]( vector<uint32>& listLevel )
                {
                    std::stable_sort( listLevel.begin(), listLevel.end(), [&listCandidate]( uint32 left, uint32 right )
                    { return isBefore( listCandidate[left], listCandidate[right] ); } );
                };
                // 보통 길의 선행 조건이 이 단계(같은 단계의 이 그룹 또는 앞 그룹)에서 돌았으면 그 쓰기를 먼저 적용해야 한다.
                auto waitsOnObjectPath = [&listCandidate]( const vector<uint32>& listLevel )
                {
                    for ( const uint32 index : listLevel )
                    {
                        const StageCandidate& candidate = listCandidate[index];
                        if ( candidate._externalGroup != kNoExternalGroup && candidate._externalGroup >= getPhaseFirstGroup( candidate._item._group ) )
                            return true;
                    }
                    return false;
                };

                vector<uint32> listCurrentLevel;
                for ( const uint32 member : listGroupMember )
                {
                    if ( scratch._listInDegree[member] == 0 )
                        listCurrentLevel.push_back( member );
                }

                size_t processedCount = 0;
                uint32 levelNumber    = 0;
                bool   bWaits         = false; // 레벨 1 부터는 모두 앞 레벨의 선행 조건을 기다린다
                while ( listCurrentLevel.empty() == false )
                {
                    sortLevel( listCurrentLevel );
                    vector<uint32> listNextLevel;
                    for ( const uint32 current : listCurrentLevel )
                    {
                        scratch._listVisited[current] = 1;
                        ++processedCount;
                        for ( uint32 edge = graph._listOffset[current]; edge < graph._listOffset[current + 1]; ++edge )
                        {
                            const uint32 next = graph._listTarget[edge];
                            if ( listCandidate[next]._item._group == group && --scratch._listInDegree[next] == 0 )
                                listNextLevel.push_back( next );
                        }
                    }
                    splitByObject( listCurrentLevel, listCandidate, bWaits || waitsOnObjectPath( listCurrentLevel ), levelNumber, scratch._listSlot, outListStage );
                    ++levelNumber;
                    bWaits           = true;
                    listCurrentLevel = std::move( listNextLevel );
                }

                // 순환 방어: 닿지 못한 것은 순서 키 순으로 마지막에 붙인다. 틱이 조용히 빠지는 것보다 낫다. 순환은 선행 조건을 잘못 건 데이터 결함이므로
                // 오류로 알린다(스테이지는 선행 조건이 바뀔 때만 다시 만들어서 프레임마다 쏟아지지 않는다).
                if ( processedCount < count )
                {
                    SW_LOG_ERROR( "Sub-tick prerequisites form a cycle: %# sub-ticks cannot be ordered and run last in their group by order key - break the cycle",
                                  static_cast<uint32>( count - processedCount ) );
                    vector<uint32> listRemaining;
                    for ( const uint32 member : listGroupMember )
                    {
                        if ( scratch._listVisited[member] == 0 )
                            listRemaining.push_back( member );
                    }
                    sortLevel( listRemaining );
                    splitByObject( listRemaining, listCandidate, true, kCycleLevel, scratch._listSlot, outListStage );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @brief `-gv_tickBatch=0`: 타입별 일괄 틱(`TickBatchRegistry`)을 끄고 모든 오브젝트를 칸 목록으로 돌립니다 — 같은 바이너리로 전/후를 견줍니다.
     * @details 기동 때 정합니다. 실행 중에 바꾸면 다음 전체 훑기(모듈 적재 · 씬 초기화)부터 따릅니다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tickBatch, true, "타입별 일괄 틱 경로를 쓴다(0 이면 모든 오브젝트를 칸 목록으로 — 전/후 비교용)" );

    TickRegistry::TickRegistry()
        : _arrListEntry{}
        , _arrListBatch{}
        , _batchGeneration{ TickBatchRegistry::getGeneration() }
        , _listDirtyObjectID{}
        , _listProcessingObjectID{}
        , _dirtyMutex{}
        , _bAllDirty{ SW_TRUE }
        , _prerequisiteCount{ 0 }
        , _stageItemCount{ 0 }
        , _stageBuildCount{ 0 }
        , _stageDeltaCount{ 0 }
        , _stageGeneration{ 1 }
        , _builtStageGeneration{ 0 }
        , _listDependentObjectID{}
        , _listReferencedObjectID{}
        , _listStage{}
    {
    }

    void TickRegistry::markObjectDirty( GameObject* pObj )
    {
        if ( pObj == nullptr || pObj->_bTickDirty.exchange( SW_TRUE, std::memory_order_acq_rel ) != SW_FALSE )
            return;
        std::scoped_lock<mutex> lock{ _dirtyMutex };
        _listDirtyObjectID.push_back( pObj->getObjectID() );
    }

    void TickRegistry::markAllDirty()
    {
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    bool TickRegistry::refresh( GameObjectManager& manager )
    {
        // 일괄 틱 타입이 등록 · 해제됐으면(모듈 적재 · 해제) 모두 다시 훑는다 — 오브젝트가 어느 목록에 갈지가 바뀌었고, 내려간 함수를 들고 있으면 안 된다.
        const uint32 batchGeneration = TickBatchRegistry::getGeneration();
        if ( batchGeneration != _batchGeneration )
        {
            _batchGeneration = batchGeneration;
            _bAllDirty.store( SW_TRUE, std::memory_order_release );
        }
        const bool bAll = _bAllDirty.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE;
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listProcessingObjectID.swap( _listDirtyObjectID );
        }
        bool bRefreshedAny = false;
        if ( bAll )
        {
            manager.forEachGameObject( [this, &manager]( GameObject* pObj )
            {
                pObj->_bTickDirty.store( SW_FALSE, std::memory_order_relaxed );
                refreshObject( manager, pObj );
            } );
            // 모두 훑었으니 개별 표시는 지운다. 죽어 가는 오브젝트의 id 는 어차피 해석이 비었을 것이다.
            _listProcessingObjectID.clear();
            bRefreshedAny = true;
        }
        else
        {
            for ( const uint64 objectID : _listProcessingObjectID )
            {
                // 삭제 대기면 비어 있다. 그 오브젝트는 파괴 때 `unregisterObject` 로 빠진다.
                GameObject* pObj = manager.findGameObjectByID( objectID );
                if ( pObj == nullptr )
                    continue;
                pObj->_bTickDirty.store( SW_FALSE, std::memory_order_relaxed );
                refreshObject( manager, pObj );
                bRefreshedAny = true;
            }
            _listProcessingObjectID.clear();
        }
        if ( bRefreshedAny )
            removeEmptyBatchLists();

        // 선행 조건에 걸린 오브젝트(가진 쪽 · 가리켜진 쪽)가 바뀌었을 때만 스테이지를 다시 짓는다 — 스폰이 잦아도 사슬 밖이면 오르지 않는다.
        if ( _builtStageGeneration != _stageGeneration )
            rebuildStages( manager );
        return bRefreshedAny;
    }

    void TickRegistry::refreshObject( GameObjectManager& manager, GameObject* pObj )
    {
        const uint64  objectID      = pObj->getObjectID();
        const bool    bWasDependent = std::binary_search( _listDependentObjectID.begin(), _listDependentObjectID.end(), objectID );
        const bool    bReferenced   = std::binary_search( _listReferencedObjectID.begin(), _listReferencedObjectID.end(), objectID );
        TickItemList& listItem      = pObj->_listTickItem;
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
                listItem.push_back( TickItem{ pComp, subTick._subTickID, group, static_cast<uint8>( static_cast<uint8>( subTick._phase ) + subTick._priority ),
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
            while ( true )
            {
                const bool bSameGroupWithoutPrerequisite = cursor < listItem.size() && listItem[cursor]._group == group && listItem[cursor]._bHasPrerequisite == SW_FALSE;
                if ( bSameGroupWithoutPrerequisite == false )
                    break;
                ++cursor;
            }
            const uint32 end = cursor;
            while ( cursor < listItem.size() && listItem[cursor]._group == group )
            {
                ++cursor;
            }
            TickObjectEntry   entry{};
            TickBatchFunction pBatchFunction = nullptr;
            if ( begin < end )
            {
                entry._pObject   = pObj;
                entry._firstItem = listItem[begin];
                entry._pItem     = listItem.data() + begin;
                entry._itemCount = end - begin;
                pBatchFunction   = bActive ? findBatchFunction( entry._pItem, entry._itemCount ) : nullptr;
            }
            setMembership( pObj, group, bActive && begin < end, entry, pBatchFunction );
        }

        _prerequisiteCount           = _prerequisiteCount - pObj->_tickPrerequisiteCount + prerequisiteCount;
        pObj->_tickPrerequisiteCount = prerequisiteCount;
        if ( dependentCount > 0 )
            TickRegistryInternal::insertSorted( _listDependentObjectID, objectID );
        else
            (void)TickRegistryInternal::eraseSorted( _listDependentObjectID, objectID );
        if ( bReferenced )
        {
            ++_stageGeneration; // 다른 항목이 이 오브젝트의 항목을 기다린다 — 순서가 바뀔 수 있으니 전부 다시 짓는다
        }
        else if ( bWasDependent || dependentCount > 0 )
        {
            // 아무도 기다리지 않는 오브젝트는 자기 항목만 끼우고 뺀다. 전체 재구축이 이미 서 있으면 그때 함께 지어진다.
            const bool bRebuildPending = _builtStageGeneration != _stageGeneration;
            if ( bRebuildPending || applyStageDelta( manager, pObj ) == false )
                ++_stageGeneration;
            else
                ++_stageDeltaCount;
        }
    }

    void TickRegistry::removeStageItems( uint64 objectID )
    {
        for ( TickStage& stage : _listStage )
        {
            for ( size_t index = 0; index < stage._listItemObjectID.size(); )
            {
                if ( stage._listItemObjectID[index] != objectID )
                {
                    ++index;
                    continue;
                }
                stage._listItem.erase( stage._listItem.begin() + static_cast<ptrdiff_t>( index ) );
                stage._listItemObjectID.erase( stage._listItemObjectID.begin() + static_cast<ptrdiff_t>( index ) );
                --_stageItemCount;
            }
        }
    }

    bool TickRegistry::applyStageDelta( GameObjectManager& manager, GameObject* pObj )
    {
        const uint64 objectID = pObj->getObjectID();
        removeStageItems( objectID );
        if ( pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false )
            return true;

        struct NewItem
        {
            TickItem _item;
            uint8    _externalGroup{ TickRegistryInternal::kNoExternalGroup };
        };
        vector<NewItem> listNew;
        for ( const TickItem& item : pObj->_listTickItem )
        {
            const bool bSkipItem = item._bHasPrerequisite == SW_FALSE || item._pComponent == nullptr || item._pComponent->isPendingDestroy();
            if ( bSkipItem )
                continue;
            const vector<SubTickHandle>* pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
            if ( pListPrerequisite == nullptr )
                continue;
            NewItem entry{};
            entry._item = item;
            for ( const SubTickHandle& prerequisite : *pListPrerequisite )
            {
                // 선행 조건이 의존 오브젝트(자기 자신 포함)를 가리키면 후보 사이의 간선이 생길 수 있다 — 말단이 아니다.
                if ( std::binary_search( _listDependentObjectID.begin(), _listDependentObjectID.end(), prerequisite._objectID ) )
                    return false;
                TickRegistryInternal::insertSorted( _listReferencedObjectID, prerequisite._objectID );
                const uint8 externalGroup = TickRegistryInternal::findExternalGroup( manager, prerequisite );
                const bool  bLaterGroup   = entry._externalGroup == TickRegistryInternal::kNoExternalGroup || entry._externalGroup < externalGroup;
                if ( externalGroup != TickRegistryInternal::kNoExternalGroup && bLaterGroup )
                    entry._externalGroup = externalGroup;
            }
            if ( entry._externalGroup != TickRegistryInternal::kNoExternalGroup && entry._item._group < entry._externalGroup )
                entry._item._group = entry._externalGroup;
            listNew.push_back( entry );
        }
        std::stable_sort( listNew.begin(), listNew.end(), []( const NewItem& left, const NewItem& right )
        {
            if ( left._item._group != right._item._group )
                return left._item._group < right._item._group;
            return left._item._orderKey < right._item._orderKey;
        } );

        // 그룹마다 같은 오브젝트의 항목을 레벨 0 의 0 번 스테이지부터 차례로 넣는다(전체 재구축의 `splitByObject` 와 같은 규칙).
        size_t cursor = 0;
        while ( cursor < listNew.size() )
        {
            const uint8 group  = listNew[cursor]._item._group;
            bool        bWaits = false;
            for ( uint32 slot = 0; cursor < listNew.size() && listNew[cursor]._item._group == group; ++cursor, ++slot )
            {
                const NewItem& entry = listNew[cursor];
                bWaits               = bWaits || ( entry._externalGroup != TickRegistryInternal::kNoExternalGroup &&
                                     entry._externalGroup >= TickRegistryInternal::getPhaseFirstGroup( group ) );

                // 이 그룹 레벨 0 의 slot 번째 스테이지를 찾고, 없으면 레벨 0 의 끝(같은 그룹의 더 높은 레벨이나 다음 그룹이 시작하는 자리)에 만든다.
                size_t stageIndex = _listStage.size();
                size_t insertAt   = _listStage.size();
                uint32 seen       = 0;
                for ( size_t index = 0; index < _listStage.size(); ++index )
                {
                    const TickStage& stage       = _listStage[index];
                    const bool       bLevelZero  = stage._group == group && stage._level == 0;
                    const bool       bPastLevel0 = stage._group > group || ( stage._group == group && stage._level != 0 );
                    if ( bLevelZero )
                    {
                        if ( seen++ == slot )
                        {
                            stageIndex = index;
                            break;
                        }
                    }
                    else if ( bPastLevel0 )
                    {
                        insertAt = index;
                        break;
                    }
                }
                if ( stageIndex == _listStage.size() )
                {
                    TickStage created{};
                    created._group = group;
                    created._level = 0;
                    stageIndex     = insertAt;
                    _listStage.insert( _listStage.begin() + static_cast<ptrdiff_t>( insertAt ), std::move( created ) );
                }
                TickStage& stage = _listStage[stageIndex];
                // 전체 재구축과 같은 순서 규칙(`isOrderedBefore`)으로 끼운다. 같은 키면 뒤에 붙는다.
                const TickRegistryInternal::StageOrder entryOrder{ entry._item._orderKey, objectID };
                size_t                                 low  = 0;
                size_t                                 high = stage._listItem.size();
                while ( low < high )
                {
                    const size_t                           middle = low + ( high - low ) / 2;
                    const TickRegistryInternal::StageOrder otherOrder{ stage._listItem[middle]._orderKey, stage._listItemObjectID[middle] };
                    if ( TickRegistryInternal::isOrderedBefore( entryOrder, otherOrder ) )
                        high = middle;
                    else
                        low = middle + 1;
                }
                const size_t position = low;
                stage._listItem.insert( stage._listItem.begin() + static_cast<ptrdiff_t>( position ), entry._item );
                stage._listItemObjectID.insert( stage._listItemObjectID.begin() + static_cast<ptrdiff_t>( position ), objectID );
                ++_stageItemCount;
            }
            if ( bWaits )
            {
                for ( TickStage& stage : _listStage )
                {
                    if ( stage._group == group && stage._level == 0 )
                    {
                        stage._bApplyBefore = SW_TRUE; // 이 그룹 레벨 0 의 0 번 스테이지다
                        break;
                    }
                }
            }
        }
        return true;
    }

    TickBatchFunction TickRegistry::findBatchFunction( const TickItem* pItem, uint32 itemCount )
    {
        // 그 그룹의 항목이 주 틱 하나뿐일 때만 — 둘 이상이면 오브젝트 안의 순서를 지키려고 오브젝트 칸으로 간다.
        const bool bNotBatchable = gv_tickBatch == false || itemCount != 1 || pItem->_subTickID != 0 || pItem->_pComponent == nullptr;
        if ( bNotBatchable )
            return nullptr;
        return TickBatchRegistry::findFunction( pItem->_pComponent->findCachedTypeInfo() );
    }

    TickBatchList* TickRegistry::findBatchMembership( const GameObject* pObj, uint32 group )
    {
        const uint32 index = pObj->_arrTickIndex[group];
        if ( index == kNotInList )
            return nullptr;
        for ( TickBatchList& batchList : _arrListBatch[group] )
        {
            if ( index < batchList._listElement.size() && batchList._listElement[index]._pObject == pObj )
                return &batchList;
        }
        return nullptr;
    }

    TickBatchList& TickRegistry::getOrCreateBatchList( uint32 group, TickBatchFunction pFunction )
    {
        for ( TickBatchList& batchList : _arrListBatch[group] )
        {
            if ( batchList._pFunction == pFunction )
                return batchList;
        }
        TickBatchList& created = _arrListBatch[group].emplace_back();
        created._pFunction     = pFunction;
        return created;
    }

    void TickRegistry::removeEmptyBatchLists()
    {
        for ( vector<TickBatchList>& listBatch : _arrListBatch )
        {
            // 목록의 자리는 아무도 들지 않는다(오브젝트는 목록 안의 위치만 든다) — 순서를 지켜 걷기만 한다.
            size_t keptCount = 0;
            for ( size_t listIndex = 0; listIndex < listBatch.size(); ++listIndex )
            {
                if ( listBatch[listIndex]._listElement.empty() )
                    continue;
                if ( keptCount != listIndex )
                    listBatch[keptCount] = std::move( listBatch[listIndex] );
                ++keptCount;
            }
            listBatch.resize( keptCount );
        }
    }

    void TickRegistry::setMembership( GameObject* pObj, uint32 group, bool bMember, const TickObjectEntry& entry, TickBatchFunction pBatchFunction )
    {
        vector<TickObjectEntry>& listEntry      = _arrListEntry[group];
        uint32&                  index          = pObj->_arrTickIndex[group];
        const bool               bInEntryList   = index != kNotInList && index < listEntry.size() && listEntry[index]._pObject == pObj;
        TickBatchList*           pCurrentBatch  = bInEntryList ? nullptr : findBatchMembership( pObj, group );
        const bool               bWantsBatch    = bMember && pBatchFunction != nullptr;
        const bool               bWantsEntry    = bMember && pBatchFunction == nullptr;
        const bool               bSameBatchList = bWantsBatch && pCurrentBatch != nullptr && pCurrentBatch->_pFunction == pBatchFunction;

        // 이미 같은 목록에 있으면 그 자리만 새로 쓴다(항목 버퍼가 다시 지어졌다).
        if ( bWantsEntry && bInEntryList )
        {
            listEntry[index] = entry;
            return;
        }
        if ( bSameBatchList )
        {
            pCurrentBatch->_listElement[index] = TickBatchElement{ entry._firstItem._pComponent, pObj };
            return;
        }

        // 지금 목록에서 뺀다(끝 원소를 그 자리로 옮기고 옮긴 오브젝트의 위치를 고친다).
        if ( bInEntryList )
        {
            const TickObjectEntry moved          = listEntry.back();
            listEntry[index]                     = moved;
            moved._pObject->_arrTickIndex[group] = index;
            listEntry.pop_back();
        }
        else if ( pCurrentBatch != nullptr )
        {
            vector<TickBatchElement>& listElement = pCurrentBatch->_listElement;
            const TickBatchElement    moved       = listElement.back();
            listElement[index]                    = moved;
            moved._pObject->_arrTickIndex[group]  = index;
            listElement.pop_back();
        }
        index = kNotInList;

        if ( bWantsEntry )
        {
            index = static_cast<uint32>( listEntry.size() );
            listEntry.push_back( entry );
        }
        else if ( bWantsBatch )
        {
            vector<TickBatchElement>& listElement = getOrCreateBatchList( group, pBatchFunction )._listElement;
            index                                 = static_cast<uint32>( listElement.size() );
            listElement.push_back( TickBatchElement{ entry._firstItem._pComponent, pObj } );
        }
    }

    void TickRegistry::unregisterObject( GameObject* pObj )
    {
        if ( pObj == nullptr )
            return;
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            setMembership( pObj, group, false, TickObjectEntry{}, nullptr );
        }
        const uint64 objectID = pObj->getObjectID();
        // 스테이지가 이 오브젝트의 항목을 들고 있거나(선행 조건을 가진 쪽) 이 오브젝트를 기다린다(가리켜진 쪽) — 다음 틱 전에 다시 짓는다.
        const bool bWasDependent = TickRegistryInternal::eraseSorted( _listDependentObjectID, objectID );
        if ( std::binary_search( _listReferencedObjectID.begin(), _listReferencedObjectID.end(), objectID ) )
            ++_stageGeneration;
        else if ( bWasDependent && _builtStageGeneration == _stageGeneration )
            removeStageItems( objectID ); // 아무도 기다리지 않으니 자기 항목만 뺀다(스폰이 잦은 팔로워가 전체 재구축을 부르지 않게)
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
        for ( vector<TickBatchList>& listBatch : _arrListBatch )
        {
            listBatch.clear();
        }
        {
            std::scoped_lock<mutex> lock{ _dirtyMutex };
            _listDirtyObjectID.clear();
        }
        _listProcessingObjectID.clear();
        _prerequisiteCount = 0;
        _listDependentObjectID.clear();
        _listReferencedObjectID.clear();
        _listStage.clear();
        _stageItemCount = 0;
        ++_stageGeneration;
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    void TickRegistry::rebuildStages( GameObjectManager& manager )
    {
        _builtStageGeneration = _stageGeneration;
        ++_stageBuildCount;
        _listStage.clear();
        _listReferencedObjectID.clear();
        _stageItemCount = 0;
        if ( _listDependentObjectID.empty() )
            return;

        // 의존 오브젝트 목록은 오브젝트 id(만든 순서) 순으로 정렬되어 있다 — 같은 씬이 늘 같은 스테이지를 짓는다.
        vector<TickRegistryInternal::StageCandidate> listCandidate;
        uint32                                       originalIndex = 0;
        uint32                                       objectOrdinal = 0;
        for ( const uint64 objectID : _listDependentObjectID )
        {
            GameObject* pObj               = manager.findGameObjectByID( objectID );
            const bool  bObjectNotTickable = pObj == nullptr || pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false;
            if ( bObjectNotTickable )
                continue;
            bool bAnyCandidate = false;
            for ( const TickItem& item : pObj->_listTickItem )
            {
                const bool bSkipItem = item._bHasPrerequisite == SW_FALSE || item._pComponent == nullptr || item._pComponent->isPendingDestroy();
                if ( bSkipItem )
                    continue;
                const vector<SubTickHandle>* pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
                if ( pListPrerequisite == nullptr )
                    continue;
                TickRegistryInternal::StageCandidate candidate{};
                candidate._item              = item;
                candidate._objectID          = objectID;
                candidate._objectOrdinal     = objectOrdinal;
                candidate._componentID       = item._pComponent->getComponentID();
                candidate._originalIndex     = originalIndex++;
                candidate._pListPrerequisite = pListPrerequisite;
                listCandidate.push_back( candidate );
                bAnyCandidate = true;
            }
            if ( bAnyCandidate )
                ++objectOrdinal;
        }

        // 후보 사이가 아닌 선행 조건은 보통 길에서 돈다 — 그 그룹을 찾아 둔다. 가리켜진 오브젝트는 모두 적어, 그것이 바뀌면 다시 짓는다.
        // 같은 대상을 거듭 가리키는 일(한 리더를 따르는 뷰 여럿)이 흔해서 바로 앞에 본 대상의 결과는 다시 찾지 않는다.
        TickRegistryInternal::CandidateIndex candidateIndex;
        candidateIndex.build( listCandidate );
        SubTickHandle lastHandle{};
        uint8         lastExternalGroup = TickRegistryInternal::kNoExternalGroup;
        for ( TickRegistryInternal::StageCandidate& candidate : listCandidate )
        {
            for ( const SubTickHandle& prerequisite : *candidate._pListPrerequisite )
            {
                if ( _listReferencedObjectID.empty() || _listReferencedObjectID.back() != prerequisite._objectID )
                    _listReferencedObjectID.push_back( prerequisite._objectID );
                if ( candidateIndex.find( prerequisite._componentID, prerequisite._subTickID ) != TickRegistryInternal::CandidateIndex::kNone )
                    continue;
                if ( prerequisite != lastHandle || lastHandle.isValid() == false )
                {
                    lastHandle        = prerequisite;
                    lastExternalGroup = TickRegistryInternal::findExternalGroup( manager, prerequisite );
                }
                const uint8 externalGroup = lastExternalGroup;
                if ( externalGroup != TickRegistryInternal::kNoExternalGroup &&
                     ( candidate._externalGroup == TickRegistryInternal::kNoExternalGroup || candidate._externalGroup < externalGroup ) )
                    candidate._externalGroup = externalGroup;
            }
        }
        std::sort( _listReferencedObjectID.begin(), _listReferencedObjectID.end() );
        _listReferencedObjectID.erase( std::unique( _listReferencedObjectID.begin(), _listReferencedObjectID.end() ), _listReferencedObjectID.end() );

        TickRegistryInternal::PrerequisiteGraph graph;
        TickRegistryInternal::buildPrerequisiteGraph( listCandidate, candidateIndex, graph );
        TickRegistryInternal::raiseGroupsToPrerequisites( listCandidate, graph );

        TickRegistryInternal::StageScratch scratch;
        scratch._listInDegree.assign( listCandidate.size(), 0 );
        scratch._listVisited.assign( listCandidate.size(), 0 );
        scratch._listSlot.assign( objectOrdinal, 0 );
        vector<uint32> listGroupMember;
        listGroupMember.reserve( listCandidate.size() );
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            listGroupMember.clear();
            for ( uint32 index = 0; index < listCandidate.size(); ++index )
            {
                if ( listCandidate[index]._item._group == group )
                    listGroupMember.push_back( index );
            }
            TickRegistryInternal::appendGroupStages( listCandidate, listGroupMember, graph, static_cast<uint8>( group ), scratch, _listStage );
        }
        _stageItemCount = static_cast<uint32>( listCandidate.size() );
    }
} // namespace sw
