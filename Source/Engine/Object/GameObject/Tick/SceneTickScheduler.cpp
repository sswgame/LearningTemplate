/**
 * @file SceneTickScheduler.cpp
 * @brief 컴포넌트 틱 디스패치 — 그룹 포크-조인(오브젝트 칸 + 타입별 일괄 목록) · 선행 조건 스테이지 · 스테이지 경계의 트랜스폼 적용입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineParallel.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    /**
     * @brief `-gv_tickCostDispatch=1`: 씬 틱의 병렬 분배를 앞 항목을 직접 돌아 측정한 비용으로 고릅니다(`TaskManager::runParallelByCost`). 기동 때 정하는 값입니다.
     * @details 기본(0)은 옛 방식(항목 16 개 이상이면 늘 나눈다)이다. 실제 게임 여덟 개에서 이득은 한 곳뿐이고 일곱 곳은 같거나 느렸다
     *          (결정 기록 5-6). 상용 엔진도 틱 분배는 정적 그레인 크기를 쓴다. 프레임 사이에 비용을 기억하는 방식을 재 보기 위해 남긴 스위치다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_tickCostDispatch, false, "씬 틱 병렬 분배를 잰 비용으로 고른다(0 이면 항목 16 개 이상이면 늘 나눈다 — 기본)" );

    namespace
    {
        struct SceneTickSchedulerInternal
        {
            /** @brief 옛 방식의 임계값입니다: 항목이 이 수보다 적으면 나누지 않고 이 스레드가 돕니다(`gv_tickCostDispatch` 가 0 일 때만). */
            static constexpr uint32 kLegacyParallelThreshold = 16;

            /** @brief [0, count) 를 병렬 분배합니다. 스위치가 켜지면 측정한 비용으로, 기본(꺼짐)은 옛 고정 임계값으로 고릅니다. */
            static void dispatch( uint32 count, const ParallelBlockDelegate& body )
            {
                if ( gv_tickCostDispatch )
                    engine::runParallelByCost( count, body );
                else
                    engine::runParallel( count, kLegacyParallelThreshold, body );
            }

            /** @brief 항목 하나를 돌립니다. 주 틱이면 `onTick`, 서브틱이면 `onSubTick` 입니다. 살아 있고 켜져 있는지는 부르는 쪽이 이미 봤습니다. */
            static void runTickItem( float32 deltaTime, const TickItem& item )
            {
                Component* pComp = item._pComponent;
                if ( item._subTickID == 0 )
                {
                    if ( pComp->canEverTick() )
                        pComp->onTick( deltaTime );
                }
                else
                {
                    if ( pComp->isSubTickActive( item._subTickID ) )
                        pComp->onSubTick( item._subTickID, deltaTime );
                }
            }

            /** @brief 항목 하나를 삭제 대기 · 자기 활성만 보고 돌립니다. 소유 오브젝트는 보지 않습니다(레지스트리 칸 설명). */
            static void runTickItemIfLive( float32 deltaTime, const TickItem& item )
            {
                Component* pComp                  = item._pComponent;
                const bool bInactiveTickComponent = pComp == nullptr || pComp->isPendingDestroy() || pComp->isSelfActive() == false;
                if ( bInactiveTickComponent )
                    return;
                runTickItem( deltaTime, item );
            }

            /**
             * @brief 레지스트리 칸 하나(오브젝트 하나의 그룹 항목)를 순서대로 틱합니다. 워커에서 불립니다.
             * @details **게임 오브젝트를 읽지 않습니다.** 계층에서 꺼진 오브젝트는 칸이 없고(목록 소속이 곧 활성), 틱 중의 `setActive` 는 틱
             *          뒤로 미뤄지며, 파괴는 컴포넌트마다 삭제 표시를 세웁니다 — 그래서 컴포넌트의 표시만 보면 됩니다. 언리얼 틱 함수가 대상
             *          컴포넌트만 보고 액터는 보지 않는 것과 같습니다(오브젝트를 읽으면 큐브 8000 개 프로파일에서 틱 CPU 의 절반이 이 루프다).
             */
            static void tickEntry( TickThreadState& threadState, float32 deltaTime, const TickObjectEntry& entry )
            {
                // 이 오브젝트의 항목은 이 스레드만 돈다. 그동안 그 씬 컴포넌트의 세터는 칸에 바로 쓴다(`SceneComponent::writeTickTransform`).
                threadState._pTickingObject = entry._pObject;
                threadState._pTickWriter    = entry._pObject;
                runTickItemIfLive( deltaTime, entry._firstItem );
                for ( uint32 index = 1; index < entry._itemCount; ++index )
                {
                    runTickItemIfLive( deltaTime, entry._pItem[index] );
                }
            }

            /**
             * @brief 청크 하나를 시작합니다 — 이 스레드의 틱 상태에 대기 칸 목록을 한 번 넣어 둡니다(세터가 스레드 슬롯을 다시 묻지 않게).
             * @details 청크 도중 이 스레드가 기다리며 다른 틱 청크를 돌면 그 청크의 끝이 상태를 비운다. 그 뒤의 세터는 쓰기 큐로 가므로 결과는 같다.
             */
            static TickThreadState& beginRange( SceneTransformHierarchy& hierarchy )
            {
                TickThreadState& threadState  = StructuralChangeBuffer::getThreadState();
                threadState._pPendingSlotList = hierarchy.findPendingSlotList();
                return threadState;
            }

            /** @brief 청크를 끝냅니다 — 틱 상태를 비웁니다(틱 밖의 세터가 대기 칸으로 가지 않게). */
            static void endRange( TickThreadState& threadState )
            {
                threadState._pTickingObject   = nullptr;
                threadState._pTickWriter      = nullptr;
                threadState._pPendingSlotList = nullptr;
            }

            /**
             * @brief 한 그룹의 보통 길을 [start, end) 로 나눠 도는 잡 본문입니다. 워커는 포인터만 받습니다.
             * @details 번호 공간은 [0, 오브젝트 칸 수) 가 오브젝트 칸, 그 뒤가 타입별 일괄 목록을 차례로 이어 붙인 것이다(`_pBatchStart`). 한 칸 · 한 원소는
             *          한 오브젝트라 나눠지지 않는다. 일괄 구간은 겹치는 목록마다 그 타입의 함수를 한 번 부른다.
             */
            struct ObjectGroupTick
            {
                const TickObjectEntry*   _pEntry{ nullptr };
                const TickBatchList*     _pBatchList{ nullptr };
                const uint32*            _pBatchStart{ nullptr }; ///< 일괄 목록마다 시작 번호, 끝에 전체 수(`_batchListCount + 1` 개)
                SceneTransformHierarchy* _pHierarchy{ nullptr };
                uint32                   _entryCount{ 0 };
                uint32                   _batchListCount{ 0 };
                float32                  _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    // 청크 하나의 CPU 시간이다. 모든 스레드의 합이라 `GT.Scene.tick.components`(벽시계)와 비교하면 병렬 효율이 보인다.
                    SW_PROFILE_SCOPE( "GT.Scene.tick.objectRange" );
                    TickThreadState& threadState = beginRange( *_pHierarchy );
                    const uint32     entryEnd    = MathUtil::min( end, _entryCount );
                    for ( uint32 index = start; index < entryEnd; ++index )
                    {
                        tickEntry( threadState, _deltaTime, _pEntry[index] );
                    }
                    if ( end > _entryCount )
                        tickBatchRange( threadState, MathUtil::max( start, _entryCount ), end );
                    endRange( threadState );
                }

                /** @brief 일괄 번호 [start, end) 를 목록마다 잘라 그 타입의 함수에 넘깁니다. */
                void tickBatchRange( TickThreadState& threadState, uint32 start, uint32 end ) const
                {
                    // start 를 담은 목록: 시작 번호가 start 보다 큰 첫 목록의 바로 앞(빈 목록은 시작 번호가 같아 건너뛰어진다).
                    uint32 listIndex = static_cast<uint32>( std::upper_bound( _pBatchStart, _pBatchStart + _batchListCount, start ) - _pBatchStart ) - 1u;
                    for ( uint32 cursor = start; cursor < end && listIndex < _batchListCount; ++listIndex )
                    {
                        const TickBatchList& batchList = _pBatchList[listIndex];
                        const uint32         rangeEnd  = MathUtil::min( end, _pBatchStart[listIndex + 1] );
                        if ( cursor < rangeEnd )
                            batchList._pFunction( threadState, batchList._listElement.data() + ( cursor - _pBatchStart[listIndex] ), rangeEnd - cursor, _deltaTime );
                        cursor = MathUtil::max( cursor, rangeEnd );
                    }
                }
            };

            /**
             * @brief 선행 조건 스테이지 하나의 항목 [start, end) 를 도는 잡 본문입니다. 항목마다 오브젝트가 다르므로 소유자도 봅니다(항목이 적다).
             * @details 한 스테이지에 한 오브젝트의 항목은 하나뿐이라 자기 오브젝트 쓰기는 보통 길처럼 대기 칸으로 간다.
             */
            struct StageTick
            {
                const TickItem*          _pItem{ nullptr };
                SceneTransformHierarchy* _pHierarchy{ nullptr };
                float32                  _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    TickThreadState& threadState = beginRange( *_pHierarchy );
                    for ( uint32 index = start; index < end; ++index )
                    {
                        const TickItem& item               = _pItem[index];
                        Component*      pComp              = item._pComponent;
                        const bool      bInactiveComponent = pComp == nullptr || pComp->isPendingDestroy() || pComp->isActive() == false;
                        if ( bInactiveComponent )
                            continue;
                        // 소유 오브젝트의 삭제 대기는 다시 보지 않는다. 그 오브젝트의 컴포넌트는 항상 삭제 대기다(`GameObject::isPendingDestroy`).
                        GameObject* pOwner = pComp->getOwner();
                        if ( pOwner == nullptr )
                            continue;
                        threadState._pTickingObject = pOwner;
                        threadState._pTickWriter    = pOwner;
                        runTickItem( _deltaTime, item );
                    }
                    endRange( threadState );
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SceneTickScheduler::SceneTickScheduler( GameObjectManager& manager, SceneTransformHierarchy& hierarchy, PrimitiveRegistry& primitiveRegistry,
                                            StructuralChangeBuffer& structuralChangeBuffer )
        : _pManager{ &manager }
        , _pHierarchy{ &hierarchy }
        , _pPrimitiveRegistry{ &primitiveRegistry }
        , _pStructuralChangeBuffer{ &structuralChangeBuffer }
        , _tickRegistry{}
        , _listBatchStart{}
        , _stageBuildCount{ 0 }
        , _stageTransformApplyCount{ 0 }
    {
    }

    void SceneTickScheduler::tickPhase( float32 deltaTime, uint32 firstGroup, uint32 endGroup )
    {
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransforms" );
            _pHierarchy->flush();
        }

        // 틱 중의 세터가 쓸 대기 칸 목록 · 쓰기 큐를 슬롯 수만큼 미리 잡아 둔다(워커는 자기 칸만 만진다).
        _pHierarchy->beginTickWrites();
        _pStructuralChangeBuffer->freeze();

        {
            // **씬 틱은 자기가 낸 일만 기다린다.** 틱의 병렬 일은 `runParallel` 이 합류까지 기다린다. 주의: `TaskManager::waitAll()` 은
            // 엔진 **전체의** 태스크 — 렌더 스레드의 패스 기록, 에셋 스트리밍, 비동기 씬 로드, 오디오 재생 — 가 빌 때까지 게임 스레드를
            // 세운다. onTick 이 낸 태스크가 틱 뒤 단계 전에 끝나야 하면 자기 스테이지를 만들어 `waitStage` 로 기다린다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.components" );
            tickGroups( deltaTime, firstGroup, endGroup );
        }

        _pStructuralChangeBuffer->thaw();
    }

    void SceneTickScheduler::tickGroups( float32 deltaTime, uint32 firstGroup, uint32 endGroup )
    {
        {
            // 멤버십이 바뀐 오브젝트만 항목을 다시 짓는다. 씬 전체를 훑지 않는다.
            SW_PROFILE_SCOPE( "GT.Scene.tick.registry" );
            if ( _tickRegistry.refresh( *_pManager ) )
                _stageBuildCount.fetch_add( 1, std::memory_order_relaxed );
        }

        // 그룹마다: 보통 길(오브젝트 칸 목록을 한 번의 포크-조인) → 그 그룹의 선행 조건 스테이지. 한 오브젝트의 칸은 한 워커가 (순서 키 순으로)
        // 돌므로 같은 오브젝트의 컴포넌트 둘이 동시에 돌지 않고, 스테이지는 보통 길이 끝난 뒤라 겹치지 않는다. 선행 조건을 가진 항목만
        // 스테이지로 간다 — 사슬 밖 오브젝트는 선행 조건이 씬에 있든 없든 같은 길이다. 보통 길이 먼저라 선행 조건이 보통 길의 항목이어도 순서가 맞다.
        const vector<TickStage>& listStage  = _tickRegistry.getStages();
        size_t                   stageIndex = 0;
        for ( uint32 group = firstGroup; group < endGroup && group < TickRegistry::kGroupCount; ++group )
        {
            // 오브젝트 칸과 타입별 일괄 목록은 한 번호 공간으로 이어 한 번의 포크-조인으로 돈다(일괄 목록을 따로 돌면 워커를 한 번 더 깨운다).
            const vector<TickObjectEntry>& listEntry  = _tickRegistry.getEntries( group );
            const vector<TickBatchList>&   listBatch  = _tickRegistry.getBatchLists( group );
            const uint32                   entryCount = static_cast<uint32>( listEntry.size() );
            uint32                         totalCount = entryCount;
            _listBatchStart.clear();
            for ( const TickBatchList& batchList : listBatch )
            {
                _listBatchStart.push_back( totalCount );
                totalCount += static_cast<uint32>( batchList._listElement.size() );
            }
            _listBatchStart.push_back( totalCount );
            if ( totalCount > 0 )
            {
                SW_PROFILE_COUNT( "GT.Scene.tick.objectEntries", entryCount );
                SW_PROFILE_COUNT( "GT.Scene.tick.batchElements", totalCount - entryCount );
                SceneTickSchedulerInternal::ObjectGroupTick job{};
                job._pEntry         = listEntry.data();
                job._pBatchList     = listBatch.data();
                job._pBatchStart    = _listBatchStart.data();
                job._pHierarchy     = _pHierarchy;
                job._entryCount     = entryCount;
                job._batchListCount = static_cast<uint32>( listBatch.size() );
                job._deltaTime      = deltaTime;
                SceneTickSchedulerInternal::dispatch( totalCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &SceneTickSchedulerInternal::ObjectGroupTick::tickRange, &job ) );
            }

            // 스테이지는 그룹 순서로 지어진다(한 스테이지 = 한 그룹).
            while ( stageIndex < listStage.size() && listStage[stageIndex]._group < group )
            {
                ++stageIndex;
            }
            for ( ; stageIndex < listStage.size() && listStage[stageIndex]._group == group; ++stageIndex )
            {
                const TickStage& stage = listStage[stageIndex];
                if ( stage._listItem.empty() ) // 증분 갱신이 비워 둔 스테이지는 돌 것도 적용할 것도 없다
                    continue;
                // 앞에서 돈 선행 조건(앞 스테이지 · 보통 길)이 쓴 트랜스폼을 이 스테이지가 같은 프레임에 읽게 한다. 기다리는 스테이지 앞에서만.
                if ( stage._bApplyBefore == SW_TRUE )
                    applyStageTransforms();
                SceneTickSchedulerInternal::StageTick job{};
                job._pItem      = stage._listItem.data();
                job._pHierarchy = _pHierarchy;
                job._deltaTime  = deltaTime;
                SceneTickSchedulerInternal::dispatch( static_cast<uint32>( stage._listItem.size() ),
                                                      SW_DELEGATE_METHOD( ParallelBlockDelegate, &SceneTickSchedulerInternal::StageTick::tickRange, &job ) );
            }
        }
    }

    void SceneTickScheduler::applyStageTransforms()
    {
        // 부른 순서: 계층 변경(attach · detach)이 이번 단계에서 미뤄졌으면 그 뒤에 부른 쓰기는 그 변경 **뒤에** 적용돼야 한다(`KeepWorld` 는
        // 붙이기 전 월드에서 로컬을 다시 구한다). 구조 변경은 단계 끝에서만 돌므로, 그때부터는 쓰기도 단계 끝까지 둔다.
        if ( _pStructuralChangeBuffer->hasDeferredHierarchyChange() )
            return;
        SW_PROFILE_SCOPE( "GT.Scene.tick.stageTransforms" );
        ++_stageTransformApplyCount;
        // 스테이지 사이엔 워커가 돌지 않는다 — 틱 뒤 적용과 같은 길(대기 칸 → 쓰기 큐 → 잎 루트 합성 · 더티 루트)에 플러시까지 해서
        // 다음 스테이지가 로컬 · 월드 값을 모두 읽게 한다. 동결은 그대로다(구조 변경 · 스폰은 여전히 단계 끝).
        _pHierarchy->applyTickWrites( *_pManager, *_pPrimitiveRegistry );
        if ( _pHierarchy->hasDirty() )
            _pHierarchy->flush();
    }
} // namespace sw
