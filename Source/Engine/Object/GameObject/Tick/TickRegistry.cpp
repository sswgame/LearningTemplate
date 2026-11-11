/**
 * @file TickRegistry.cpp
 * @brief 틱 레지스트리 구현입니다(오브젝트 항목 재구축 · 그룹 멤버십 · 더티 표시 · 선행 조건 스테이지).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/TickRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/TypeInfo.h"

#include <algorithm>
#include <functional>

namespace sw
{
    static_assert( TickRegistry::kGroupCount == static_cast<uint32>( TickGroup::PostUpdate ) + 1, "TickRegistry::kGroupCount must match TickGroup" );

    namespace
    {
        struct TickRegistryInternal
        {
            /** @brief 물리 뒤 단계의 첫 그룹입니다. 같은 단계의 그룹끼리는 그 사이에 틱 결과 적용이 없습니다. */
            static constexpr uint8 kPostPhysicsGroup = static_cast<uint8>( TickGroup::PostPhysics );
            /** @brief 파도 키의 오브젝트 안 순번(`TickItem::_waveOrdinal`, uint8)의 상한입니다. 넘는 오브젝트는 파도 대신 칸(Task 레인)으로 간다. */
            static constexpr uint8 kMaxWaveOrdinal = std::numeric_limits<uint8>::max();

            /** @brief 선행 조건 스테이지를 지을 때의 후보 하나입니다(선행 조건을 가진 항목 + 그 선행 목록). */
            struct StageCandidate
            {
                TickItem                           _item;
                TickStageOrder                     _stageOrder;         ///< 스테이지 안 자리 키(오브젝트 id 를 든다)
                uint32                             _objectOrdinal{ 0 }; ///< 후보를 낸 오브젝트의 순번(0 부터, 후보가 있는 오브젝트만)
                uint64                             _componentID{ 0 };
                uint32                             _originalIndex{ 0 };
                const vector<SubTickPrerequisite>* _pListPrerequisite{ nullptr }; ///< 서브틱의 선행 목록
                optional<uint8>                    _externalGroup;                ///< 보통 길에서 도는 선행 조건 중 가장 늦은 그룹. 없으면 비어 있다.
                optional<uint8>                    _externalWriteGroup;           ///< 그중 쓰기를 보는(`ReadsWrites`) 선행 조건의 가장 늦은 그룹 — 적용을 먼저 할지 가른다
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

            /**
             * @brief 스테이지 안의 순서 규칙의 유일한 정의입니다(`TickStageOrder` — 순서 키, 타입 키, 서브틱 id, 오브젝트 id). @p left 가 @p right 보다 엄격하게 앞이면 true 입니다.
             * @details 전체 재구축의 정렬과 증분 끼움(`insertIntoStage`)이 이 함수만 쓰므로 두 길의 순서가 어긋날 수 없다.
             */
            static bool isOrderedBefore( const TickStageOrder& left, const TickStageOrder& right )
            {
                if ( left._orderKey != right._orderKey )
                    return left._orderKey < right._orderKey;
                if ( left._typeKey != right._typeKey )
                    return left._typeKey < right._typeKey;
                if ( left._subTickID != right._subTickID )
                    return left._subTickID < right._subTickID;
                return left._objectID < right._objectID;
            }

            /** @brief `isOrderedBefore` 로, 같은 키(한 오브젝트의 같은 타입 항목끼리)면 등록 순서로 비교합니다. 후보 목록을 정렬할 때의 규칙입니다. */
            static bool isBefore( const StageCandidate& left, const StageCandidate& right )
            {
                if ( isOrderedBefore( left._stageOrder, right._stageOrder ) )
                    return true;
                if ( isOrderedBefore( right._stageOrder, left._stageOrder ) )
                    return false;
                return left._originalIndex < right._originalIndex;
            }

            /** @brief 항목의 서브틱 정보에서 선행 목록을 찾습니다. 주 틱이거나 없으면 nullptr. */
            static const vector<SubTickPrerequisite>* findPrerequisiteList( const TickItem& item )
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
             * @brief 보통 길에서 도는 선행 조건 항목의 그룹을 찾습니다. 오브젝트가 없거나 꺼졌거나 그 항목이 없으면 비어 있습니다.
             * @details 핸들이 든 오브젝트 id 로 찾고 그 오브젝트의 항목만 훑습니다(씬 전체를 보지 않는다).
             */
            static optional<uint8> findExternalGroup( GameObjectManager& manager, const SubTickHandle& handle )
            {
                GameObject* pObj            = manager.findGameObjectByID( handle._objectID );
                const bool  bInactiveObject = pObj == nullptr || pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false;
                if ( bInactiveObject )
                    return nullopt;
                for ( const TickItem& item : pObj->getTickItems() )
                {
                    const bool bMatchingTickItem = item._pComponent != nullptr && item._pComponent->getComponentID() == handle._componentID && item._subTickID == handle._subTickID &&
                                                   item._pComponent->isPendingDestroy() == false;
                    if ( bMatchingTickItem )
                        return item._group;
                }
                return nullopt;
            }

            /** @brief 후보를 (구성요소 id, 서브틱 id) 로 찾는 평평한 열린 주소 표입니다. 해시맵의 노드 할당 없이 한 번 짓고 그래프와 바깥 선행 조건 검사가 함께 씁니다. */
            struct CandidateIndex
            {
                /** @brief 빈 칸 표시입니다(열린 주소 표의 칸이라 optional 로 키우지 않는다 — 밖으로는 find 가 optional 로 낸다). */
                static constexpr uint32 kEmptySlot = constant::invalid_index::kUint32;

                struct Slot
                {
                    uint64 _componentID{ 0 };
                    uint32 _subTickID{ 0 };
                    uint32 _index{ kEmptySlot };
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
                        while ( _listSlot[static_cast<size_t>( cursor )]._index != kEmptySlot )
                        {
                            cursor = ( cursor + 1 ) & _mask;
                        }
                        _listSlot[static_cast<size_t>( cursor )] = Slot{ componentID, subTickID, index };
                    }
                }

                /** @brief 후보 번호를 돌려줍니다. 없으면 비어 있습니다. */
                optional<uint32> find( uint64 componentID, uint32 subTickID ) const
                {
                    uint64 cursor = hash( componentID, subTickID ) & _mask;
                    while ( true )
                    {
                        const Slot& slot = _listSlot[static_cast<size_t>( cursor )];
                        if ( slot._index == kEmptySlot )
                            return nullopt;
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
                vector<uint8>  _listReadsWrites; ///< 간선마다(`_listTarget` 과 같은 자리) 쓰기를 보는 간선(`TickPrerequisiteKind::ReadsWrites`)이면 1 — 레벨을 하나 올린다
                vector<uint32> _listInDegree;
            };

            /** @brief 선행 목록 원소 하나를 보통 길 그룹 칸들에 접습니다(가장 늦은 그룹, 쓰기를 보는 간선이면 쓰기 그룹도). */
            static void foldExternalGroup( optional<uint8>& inoutGroup, optional<uint8>& inoutWriteGroup, const optional<uint8>& externalGroup, TickPrerequisiteKind kind )
            {
                if ( externalGroup.has_value() == false )
                    return;
                if ( inoutGroup.has_value() == false || *inoutGroup < *externalGroup )
                    inoutGroup = externalGroup;
                const bool bLaterWrite = kind == TickPrerequisiteKind::ReadsWrites && ( inoutWriteGroup.has_value() == false || *inoutWriteGroup < *externalGroup );
                if ( bLaterWrite )
                    inoutWriteGroup = externalGroup;
            }

            /** @brief 순환에 든 항목이 모이는 스테이지의 레벨입니다(어느 정상 레벨보다 뒤이고, 오류 로그와 함께 마지막에 돈다). */
            static constexpr uint32 kCycleLevel = constant::invalid_index::kUint32;

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
                    outListStage[firstStage + slot]._listItemOrder.push_back( candidate._stageOrder );
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
                vector<uint64> listEdge;       // (선행 후보 << 32) | 기다리는 후보
                vector<uint8>  listReadsWrite; // listEdge 와 같은 자리의 간선 종류
                for ( uint32 dependent = 0; dependent < count; ++dependent )
                {
                    for ( const SubTickPrerequisite& prerequisite : *listCandidate[dependent]._pListPrerequisite )
                    {
                        const optional<uint32> foundSource = index.find( prerequisite._handle._componentID, prerequisite._handle._subTickID );
                        if ( foundSource.has_value() == false || *foundSource == dependent )
                            continue;
                        const uint32 source = *foundSource;
                        listEdge.push_back( ( static_cast<uint64>( source ) << 32 ) | dependent );
                        listReadsWrite.push_back( prerequisite._kind == TickPrerequisiteKind::ReadsWrites ? SW_TRUE : SW_FALSE );
                        ++outGraph._listOffset[source + 1];
                        ++outGraph._listInDegree[dependent];
                    }
                }
                for ( size_t node = 0; node < count; ++node )
                {
                    outGraph._listOffset[node + 1] += outGraph._listOffset[node];
                }
                outGraph._listTarget.assign( listEdge.size(), 0 );
                outGraph._listReadsWrites.assign( listEdge.size(), SW_FALSE );
                vector<uint32> listCursor( outGraph._listOffset.begin(), outGraph._listOffset.end() - 1 );
                for ( size_t edgeIndex = 0; edgeIndex < listEdge.size(); ++edgeIndex )
                {
                    const uint32 slot               = listCursor[static_cast<uint32>( listEdge[edgeIndex] >> 32 )]++;
                    outGraph._listTarget[slot]      = static_cast<uint32>( listEdge[edgeIndex] );
                    outGraph._listReadsWrites[slot] = listReadsWrite[edgeIndex];
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
                    if ( candidate._externalGroup.has_value() && candidate._item._group < *candidate._externalGroup )
                        candidate._item._group = *candidate._externalGroup;
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

            /** @brief 한 레벨의 후보를 순서 키 순으로 정렬합니다. */
            static void sortLevel( const vector<StageCandidate>& listCandidate, vector<uint32>& listLevel )
            {
                VectorUtil::stableSort( listLevel, [&listCandidate]( uint32 left, uint32 right )
                { return isBefore( listCandidate[left], listCandidate[right] ); } );
            }

            /** @brief 보통 길의 쓰기를 보는 선행 조건이 이 단계(같은 단계의 이 그룹 또는 앞 그룹)에서 돌았으면 그 쓰기를 먼저 적용해야 한다. 순서만 간선은 적용을 기다리지 않는다. */
            static bool isWaitingOnObjectPath( const vector<StageCandidate>& listCandidate, const vector<uint32>& listLevel )
            {
                for ( const uint32 index : listLevel )
                {
                    const StageCandidate& candidate = listCandidate[index];
                    if ( candidate._externalWriteGroup.has_value() && *candidate._externalWriteGroup >= getPhaseFirstGroup( candidate._item._group ) )
                        return true;
                }
                return false;
            }

            /**
             * @brief 선행 조건 스테이지를 짓는 일과 그 작업 칸입니다(후보 수만큼 한 번 잡고 그룹 · 레벨마다 되씁니다). 칸은 이 타입의 짓기 함수만 만진다.
             */
            class StageBuilder
            {
            public:
                /** @brief 후보 @p candidateCount 개 · 후보를 낸 오브젝트 @p objectCount 개만큼 작업 칸을 잡습니다. */
                StageBuilder( size_t candidateCount, uint32 objectCount )
                    : _listInDegree( candidateCount, 0 )
                    , _listVisited( candidateCount, 0 )
                    , _listLevel( candidateCount, 0 )
                    , _listRank( candidateCount, 0 )
                    , _listMinSlot( candidateCount, 0 )
                    , _listAssignedSlot( candidateCount, 0 )
                    , _listPosition( candidateCount, 0 )
                    , _listSlot( objectCount, 0 )
                {
                }

                /**
                 * @brief 한 그룹의 후보(@p listGroupMember — 후보 번호)를 선행 조건 순서로 갈라 스테이지를 붙입니다(쓰기 간선 레벨 + 레벨마다 칸 스테이지 · 순서만 태스크 간선).
                 * @details 위상 순서(Kahn)로 걸으며 레벨 = max(선행 레벨 + 쓰기를 보는 간선이면 1) 을 매긴다. 레벨 0 은 쓰기를 기다리는 후보 사이 간선이 없지만, 보통 길의
                 *          쓰기를 보는 선행 조건이 같은 단계에서 돌았으면(그 쓰기가 아직 대기 중이다) 그 레벨도 돌기 전에 적용한다. 레벨 1 부터는 늘 앞 레벨의 쓰기를
                 *          기다린다. 다른 그룹에 있는 후보와의 간선은 그룹 사이 장벽이 지키므로 세지 않는다.
                 */
                void appendGroupStages( const vector<StageCandidate>& listCandidate, const vector<uint32>& listGroupMember, const PrerequisiteGraph& graph, uint8 group,
                                        vector<TickStage>& outListStage )
                {
                    const size_t count = listGroupMember.size();
                    if ( count == 0 )
                        return;

                    for ( const uint32 member : listGroupMember )
                    {
                        _listInDegree[member] = 0;
                        _listVisited[member]  = 0;
                        _listLevel[member]    = 0;
                    }
                    for ( const uint32 member : listGroupMember )
                    {
                        for ( uint32 edge = graph._listOffset[member]; edge < graph._listOffset[member + 1]; ++edge )
                        {
                            const uint32 target = graph._listTarget[edge];
                            if ( listCandidate[target]._item._group == group )
                                ++_listInDegree[target];
                        }
                    }

                    vector<uint32> listOrder;
                    listOrder.reserve( count );
                    for ( const uint32 member : listGroupMember )
                    {
                        if ( _listInDegree[member] == 0 )
                            listOrder.push_back( member );
                    }
                    uint32 maxLevel = 0;
                    for ( size_t cursor = 0; cursor < listOrder.size(); ++cursor )
                    {
                        const uint32 current  = listOrder[cursor];
                        _listVisited[current] = 1;
                        maxLevel              = MathUtil::max( maxLevel, _listLevel[current] );
                        for ( uint32 edge = graph._listOffset[current]; edge < graph._listOffset[current + 1]; ++edge )
                        {
                            const uint32 next = graph._listTarget[edge];
                            if ( listCandidate[next]._item._group != group )
                                continue;
                            _listLevel[next] = MathUtil::max( _listLevel[next], _listLevel[current] + graph._listReadsWrites[edge] );
                            if ( --_listInDegree[next] == 0 )
                                listOrder.push_back( next );
                        }
                    }

                    // 레벨마다 칸 스테이지. 레벨 수는 쓰기 간선 사슬의 깊이라 작다 — 레벨마다 위상 순서를 한 번 훑어 모은다.
                    vector<uint32> listLevel;
                    for ( uint32 level = 0; level <= maxLevel && listOrder.empty() == false; ++level )
                    {
                        listLevel.clear();
                        for ( const uint32 member : listOrder )
                        {
                            if ( _listLevel[member] == level )
                                listLevel.push_back( member );
                        }
                        if ( listLevel.empty() )
                            continue;
                        appendLevelStages( listCandidate, listLevel, graph, group, level, level > 0 || isWaitingOnObjectPath( listCandidate, listLevel ), outListStage );
                    }

                    // 순환 방어: 닿지 못한 것은 순서 키 순으로 마지막에 붙인다. 틱이 조용히 빠지는 것보다 낫다. 순환은 선행 조건을 잘못 건 데이터 결함이므로
                    // 오류로 알린다(스테이지는 선행 조건이 바뀔 때만 다시 만들어서 프레임마다 쏟아지지 않는다).
                    if ( listOrder.size() < count )
                    {
                        SW_LOG_ERROR_ID( "TickRegistry.SubTickPrerequisitesForm", "Sub-tick prerequisites form a cycle: %# sub-ticks cannot be ordered and run last in their group by order key - break the cycle",
                                         static_cast<uint32>( count - listOrder.size() ) );
                        vector<uint32> listRemaining;
                        for ( const uint32 member : listGroupMember )
                        {
                            if ( _listVisited[member] == 0 )
                                listRemaining.push_back( member );
                        }
                        sortLevel( listCandidate, listRemaining );
                        splitByObject( listRemaining, listCandidate, true, kCycleLevel, _listSlot, outListStage );
                    }
                }

            private:
                /**
                 * @brief 한 레벨(@p listLevel — 같은 그룹 · 같은 레벨의 후보)을 오브젝트별 칸 스테이지로 붙이고, 같은 칸의 순서만 간선을 스테이지 간선으로 적습니다.
                 * @details 레벨 안 간선은 모두 순서만이다(쓰기를 보는 간선은 레벨을 올린다). 순서 키 순위가 작은 것부터 위상 순(Kahn, 최소 힙)으로 걸으며 칸을 고른다 —
                 *          칸 = max(그 오브젝트의 다음 칸, 순서만 선행이 든 칸). 그래서 같은 오브젝트는 칸이 갈리고(한 스테이지에 하나), 간선의 뒤쪽은 앞쪽과 같은 칸
                 *          (스테이지 간선 → 태스크 간선)이거나 뒤 칸(장벽)이다. 간선이 없으면 순서 키 순 그대로라 옛 `splitByObject` 와 같은 칸이 나온다.
                 *          칸 안의 항목은 자리 키 순(`isBefore`)으로 놓는다 — 일괄 목록이 같은 타입 · 서브틱 이웃을 묶는다.
                 */
                void appendLevelStages( const vector<StageCandidate>& listCandidate, vector<uint32>& listLevel, const PrerequisiteGraph& graph, uint8 group, uint32 level,
                                        bool bWaits, vector<TickStage>& outListStage )
                {
                    sortLevel( listCandidate, listLevel );
                    for ( uint32 rank = 0; rank < listLevel.size(); ++rank )
                    {
                        const uint32 member       = listLevel[rank];
                        _listRank[member]         = rank;
                        _listInDegree[member]     = 0;
                        _listMinSlot[member]      = 0;
                        _listAssignedSlot[member] = 0;
                    }
                    const auto isLevelEdge = [this, &listCandidate, group, level]( uint32 target )
                    { return listCandidate[target]._item._group == group && _listVisited[target] != 0 && _listLevel[target] == level; };
                    for ( const uint32 member : listLevel )
                    {
                        for ( uint32 edge = graph._listOffset[member]; edge < graph._listOffset[member + 1]; ++edge )
                        {
                            if ( isLevelEdge( graph._listTarget[edge] ) )
                                ++_listInDegree[graph._listTarget[edge]];
                        }
                    }

                    vector<uint32> listReady; // 순위의 최소 힙
                    for ( const uint32 member : listLevel )
                    {
                        if ( _listInDegree[member] == 0 )
                            listReady.push_back( _listRank[member] );
                    }
                    std::make_heap( listReady.begin(), listReady.end(), std::greater<uint32>{} );
                    vector<vector<uint32>> listSlotMember;
                    while ( listReady.empty() == false )
                    {
                        std::pop_heap( listReady.begin(), listReady.end(), std::greater<uint32>{} );
                        const uint32 current = listLevel[listReady.back()];
                        listReady.pop_back();
                        uint32&      nextSlot      = _listSlot[listCandidate[current]._objectOrdinal];
                        const uint32 slot          = MathUtil::max( nextSlot, _listMinSlot[current] );
                        nextSlot                   = slot + 1;
                        _listAssignedSlot[current] = slot;
                        if ( slot == listSlotMember.size() )
                            listSlotMember.emplace_back();
                        listSlotMember[slot].push_back( current );
                        for ( uint32 edge = graph._listOffset[current]; edge < graph._listOffset[current + 1]; ++edge )
                        {
                            const uint32 next = graph._listTarget[edge];
                            if ( isLevelEdge( next ) == false )
                                continue;
                            _listMinSlot[next] = MathUtil::max( _listMinSlot[next], slot );
                            if ( --_listInDegree[next] == 0 )
                            {
                                listReady.push_back( _listRank[next] );
                                std::push_heap( listReady.begin(), listReady.end(), std::greater<uint32>{} );
                            }
                        }
                    }

                    for ( uint32 slot = 0; slot < listSlotMember.size(); ++slot )
                    {
                        vector<uint32>& listMember = listSlotMember[slot];
                        sortLevel( listCandidate, listMember );
                        TickStage& stage    = outListStage.emplace_back();
                        stage._group        = group;
                        stage._level        = level;
                        stage._bApplyBefore = ( bWaits && slot == 0 ) ? SW_TRUE : SW_FALSE;
                        for ( uint32 position = 0; position < listMember.size(); ++position )
                        {
                            const StageCandidate& candidate     = listCandidate[listMember[position]];
                            _listPosition[listMember[position]] = position;
                            stage._listItem.push_back( candidate._item );
                            stage._listItemOrder.push_back( candidate._stageOrder );
                        }
                        for ( const uint32 member : listMember )
                        {
                            for ( uint32 edge = graph._listOffset[member]; edge < graph._listOffset[member + 1]; ++edge )
                            {
                                const uint32 next = graph._listTarget[edge];
                                if ( isLevelEdge( next ) && _listAssignedSlot[next] == slot )
                                    stage._listEdge.push_back( TickStageEdge{ _listPosition[member], _listPosition[next] } );
                            }
                        }
                    }
                    for ( const uint32 member : listLevel )
                    {
                        _listSlot[listCandidate[member]._objectOrdinal] = 0;
                    }
                }

            private:
                vector<uint32> _listInDegree;     ///< 같은 그룹 후보 사이의 진입 차수(레벨 안에서는 같은 레벨 간선만 다시 센다)
                vector<uint8>  _listVisited;      ///< 위상 순서에 든 후보
                vector<uint32> _listLevel;        ///< 후보의 레벨 — 쓰기를 보는 간선만 하나 올린다
                vector<uint32> _listRank;         ///< 레벨 안 순서 키 순위(같은 레벨의 위상 순에서 작은 것이 먼저)
                vector<uint32> _listMinSlot;      ///< 같은 레벨 순서만 선행이 든 칸 중 가장 뒤 — 이보다 앞 칸에 들지 않는다
                vector<uint32> _listAssignedSlot; ///< 후보가 든 레벨 안 칸
                vector<uint32> _listPosition;     ///< 후보의 스테이지 안 자리(간선을 항목 번호로 바꿀 때)
                vector<uint32> _listSlot;         ///< 오브젝트 순번마다 이미 든 스테이지 수(`splitByObject` · 레벨 칸 고르기)
            };

            /**
             * @brief 그룹 @p group 의 레벨 0 에서 @p slot 번째 스테이지의 자리를 찾고, 없으면 레벨 0 의 끝(같은 그룹의 더 높은 레벨이나 다음 그룹이 시작하는 자리)에 만듭니다.
             * @return 그 스테이지의 `listStage` 안 자리입니다.
             */
            static size_t findOrCreateLevelZeroStage( vector<TickStage>& inoutListStage, uint8 group, uint32 slot )
            {
                size_t stageIndex = inoutListStage.size();
                size_t insertAt   = inoutListStage.size();
                uint32 seen       = 0;
                for ( size_t index = 0; index < inoutListStage.size(); ++index )
                {
                    const TickStage& stage       = inoutListStage[index];
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
                if ( stageIndex == inoutListStage.size() )
                {
                    TickStage created{};
                    created._group = group;
                    created._level = 0;
                    stageIndex     = insertAt;
                    inoutListStage.insert( inoutListStage.begin() + static_cast<ptrdiff_t>( insertAt ), std::move( created ) );
                }
                return stageIndex;
            }

            /** @brief 항목을 스테이지에 끼웁니다. 전체 재구축과 같은 순서 규칙(`isOrderedBefore`)을 쓰고, 같은 키면 뒤에 붙는다. */
            static void insertIntoStage( TickStage& inoutStage, const TickItem& item, const TickStageOrder& stageOrder )
            {
                size_t low  = 0;
                size_t high = inoutStage._listItem.size();
                while ( low < high )
                {
                    const size_t middle = low + ( high - low ) / 2;
                    if ( isOrderedBefore( stageOrder, inoutStage._listItemOrder[middle] ) )
                        high = middle;
                    else
                        low = middle + 1;
                }
                inoutStage._listItem.insert( inoutStage._listItem.begin() + static_cast<ptrdiff_t>( low ), item );
                inoutStage._listItemOrder.insert( inoutStage._listItemOrder.begin() + static_cast<ptrdiff_t>( low ), stageOrder );
                for ( TickStageEdge& edge : inoutStage._listEdge ) // 끼운 자리 뒤 항목의 번호가 하나씩 밀렸다
                {
                    edge._source += edge._source >= low ? 1u : 0u;
                    edge._target += edge._target >= low ? 1u : 0u;
                }
            }

            /** @brief 그룹 @p group 레벨 0 의 0 번 스테이지가 바깥 선행 조건을 기다리게 표시합니다(적용을 먼저 한다). */
            static void markApplyBefore( vector<TickStage>& inoutListStage, uint8 group )
            {
                for ( TickStage& stage : inoutListStage )
                {
                    if ( stage._group == group && stage._level == 0 )
                    {
                        stage._bApplyBefore = SW_TRUE; // 이 그룹 레벨 0 의 0 번 스테이지다
                        break;
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    TickRegistry::TickRegistry()
        : _arrListEntry{}
        , _arrListMainEntry{}
        , _arrListWave{}
        , _mapTypeDispatch{}
        , _bBatchPathEnabled{ SW_TRUE }
        , _batchGeneration{ 0 } // 처음에는 어차피 모두 다시 훑는다(`_bAllDirty`)
        , _listDirtyObjectID{}
        , _listProcessingObjectID{}
        , _dirtyMutex{}
        , _bAllDirty{ SW_TRUE }
        , _prerequisiteCount{ 0 }
        , _stageItemCount{ 0 }
        , _stageEditOrder{ 0 }
        , _listStageRemoval{}
        , _listStageInsertion{}
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
        ScopedLock<mutex> lock{ _dirtyMutex };
        _listDirtyObjectID.push_back( pObj->getObjectID() );
    }

    void TickRegistry::markAllDirty()
    {
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    bool TickRegistry::refresh( GameObjectManager& manager )
    {
        // 일괄 틱 타입이 등록 · 해제됐으면(모듈 적재 · 해제) 모두 다시 훑는다 — 오브젝트가 어느 목록에 갈지가 바뀌었고, 내려간 함수를 들고 있으면 안 된다.
        TickBatchRegistry* const pBatchRegistry  = manager.getSceneEngineServices()._pTickBatchRegistry;
        const uint32             batchGeneration = pBatchRegistry != nullptr ? pBatchRegistry->getGeneration() : 0;
        if ( batchGeneration != _batchGeneration )
        {
            _batchGeneration = batchGeneration;
            _mapTypeDispatch.clear();
            _bAllDirty.store( SW_TRUE, std::memory_order_release );
        }
        const bool bAll = _bAllDirty.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE;
        {
            ScopedLock<mutex> lock{ _dirtyMutex };
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
        // 다시 지으면 쌓인 편집은 버려진다(`rebuildStages`). 아니면 이번 `refresh` 의 편집을 스테이지에 한 번에 적용한다.
        const bool bStageChanged = bAll || _builtStageGeneration != _stageGeneration || _listStageRemoval.empty() == false || _listStageInsertion.empty() == false;
        if ( _builtStageGeneration != _stageGeneration )
            rebuildStages( manager );
        else
            flushStageEdits();
        if ( bStageChanged )
            refreshStageBatches( manager.getSceneEngineServices()._pTickBatchRegistry );
        return bRefreshedAny;
    }

    void TickRegistry::refreshObject( GameObjectManager& manager, GameObject* pObj )
    {
        const uint64  objectID      = pObj->getObjectID();
        const bool    bWasDependent = std::binary_search( _listDependentObjectID.begin(), _listDependentObjectID.end(), objectID );
        const bool    bReferenced   = std::binary_search( _listReferencedObjectID.begin(), _listReferencedObjectID.end(), objectID );
        TickItemList& listItem      = pObj->_listTickItem;
        // 지금 버퍼의 위치(칸 · 파도 원소)로 먼저 뺀다 — 버퍼를 다시 지으면 위치를 잃는다.
        removeMembership( pObj );
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
        VectorUtil::stableSort( listItem, []( const TickItem& left, const TickItem& right )
        {
            if ( left._group != right._group )
                return left._group < right._group;
            if ( left._bHasPrerequisite != right._bHasPrerequisite )
                return left._bHasPrerequisite < right._bHasPrerequisite;
            return left._orderKey < right._orderKey;
        } );

        // 계층에서 꺼진 오브젝트는 목록에 두지 않는다(언리얼 · 유니티가 꺼진 것의 틱을 목록에서 빼는 것과 같다). 켜지고 꺼질 때
        // `GameObject::refreshActiveInHierarchy` 가 표시하므로 다음 틱 전에 여기로 다시 온다. 항목 버퍼가 다시 지어졌으므로 칸도 새로 쓴다.
        const bool         bActive        = pObj->isActiveInHierarchy();
        TickBatchRegistry* pBatchRegistry = manager.getSceneEngineServices()._pTickBatchRegistry;
        uint32             cursor         = 0;
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
            if ( bActive && begin < end )
                addMembership( pBatchRegistry, pObj, group, begin, end );
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
                SW_PROFILE_COUNT( "GT.Scene.tick.stageDeltas", 1 ); // 말단 오브젝트의 항목만 끼우거나 뺐다
        }
    }

    void TickRegistry::queueStageRemoval( uint64 objectID )
    {
        StageRemoval removal{};
        removal._objectID = objectID;
        removal._order    = _stageEditOrder++;
        _listStageRemoval.push_back( removal );
    }

    void TickRegistry::flushStageEdits()
    {
        if ( _listStageRemoval.empty() && _listStageInsertion.empty() )
            return;

        // 오브젝트마다 마지막 제거 요청 하나만 남기고 id 순으로 둔다(스테이지를 훑을 때 이분 탐색한다).
        vector<StageRemoval>& listRemoval = _listStageRemoval;
        VectorUtil::sort( listRemoval, []( const StageRemoval& left, const StageRemoval& right )
        {
            if ( left._objectID != right._objectID )
                return left._objectID < right._objectID;
            return left._order < right._order;
        } );
        size_t keptRemoval = 0;
        for ( size_t index = 0; index < listRemoval.size(); ++index )
        {
            const bool bSupersededByLater = index + 1 < listRemoval.size() && listRemoval[index + 1]._objectID == listRemoval[index]._objectID;
            if ( bSupersededByLater == false )
                listRemoval[keptRemoval++] = listRemoval[index];
        }
        listRemoval.erase( listRemoval.begin() + static_cast<ptrdiff_t>( keptRemoval ), listRemoval.end() );
        const auto findRemoval = [&listRemoval]( uint64 objectID )
        {
            return std::lower_bound( listRemoval.begin(), listRemoval.end(), objectID, []( const StageRemoval& removal, uint64 id )
            { return removal._objectID < id; } );
        };

        // 제거: 스테이지마다 한 번 훑으며 요청된 오브젝트의 항목을 걷어 낸다. 남은 항목의 순서는 그대로다.
        if ( listRemoval.empty() == false )
        {
            SW_PROFILE_COUNT( "GT.Scene.tick.stageCompacts", 1 ); // 스테이지를 훑어 항목을 걷어 냈다
            vector<uint32> listNewIndex;                          // 간선이 있는 스테이지의 옛 번호 → 새 번호(빠졌으면 무효)
            for ( TickStage& stage : _listStage )
            {
                listNewIndex.assign( stage._listEdge.empty() ? 0 : stage._listItem.size(), kNotInList );
                size_t keptCount = 0;
                for ( size_t index = 0; index < stage._listItemOrder.size(); ++index )
                {
                    const uint64 objectID = stage._listItemOrder[index]._objectID;
                    const auto   position = findRemoval( objectID );
                    const bool   bRemoved = position != listRemoval.end() && position->_objectID == objectID;
                    if ( bRemoved )
                    {
                        --_stageItemCount;
                        continue;
                    }
                    if ( keptCount != index )
                    {
                        stage._listItem[keptCount]      = stage._listItem[index];
                        stage._listItemOrder[keptCount] = stage._listItemOrder[index];
                    }
                    if ( listNewIndex.empty() == false )
                        listNewIndex[index] = static_cast<uint32>( keptCount );
                    ++keptCount;
                }
                stage._listItem.resize( keptCount );
                stage._listItemOrder.resize( keptCount );
                if ( listNewIndex.empty() )
                    continue;
                // 빠진 항목에 닿은 간선은 버린다 — 빠진 쪽이 기다리던 간선이면 기다릴 이가 없고, 빠진 쪽을 기다리던 간선이면 그 순서는 이제 없다.
                size_t keptEdge = 0;
                for ( const TickStageEdge& edge : stage._listEdge )
                {
                    const uint32 source = listNewIndex[edge._source];
                    const uint32 target = listNewIndex[edge._target];
                    if ( source != kNotInList && target != kNotInList )
                        stage._listEdge[keptEdge++] = TickStageEdge{ source, target };
                }
                stage._listEdge.resize( keptEdge );
            }
        }

        // 끼움: 쌓인 순서대로 넣는다. 뒤에 같은 오브젝트의 제거 요청이 있었으면 그 요청 앞의 끼움은 낡았다. 자리는 전체 재구축과 같은 키(`TickStageOrder`)로
        // 고르므로 끼운 순서와 무관하게 같은 타입 · 서브틱의 이웃이 붙는다 — `refreshStageBatches` 가 그 이웃을 한 일괄 목록으로 묶는다.
        for ( const StageInsertion& insertion : _listStageInsertion )
        {
            const uint64 objectID    = insertion._stageOrder._objectID;
            const auto   position    = findRemoval( objectID );
            const bool   bSuperseded = position != listRemoval.end() && position->_objectID == objectID && position->_order > insertion._order;
            if ( bSuperseded )
                continue;
            const uint8  group      = insertion._item._group;
            const size_t stageIndex = TickRegistryInternal::findOrCreateLevelZeroStage( _listStage, group, insertion._slot );
            TickRegistryInternal::insertIntoStage( _listStage[stageIndex], insertion._item, insertion._stageOrder );
            ++_stageItemCount;
            if ( insertion._bMarkApplyBefore == SW_TRUE )
                TickRegistryInternal::markApplyBefore( _listStage, group );
        }
        _listStageRemoval.clear();
        _listStageInsertion.clear();
    }

    bool TickRegistry::applyStageDelta( GameObjectManager& manager, GameObject* pObj )
    {
        const uint64 objectID = pObj->getObjectID();
        queueStageRemoval( objectID );
        if ( pObj->isPendingDestroy() || pObj->isActiveInHierarchy() == false )
            return true;

        struct NewItem
        {
            TickItem        _item;
            optional<uint8> _externalGroup;
            optional<uint8> _externalWriteGroup; ///< 쓰기를 보는 선행 조건의 가장 늦은 그룹(적용을 먼저 할지)
        };
        vector<NewItem> listNew;
        for ( const TickItem& item : pObj->_listTickItem )
        {
            const bool bSkipItem = item._bHasPrerequisite == SW_FALSE || item._pComponent == nullptr || item._pComponent->isPendingDestroy();
            if ( bSkipItem )
                continue;
            const vector<SubTickPrerequisite>* pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
            if ( pListPrerequisite == nullptr )
                continue;
            NewItem entry{};
            entry._item = item;
            for ( const SubTickPrerequisite& prerequisite : *pListPrerequisite )
            {
                // 선행 조건이 의존 오브젝트(자기 자신 포함)를 가리키면 후보 사이의 간선이 생길 수 있다 — 말단이 아니다.
                if ( std::binary_search( _listDependentObjectID.begin(), _listDependentObjectID.end(), prerequisite._handle._objectID ) )
                    return false;
                TickRegistryInternal::insertSorted( _listReferencedObjectID, prerequisite._handle._objectID );
                TickRegistryInternal::foldExternalGroup( entry._externalGroup, entry._externalWriteGroup, TickRegistryInternal::findExternalGroup( manager, prerequisite._handle ),
                                                         prerequisite._kind );
            }
            if ( entry._externalGroup.has_value() && entry._item._group < *entry._externalGroup )
                entry._item._group = *entry._externalGroup;
            listNew.push_back( entry );
        }
        VectorUtil::stableSort( listNew, []( const NewItem& left, const NewItem& right )
        {
            if ( left._item._group != right._item._group )
                return left._item._group < right._item._group;
            return left._item._orderKey < right._item._orderKey;
        } );

        // 그룹마다 같은 오브젝트의 항목을 레벨 0 의 0 번 스테이지부터 차례로 넣는다(전체 재구축의 `splitByObject` 와 같은 규칙). 넣는 일은 `flushStageEdits` 가 한다.
        size_t cursor = 0;
        while ( cursor < listNew.size() )
        {
            const uint8 group  = listNew[cursor]._item._group;
            bool        bWaits = false;
            for ( uint32 slot = 0; cursor < listNew.size() && listNew[cursor]._item._group == group; ++cursor, ++slot )
            {
                const NewItem& entry        = listNew[cursor];
                bWaits                      = bWaits || ( entry._externalWriteGroup.has_value() &&
                                     *entry._externalWriteGroup >= TickRegistryInternal::getPhaseFirstGroup( group ) );
                const bool     bLastOfGroup = cursor + 1 == listNew.size() || listNew[cursor + 1]._item._group != group;
                StageInsertion insertion{};
                insertion._item             = entry._item;
                insertion._stageOrder       = makeStageOrder( entry._item, objectID );
                insertion._order            = _stageEditOrder++;
                insertion._slot             = slot;
                insertion._bMarkApplyBefore = ( bLastOfGroup && bWaits ) ? SW_TRUE : SW_FALSE;
                _listStageInsertion.push_back( insertion );
            }
        }
        return true;
    }

    void TickRegistry::setBatchPathEnabled( bool bEnabled )
    {
        const uint8 newValue = bEnabled ? SW_TRUE : SW_FALSE;
        if ( _bBatchPathEnabled == newValue )
            return;
        _bBatchPathEnabled = newValue;
        ++_stageGeneration; // 스테이지 자리 키가 바뀐다(`makeStageOrder`) — 증분 끼움이 옛 키의 항목 사이에 들지 않게 다시 짓는다
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    TickStageOrder TickRegistry::makeStageOrder( const TickItem& item, uint64 objectID ) const
    {
        TickStageOrder stageOrder{};
        stageOrder._objectID = objectID;
        stageOrder._orderKey = item._orderKey;
        if ( _bBatchPathEnabled == SW_TRUE )
        {
            const TypeInfo* pType = item._pComponent != nullptr ? item._pComponent->findCachedTypeInfo() : nullptr;
            stageOrder._typeKey   = pType != nullptr ? pType->_typeID : 0;
            stageOrder._subTickID = item._subTickID;
        }
        return stageOrder;
    }

    TickTypeDispatch TickRegistry::resolveTypeDispatch( TickBatchRegistry* pBatchRegistry, const Component* pComp )
    {
        const TypeInfo* pType = pComp->findCachedTypeInfo();
        auto            found = _mapTypeDispatch.find( pType );
        if ( found != _mapTypeDispatch.end() )
            return found->second;
        // 표가 없는 매니저(엔진 없는 테스트)는 모든 타입이 Batch 레인의 가상 호출 함수다.
        TickTypeDispatch lane = ( pBatchRegistry != nullptr && pType != nullptr ) ? pBatchRegistry->findDispatch( pType ) : TickTypeDispatch{};
        if ( lane._pFunction == nullptr )
            lane._pFunction = &tickBatchVirtual;
        _mapTypeDispatch.insert_or_assign( pType, lane );
        return lane;
    }

    void TickRegistry::addMembership( TickBatchRegistry* pBatchRegistry, GameObject* pObj, uint32 group, uint32 begin, uint32 end )
    {
        TickItemList& listItem = pObj->_listTickItem;
        // 레인은 그 오브젝트 그룹 항목의 가장 큰 선언이다 — 오브젝트 안 순서를 한 스레드(Task) · 게임 스레드(Main)가 지키게.
        TickMode lane = _bBatchPathEnabled == SW_TRUE ? TickMode::Batch : TickMode::Task;
        for ( uint32 index = begin; index < end && lane == TickMode::Batch; ++index )
        {
            const TickMode mode = resolveTypeDispatch( pBatchRegistry, listItem[index]._pComponent )._mode;
            if ( mode > lane )
                lane = mode;
            // 파도 키의 순번은 uint8 이다 — 같은 순서 키 항목이 256 개를 넘는 오브젝트는 파도 대신 칸으로 간다(순서는 그대로다).
            const bool bOrdinalOverflow  = index > begin && listItem[index - 1]._orderKey == listItem[index]._orderKey && listItem[index - 1]._waveOrdinal == TickRegistryInternal::kMaxWaveOrdinal;
            listItem[index]._waveOrdinal = ( index > begin && listItem[index - 1]._orderKey == listItem[index]._orderKey ) ? static_cast<uint8>( listItem[index - 1]._waveOrdinal + 1 ) : 0;
            if ( bOrdinalOverflow )
                lane = TickMode::Task;
        }

        if ( lane != TickMode::Batch )
        {
            TickObjectEntry entry{};
            entry._pObject                = pObj;
            entry._firstItem              = listItem[begin];
            entry._pItem                  = listItem.data() + begin;
            entry._itemCount              = end - begin;
            vector<TickObjectEntry>& list = lane == TickMode::Main ? _arrListMainEntry[group] : _arrListEntry[group];
            pObj->_arrTickIndex[group]    = static_cast<uint32>( list.size() );
            list.push_back( entry );
            return;
        }

        for ( uint32 index = begin; index < end; ++index )
        {
            TickItem&               item       = listItem[index];
            const TickBatchFunction pFunction  = resolveTypeDispatch( pBatchRegistry, item._pComponent )._pFunction;
            TickWave&               wave       = getOrCreateWave( group, item._orderKey, item._waveOrdinal );
            TickBatchList*          pBatchList = nullptr;
            for ( TickBatchList& batchList : wave._listBatch )
            {
                if ( batchList._pFunction == pFunction && batchList._subTickID == item._subTickID )
                {
                    pBatchList = &batchList;
                    break;
                }
            }
            if ( pBatchList == nullptr )
            {
                pBatchList             = &wave._listBatch.emplace_back();
                pBatchList->_pFunction = pFunction;
                pBatchList->_subTickID = item._subTickID;
            }
            item._batchIndex = static_cast<uint32>( pBatchList->_listElement.size() );
            pBatchList->_listElement.push_back( TickBatchElement{ item._pComponent, pObj } );
        }
    }

    TickWave& TickRegistry::getOrCreateWave( uint32 group, uint8 orderKey, uint8 ordinal )
    {
        vector<TickWave>& listWave = _arrListWave[group];
        size_t            position = 0;
        while ( position < listWave.size() )
        {
            const TickWave& wave = listWave[position];
            if ( wave._orderKey == orderKey && wave._ordinal == ordinal )
                return listWave[position];
            const bool bAfterKey = wave._orderKey > orderKey || ( wave._orderKey == orderKey && wave._ordinal > ordinal );
            if ( bAfterKey )
                break;
            ++position;
        }
        TickWave created{};
        created._orderKey = orderKey;
        created._ordinal  = ordinal;
        listWave.insert( listWave.begin() + static_cast<ptrdiff_t>( position ), std::move( created ) );
        return listWave[position];
    }

    void TickRegistry::removeEntry( vector<TickObjectEntry>& listEntry, uint32 group, uint32 index )
    {
        const TickObjectEntry moved          = listEntry.back();
        listEntry[index]                     = moved;
        moved._pObject->_arrTickIndex[group] = index;
        listEntry.pop_back();
    }

    void TickRegistry::removeWaveElement( GameObject* pObj, TickItem& item )
    {
        const uint32 index = item._batchIndex;
        item._batchIndex   = kNotInList;
        for ( TickWave& wave : _arrListWave[item._group] )
        {
            if ( wave._orderKey != item._orderKey || wave._ordinal != item._waveOrdinal )
                continue;
            // 한 오브젝트의 원소는 파도마다 하나라, 그 위치에 이 오브젝트가 있는 목록이 이 항목의 목록이다.
            for ( TickBatchList& batchList : wave._listBatch )
            {
                vector<TickBatchElement>& listElement = batchList._listElement;
                if ( index >= listElement.size() || listElement[index]._pObject != pObj )
                    continue;
                const uint32           lastIndex = static_cast<uint32>( listElement.size() - 1 );
                const TickBatchElement moved     = listElement[lastIndex];
                listElement[index]               = moved;
                listElement.pop_back();
                if ( lastIndex == index )
                    return;
                // 옮긴 오브젝트의 이 파도 항목은 같은 (그룹, 파도 키)에 끝 위치를 든 항목 하나다.
                for ( TickItem& movedItem : moved._pObject->_listTickItem )
                {
                    const bool bMovedItem = movedItem._group == item._group && movedItem._orderKey == item._orderKey && movedItem._waveOrdinal == item._waveOrdinal &&
                                            movedItem._batchIndex == lastIndex;
                    if ( bMovedItem )
                    {
                        movedItem._batchIndex = index;
                        return;
                    }
                }
                return;
            }
            return;
        }
    }

    void TickRegistry::removeMembership( GameObject* pObj )
    {
        for ( TickItem& item : pObj->_listTickItem )
        {
            if ( item._batchIndex != kNotInList )
                removeWaveElement( pObj, item );
        }
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            uint32& index = pObj->_arrTickIndex[group];
            if ( index == kNotInList )
                continue;
            vector<TickObjectEntry>& listTask = _arrListEntry[group];
            vector<TickObjectEntry>& listMain = _arrListMainEntry[group];
            if ( index < listTask.size() && listTask[index]._pObject == pObj )
                removeEntry( listTask, group, index );
            else if ( index < listMain.size() && listMain[index]._pObject == pObj )
                removeEntry( listMain, group, index );
            index = kNotInList;
        }
    }

    void TickRegistry::refreshStageBatches( TickBatchRegistry* pBatchRegistry )
    {
        // 스테이지 항목도 레인의 일괄 함수로 돈다(가상 호출 대신). 자리 키가 같은 타입 · 서브틱을 붙여 두었으므로 이웃한 같은 (함수, 서브틱 id) 를 한 목록으로
        // 묶으면 디스패치가 파도와 같은 잡으로 목록 하나를 함수 한 번에 넘긴다. 한 스테이지에 한 오브젝트의 항목은 하나라 묶어도 오브젝트 안 순서는 그대로다.
        // 순서만 간선이 있는 스테이지는 항목마다 목록 하나다 — 디스패치가 목록마다 태스크를 띄우고 간선(항목 번호)을 그대로 건다.
        for ( TickStage& stage : _listStage )
        {
            stage._listBatch.clear();
            const bool bPerItem = stage._listEdge.empty() == false;
            for ( const TickItem& item : stage._listItem )
            {
                Component* const pComp = item._pComponent;
                if ( pComp == nullptr )
                {
                    if ( bPerItem )
                        stage._listBatch.emplace_back(); // 빈 목록 — 항목 번호와 목록 번호를 맞춘다
                    continue;
                }
                const TickBatchFunction pFunction   = resolveTypeDispatch( pBatchRegistry, pComp )._pFunction;
                const bool              bSameAsLast = bPerItem == false && stage._listBatch.empty() == false && stage._listBatch.back()._pFunction == pFunction &&
                                         stage._listBatch.back()._subTickID == item._subTickID;
                if ( bSameAsLast == false )
                {
                    TickBatchList& created = stage._listBatch.emplace_back();
                    created._pFunction     = pFunction;
                    created._subTickID     = item._subTickID;
                }
                stage._listBatch.back()._listElement.push_back( TickBatchElement{ pComp, pComp->getOwner() } );
            }
        }
    }

    void TickRegistry::removeEmptyBatchLists()
    {
        for ( vector<TickWave>& listWave : _arrListWave )
        {
            // 목록 · 파도의 자리는 아무도 들지 않는다(항목은 목록 안의 위치만 든다) — 순서를 지켜 걷기만 한다.
            for ( TickWave& wave : listWave )
            {
                (void)VectorUtil::removeIf( wave._listBatch, []( const TickBatchList& batchList )
                { return batchList._listElement.empty(); } );
            }
            (void)VectorUtil::removeIf( listWave, []( const TickWave& wave )
            { return wave._listBatch.empty(); } );
        }
    }

    void TickRegistry::unregisterObject( GameObject* pObj )
    {
        if ( pObj == nullptr )
            return;
        removeMembership( pObj );
        const uint64 objectID = pObj->getObjectID();
        // 스테이지가 이 오브젝트의 항목을 들고 있거나(선행 조건을 가진 쪽) 이 오브젝트를 기다린다(가리켜진 쪽) — 다음 틱 전에 다시 짓는다.
        const bool bWasDependent = TickRegistryInternal::eraseSorted( _listDependentObjectID, objectID );
        if ( std::binary_search( _listReferencedObjectID.begin(), _listReferencedObjectID.end(), objectID ) )
            ++_stageGeneration;
        else if ( bWasDependent && _builtStageGeneration == _stageGeneration )
            queueStageRemoval( objectID ); // 아무도 기다리지 않으니 자기 항목만 뺀다(스폰이 잦은 팔로워가 전체 재구축을 부르지 않게). 다음 `refresh` 가 한 번에 뺀다
        _prerequisiteCount -= pObj->_tickPrerequisiteCount;
        pObj->_tickPrerequisiteCount = 0;
        pObj->_listTickItem.clear();
    }

    void TickRegistry::clear()
    {
        // 오브젝트가 든 위치(`_arrTickIndex` · 항목의 `_batchIndex`)는 낡지만, 빼는 쪽이 그 위치에 자기가 있는지 보고 빼므로 다음 `refresh` 가 안전하게 다시 넣는다.
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            _arrListEntry[group].clear();
            _arrListMainEntry[group].clear();
            _arrListWave[group].clear();
        }
        _mapTypeDispatch.clear();
        {
            ScopedLock<mutex> lock{ _dirtyMutex };
            _listDirtyObjectID.clear();
        }
        _listProcessingObjectID.clear();
        _prerequisiteCount = 0;
        _listDependentObjectID.clear();
        _listReferencedObjectID.clear();
        _listStage.clear();
        _listStageRemoval.clear();
        _listStageInsertion.clear();
        _stageItemCount = 0;
        ++_stageGeneration;
        _bAllDirty.store( SW_TRUE, std::memory_order_release );
    }

    void TickRegistry::rebuildStages( GameObjectManager& manager )
    {
        _builtStageGeneration = _stageGeneration;
        SW_PROFILE_COUNT( "GT.Scene.tick.stageRebuilds", 1 ); // 스테이지를 다시 만들었다 — 잦으면 틱 멤버십이 흔들린다
        _listStage.clear();
        _listStageRemoval.clear(); // 처음부터 다시 짓는다 — 쌓인 편집은 필요 없다
        _listStageInsertion.clear();
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
                const vector<SubTickPrerequisite>* pListPrerequisite = TickRegistryInternal::findPrerequisiteList( item );
                if ( pListPrerequisite == nullptr )
                    continue;
                TickRegistryInternal::StageCandidate candidate{};
                candidate._item              = item;
                candidate._stageOrder        = makeStageOrder( item, objectID );
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
        SubTickHandle   lastHandle{};
        optional<uint8> lastExternalGroup;
        for ( TickRegistryInternal::StageCandidate& candidate : listCandidate )
        {
            for ( const SubTickPrerequisite& prerequisite : *candidate._pListPrerequisite )
            {
                const SubTickHandle& handle = prerequisite._handle;
                if ( _listReferencedObjectID.empty() || _listReferencedObjectID.back() != handle._objectID )
                    _listReferencedObjectID.push_back( handle._objectID );
                if ( candidateIndex.find( handle._componentID, handle._subTickID ).has_value() )
                    continue;
                if ( handle != lastHandle || lastHandle.isValid() == false )
                {
                    lastHandle        = handle;
                    lastExternalGroup = TickRegistryInternal::findExternalGroup( manager, handle );
                }
                TickRegistryInternal::foldExternalGroup( candidate._externalGroup, candidate._externalWriteGroup, lastExternalGroup, prerequisite._kind );
            }
        }
        VectorUtil::sortUnique( _listReferencedObjectID );

        TickRegistryInternal::PrerequisiteGraph graph;
        TickRegistryInternal::buildPrerequisiteGraph( listCandidate, candidateIndex, graph );
        TickRegistryInternal::raiseGroupsToPrerequisites( listCandidate, graph );

        TickRegistryInternal::StageBuilder builder{ listCandidate.size(), objectOrdinal };
        vector<uint32>                     listGroupMember;
        listGroupMember.reserve( listCandidate.size() );
        for ( uint32 group = 0; group < kGroupCount; ++group )
        {
            listGroupMember.clear();
            for ( uint32 index = 0; index < listCandidate.size(); ++index )
            {
                if ( listCandidate[index]._item._group == group )
                    listGroupMember.push_back( index );
            }
            builder.appendGroupStages( listCandidate, listGroupMember, graph, static_cast<uint8>( group ), _listStage );
        }
        _stageItemCount = static_cast<uint32>( listCandidate.size() );
    }
} // namespace sw
