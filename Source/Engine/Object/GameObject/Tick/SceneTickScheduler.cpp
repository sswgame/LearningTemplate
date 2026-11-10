/**
 * @file SceneTickScheduler.cpp
 * @brief 컴포넌트 틱 디스패치 — 그룹마다 레인(Task 태스크 · Batch 파도 · Main 게임 스레드) · 선행 조건 스테이지 · 스테이지 경계의 트랜스폼 적용입니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct SceneTickSchedulerInternal
        {
            /**
             * @brief 병렬 분배 임계값입니다: 항목이 이 수보다 적으면 나누지 않고 이 스레드가 돕니다.
             * @details 잰 비용으로 고르는 분배(`runParallelByCost`)도 재 봤으나 실제 게임 여덟 개 중 이득은 한 곳뿐이고 일곱 곳은 같거나 느렸다 — 상용 엔진(언리얼 틱 태스크)도 정적 그레인 크기다.
             */
            static constexpr uint32 kParallelThreshold = 16;

            /** @brief [0, count) 를 고정 임계값으로 병렬 분배합니다. */
            static void dispatch( uint32 count, const ParallelBlockDelegate& body ) { engine::runParallel( count, kParallelThreshold, body ); }

            /** @brief 항목 하나를 돌립니다. 주 틱이면 `onTick`, 서브틱이면 `onSubTick` 입니다. 살아 있고 켜져 있는지는 부르는 쪽이 이미 봤습니다. */
            static void runTickItem( float32 deltaTime, const TickItem& item )
            {
                Component* pComp = item._pComponent;
                if ( item._subTickID == 0 )
                {
                    if ( pComp->canEverTick() )
                    {
                        TickContext context{ *pComp->getOwner(), deltaTime };
                        pComp->onTick( context );
                    }
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
             * @brief 청크 하나의 틱 상태 구간입니다 — 이 스레드의 틱 상태에 대기 칸 목록 · 디스패치 순번을 넣고, 끝날 때 **들어올 때의 상태로 되돌립니다.**
             * @details 틱 본문 안의 중첩 `runParallel` 이 기다리며 다른 틱 청크를 도울 수 있다. 예전에는 그 청크의 끝이 상태를 비워, 돌아온 바깥 틱의 남은
             *          세터가 주인 없는 쓰기(writerID 0)가 되어 순서 키가 틀어졌다. 되돌리기로 중첩이 바깥 상태를 지우는 모양 자체를 없앤다.
             */
            struct TickThreadScope
            {
                TickThreadState& _threadState;
                TickThreadState  _saved;

                TickThreadScope( SceneTransformHierarchy& hierarchy, uint32 dispatchOrdinal )
                    : _threadState{ StructuralChangeBuffer::getThreadState() }
                    , _saved{ _threadState }
                {
                    _threadState._pPendingSlotList = hierarchy.findPendingSlotList();
                    _threadState._dispatchOrdinal  = dispatchOrdinal;
                }
                ~TickThreadScope() { _threadState = _saved; }

                TickThreadScope( const TickThreadScope& )            = delete;
                TickThreadScope& operator=( const TickThreadScope& ) = delete;
            };

            /**
             * @brief 오브젝트 칸 목록 [start, end) 를 도는 잡 본문입니다(일괄 경로를 끈 기준 길 · Main 레인). 한 칸 = 한 오브젝트라 나눠지지 않는다.
             */
            struct EntryTick
            {
                const TickObjectEntry*   _pEntry{ nullptr };
                SceneTransformHierarchy* _pHierarchy{ nullptr };
                uint32                   _dispatchOrdinal{ 0 };
                float32                  _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    // 청크 하나의 CPU 시간이다. 모든 스레드의 합이라 `GT.Scene.tick.components`(벽시계)와 비교하면 병렬 효율이 보인다.
                    SW_PROFILE_SCOPE( "GT.Scene.tick.objectRange" );
                    const TickThreadScope scope{ *_pHierarchy, _dispatchOrdinal };
                    uint32                itemCount = 0;
                    for ( uint32 index = start; index < end; ++index )
                    {
                        tickEntry( scope._threadState, _deltaTime, _pEntry[index] );
                        itemCount += _pEntry[index]._itemCount;
                    }
#if defined( SW_DEBUG )
                    SW_PROFILE_COUNT( "GT.Scene.tick.virtualTicks", itemCount ); // 칸 길은 항목마다 가상 호출이다
#else
                    (void)itemCount;
#endif
                }
            };

            /**
             * @brief 파도 · 선행 조건 스테이지 하나의 일괄 목록을 이은 번호 [start, end) 를 도는 잡 본문입니다. 워커는 포인터만 받습니다.
             * @details 번호 공간은 파도의 일괄 목록을 차례로 이어 붙인 것이다(`_pBatchStart`). 한 원소는 한 오브젝트의 항목 하나라 나눠지지 않는다.
             *          겹치는 목록마다 그 (타입 함수, 서브틱 id) 를 한 번 부른다.
             */
            struct BatchListTick
            {
                const TickBatchList*     _pBatchList{ nullptr };
                const uint32*            _pBatchStart{ nullptr }; ///< 일괄 목록마다 시작 번호, 끝에 전체 수(`_batchListCount + 1` 개)
                SceneTransformHierarchy* _pHierarchy{ nullptr };
                uint32                   _batchListCount{ 0 };
                uint32                   _dispatchOrdinal{ 0 };
                float32                  _deltaTime{ 0.0f };

                void tickRange( uint32 start, uint32 end )
                {
                    SW_PROFILE_SCOPE( "GT.Scene.tick.waveRange" );
                    const TickThreadScope scope{ *_pHierarchy, _dispatchOrdinal };
                    // start 를 담은 목록: 시작 번호가 start 보다 큰 첫 목록의 바로 앞(빈 목록은 시작 번호가 같아 건너뛰어진다).
                    uint32 listIndex = static_cast<uint32>( std::upper_bound( _pBatchStart, _pBatchStart + _batchListCount, start ) - _pBatchStart ) - 1u;
                    for ( uint32 cursor = start; cursor < end && listIndex < _batchListCount; ++listIndex )
                    {
                        const TickBatchList& batchList = _pBatchList[listIndex];
                        const uint32         rangeEnd  = MathUtil::min( end, _pBatchStart[listIndex + 1] );
                        if ( cursor < rangeEnd )
                            batchList._pFunction( scope._threadState, batchList._listElement.data() + ( cursor - _pBatchStart[listIndex] ), rangeEnd - cursor,
                                                  batchList._subTickID, _deltaTime );
                        cursor = MathUtil::max( cursor, rangeEnd );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SceneTickScheduler::EntryTask::run()
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.taskEntry" );
        const SceneTickSchedulerInternal::TickThreadScope scope{ *_pHierarchy, _dispatchOrdinal };
        SceneTickSchedulerInternal::tickEntry( scope._threadState, _deltaTime, *_pEntry );
    }

    void SceneTickScheduler::StageItemTask::run()
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.stageTask" );
        const SceneTickSchedulerInternal::TickThreadScope scope{ *_pHierarchy, _dispatchOrdinal };
        const TickBatchList&                              batchList = *_pBatchList;
        if ( batchList._listElement.empty() == false )
            batchList._pFunction( scope._threadState, batchList._listElement.data(), static_cast<uint32>( batchList._listElement.size() ), batchList._subTickID, _deltaTime );
    }

    SceneTickScheduler::SceneTickScheduler( GameObjectManager& manager, SceneTransformHierarchy& hierarchy, PrimitiveRegistry& primitiveRegistry,
                                            StructuralChangeBuffer& structuralChangeBuffer )
        : _pManager{ &manager }
        , _pHierarchy{ &hierarchy }
        , _pPrimitiveRegistry{ &primitiveRegistry }
        , _pStructuralChangeBuffer{ &structuralChangeBuffer }
        , _tickRegistry{}
        , _listBatchStart{}
        , _listEntryTask{}
        , _listStageItemTask{}
        , _stageTransformApplyCount{ 0 }
        , _dispatchOrdinal{ 0 }
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
            // **씬 틱은 자기가 낸 일만 기다린다.** 틱의 병렬 일은 `runParallel` 이 합류까지, Task 레인은 자기 스테이지(`waitStage`)를 기다린다.
            // 주의: `TaskManager::waitAll()` 은 엔진 **전체의** 태스크 — 렌더 스레드의 패스 기록, 에셋 스트리밍, 비동기 씬 로드, 오디오 재생 — 가 빌 때까지
            // 게임 스레드를 세운다. onTick 이 낸 태스크가 틱 뒤 단계 전에 끝나야 하면 자기 스테이지를 만들어 `waitStage` 로 기다린다.
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
            (void)_tickRegistry.refresh( *_pManager ); // 다시 지은 횟수는 레지스트리의 계측(`GT.Scene.tick.stageRebuilds`)이 센다
        }

        // 그룹마다: Task 레인 태스크를 띄움 → 파도(Batch 레인)를 키 순으로, 파도마다 한 번의 포크-조인 → Task 레인을 기다림 → Main 레인(게임 스레드)
        // → 그 그룹의 선행 조건 스테이지. 오브젝트 안 순서는 파도 장벽(Batch) · 한 태스크(Task) · 게임 스레드(Main)가 지키고, 스테이지는 앞 레인이 모두
        // 끝난 뒤라 선행 조건이 어느 레인의 항목이어도 순서가 맞다. 일괄 경로를 끄면 모든 오브젝트가 칸 목록 한 번의 포크-조인이다(기준 길).
        const vector<TickStage>& listStage  = _tickRegistry.getStages();
        size_t                   stageIndex = 0;
        // 그룹마다 구간 하나(레인 + 그 그룹의 스테이지) — 두 단계가 같은 `GT.Scene.tick.components` 에 섞여 PrePhysics 와 PostPhysics 가 갈리지 않았다.
        static_assert( TickRegistry::kGroupCount == 4, "tick group scope names follow TickGroup" );
        static const ProfileScopeID kArrGroupScopeID[TickRegistry::kGroupCount] = {
            FrameProfiler::registerEngineScopeSite( "GT.Scene.tick.group.PrePhysics", __func__, __FILE__, __LINE__ ),
            FrameProfiler::registerEngineScopeSite( "GT.Scene.tick.group.DuringPhysics", __func__, __FILE__, __LINE__ ),
            FrameProfiler::registerEngineScopeSite( "GT.Scene.tick.group.PostPhysics", __func__, __FILE__, __LINE__ ),
            FrameProfiler::registerEngineScopeSite( "GT.Scene.tick.group.PostUpdate", __func__, __FILE__, __LINE__ ),
        };
        for ( uint32 group = firstGroup; group < endGroup && group < TickRegistry::kGroupCount; ++group )
        {
            const ScopedFrameProfile groupScope{ kArrGroupScopeID[group] };
            tickLanes( deltaTime, group );

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
                // 앞에서 돈 선행 조건(앞 스테이지 · 레인)이 쓴 트랜스폼을 이 스테이지가 같은 프레임에 읽게 한다. 기다리는 스테이지 앞에서만.
                if ( stage._bApplyBefore == SW_TRUE )
                    applyStageTransforms();
                // 순서만 간선이 있으면 항목마다 태스크(간선 = runAfter), 없으면 파도와 같은 잡이다 — 이웃한 같은 (타입 함수, 서브틱 id) 항목은 함수 한 번에 넘어간다
                // (`TickRegistry::refreshStageBatches`).
                SW_PROFILE_COUNT( "GT.Scene.tick.stageLists", static_cast<uint32>( stage._listBatch.size() ) ); // 스테이지의 함수 호출 단위 수 — 항목 수보다 작을수록 잘 묶였다
                if ( stage._listEdge.empty() == false )
                    dispatchStageGraph( stage, deltaTime );
                else
                    (void)dispatchBatchLists( stage._listBatch, deltaTime );
            }
        }
    }

    void SceneTickScheduler::tickLanes( float32 deltaTime, uint32 group )
    {
        const vector<TickObjectEntry>& listTask  = _tickRegistry.getEntries( group );
        const vector<TickObjectEntry>& listMain  = _tickRegistry.getMainEntries( group );
        const vector<TickWave>&        listWave  = _tickRegistry.getWaves( group );
        const uint32                   taskCount = static_cast<uint32>( listTask.size() );

        // 일괄 경로를 끈 기준 길은 칸 목록 한 번의 포크-조인이다(예전 길 그대로 — 동등성 테스트가 이것과 파도 길을 맞댄다).
        if ( _tickRegistry.isBatchPathEnabled() == false )
        {
            if ( taskCount == 0 )
                return;
            SW_PROFILE_COUNT( "GT.Scene.tick.objectEntries", taskCount );
            SceneTickSchedulerInternal::EntryTick job{};
            job._pEntry          = listTask.data();
            job._pHierarchy      = _pHierarchy;
            job._dispatchOrdinal = ++_dispatchOrdinal;
            job._deltaTime       = deltaTime;
            SceneTickSchedulerInternal::dispatch( taskCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &SceneTickSchedulerInternal::EntryTick::tickRange, &job ) );
            return;
        }

        // Task 레인: 오브젝트마다 태스크 하나를 먼저 띄운다 — 파도 장벽에 묶이지 않고 파도와 겹쳐 돈다. 작업 풀이 없으면(엔진 없는 테스트) 파도 뒤에 차례로 돈다.
        const bool      bAsyncTask  = taskCount > 0 && engine::areEngineServicesBound() && engine::getSubsystem<TaskManager>().getWorkerCount() > 0;
        const uint32    taskOrdinal = ++_dispatchOrdinal;
        TaskStageHandle taskStage{};
        if ( bAsyncTask )
        {
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            _listEntryTask.clear();
            _listEntryTask.resize( taskCount ); // 태스크가 원소 주소를 들므로 띄우기 전에 크기를 정한다
            taskStage = taskManager.createStage();
            for ( uint32 index = 0; index < taskCount; ++index )
            {
                EntryTask& task       = _listEntryTask[index];
                task._pEntry          = &listTask[index];
                task._pHierarchy      = _pHierarchy;
                task._dispatchOrdinal = taskOrdinal;
                task._deltaTime       = deltaTime;
                TaskHandle handle     = taskManager.emplaceTask( "GT.Scene.tick.task", SW_DELEGATE_METHOD( TaskDelegate, &EntryTask::run, &task ) );
                taskStage.addTask( handle );
                handle.submit();
            }
        }

        // Batch 레인: 파도 키 순으로 파도마다 한 번의 포크-조인 — 사이가 장벽이라 한 오브젝트의 앞 파도 항목이 다음 파도 항목보다 먼저다.
        uint32 elementCount = 0;
        for ( const TickWave& wave : listWave )
        {
            const uint32 waveCount = dispatchBatchLists( wave._listBatch, deltaTime );
            if ( waveCount == 0 )
                continue;
            elementCount += waveCount;
            SW_PROFILE_COUNT( "GT.Scene.tick.waves", 1 ); // 프레임당 장벽 수 — 규모와 무관한 구조 지표
        }
        SW_PROFILE_COUNT( "GT.Scene.tick.batchElements", elementCount );

        if ( bAsyncTask )
        {
            engine::getSubsystem<TaskManager>().waitStage( taskStage, "SceneTickScheduler" );
        }
        else if ( taskCount > 0 )
        {
            SceneTickSchedulerInternal::EntryTick job{};
            job._pEntry          = listTask.data();
            job._pHierarchy      = _pHierarchy;
            job._dispatchOrdinal = taskOrdinal;
            job._deltaTime       = deltaTime;
            job.tickRange( 0, taskCount );
        }
        SW_PROFILE_COUNT( "GT.Scene.tick.taskEntries", taskCount );

        // Main 레인: 게임 스레드에서 차례로 — 메인 전용 API 를 만지는 틱이 워커로 새지 않는다.
        const uint32 mainCount = static_cast<uint32>( listMain.size() );
        if ( mainCount > 0 )
        {
            SW_PROFILE_COUNT( "GT.Scene.tick.mainEntries", mainCount );
            SceneTickSchedulerInternal::EntryTick job{};
            job._pEntry          = listMain.data();
            job._pHierarchy      = _pHierarchy;
            job._dispatchOrdinal = ++_dispatchOrdinal;
            job._deltaTime       = deltaTime;
            job.tickRange( 0, mainCount );
        }
    }

    uint32 SceneTickScheduler::dispatchBatchLists( const vector<TickBatchList>& listBatch, float32 deltaTime )
    {
        uint32 totalCount = 0;
        _listBatchStart.clear();
        for ( const TickBatchList& batchList : listBatch )
        {
            _listBatchStart.push_back( totalCount );
            totalCount += static_cast<uint32>( batchList._listElement.size() );
        }
        _listBatchStart.push_back( totalCount );
        if ( totalCount == 0 )
            return 0;
        SceneTickSchedulerInternal::BatchListTick job{};
        job._pBatchList      = listBatch.data();
        job._pBatchStart     = _listBatchStart.data();
        job._pHierarchy      = _pHierarchy;
        job._batchListCount  = static_cast<uint32>( listBatch.size() );
        job._dispatchOrdinal = ++_dispatchOrdinal;
        job._deltaTime       = deltaTime;
        SceneTickSchedulerInternal::dispatch( totalCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &SceneTickSchedulerInternal::BatchListTick::tickRange, &job ) );
        return totalCount;
    }

    void SceneTickScheduler::dispatchStageGraph( const TickStage& stage, float32 deltaTime )
    {
        SW_PROFILE_SCOPE( "GT.Scene.tick.stageGraph" );
        const uint32 itemCount = static_cast<uint32>( stage._listBatch.size() );
        SW_ASSERT( itemCount == stage._listItem.size() ); // 간선 있는 스테이지는 항목마다 목록 하나다(`TickRegistry::refreshStageBatches`)

        // 위상 순서(Kahn). 간선은 레지스트리가 DAG 에서 지었고 편집은 빼거나 번호만 민다 — 순환이 생길 수 없다.
        vector<uint32> listOffset( itemCount + 1, 0 );
        for ( const TickStageEdge& edge : stage._listEdge )
        {
            ++listOffset[edge._source + 1];
        }
        for ( uint32 index = 0; index < itemCount; ++index )
        {
            listOffset[index + 1] += listOffset[index];
        }
        vector<uint32> listTarget( stage._listEdge.size(), 0 );
        vector<uint32> listCursor( listOffset.begin(), listOffset.end() - 1 );
        vector<uint32> listInDegree( itemCount, 0 );
        for ( const TickStageEdge& edge : stage._listEdge )
        {
            listTarget[listCursor[edge._source]++] = edge._target;
            ++listInDegree[edge._target];
        }
        vector<uint32> listOrder;
        listOrder.reserve( itemCount );
        for ( uint32 index = 0; index < itemCount; ++index )
        {
            if ( listInDegree[index] == 0 )
                listOrder.push_back( index );
        }
        for ( size_t cursor = 0; cursor < listOrder.size(); ++cursor )
        {
            const uint32 current = listOrder[cursor];
            for ( uint32 edge = listOffset[current]; edge < listOffset[current + 1]; ++edge )
            {
                if ( --listInDegree[listTarget[edge]] == 0 )
                    listOrder.push_back( listTarget[edge] );
            }
        }
        SW_ASSERT( listOrder.size() == itemCount );

        const uint32 firstOrdinal = _dispatchOrdinal + 1;
        _dispatchOrdinal += itemCount;
        _listStageItemTask.clear();
        _listStageItemTask.resize( itemCount ); // 태스크가 원소 주소를 들므로 띄우기 전에 크기를 정한다
        for ( uint32 rank = 0; rank < itemCount; ++rank )
        {
            StageItemTask& task   = _listStageItemTask[listOrder[rank]];
            task._pBatchList      = &stage._listBatch[listOrder[rank]];
            task._pHierarchy      = _pHierarchy;
            task._dispatchOrdinal = firstOrdinal + rank;
            task._deltaTime       = deltaTime;
        }

        // 작업 풀이 없으면(엔진 없는 테스트) 위상 순으로 이 스레드가 돈다.
        const bool bAsync = engine::areEngineServicesBound() && engine::getSubsystem<TaskManager>().getWorkerCount() > 0;
        if ( bAsync == false )
        {
            for ( const uint32 index : listOrder )
            {
                _listStageItemTask[index].run();
            }
            return;
        }

        TaskManager&       taskManager = engine::getSubsystem<TaskManager>();
        TaskStageHandle    taskStage   = taskManager.createStage();
        vector<TaskHandle> listHandle( itemCount );
        for ( uint32 index = 0; index < itemCount; ++index )
        {
            listHandle[index] = taskManager.emplaceTask( "GT.Scene.tick.stageTask", SW_DELEGATE_METHOD( TaskDelegate, &StageItemTask::run, &_listStageItemTask[index] ) );
            taskStage.addTask( listHandle[index] );
        }
        for ( const TickStageEdge& edge : stage._listEdge )
        {
            // 레지스트리가 DAG 로 지었으니 거절(순환)은 결함이다 — 걸지 못한 간선은 순서가 깨진다.
            const bool bLinked = listHandle[edge._target].runAfter( listHandle[edge._source] );
            SW_ASSERT( bLinked );
            (void)bLinked;
        }
        for ( TaskHandle& handle : listHandle )
        {
            handle.submit();
        }
        taskManager.waitStage( taskStage, "SceneTickScheduler" );
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
