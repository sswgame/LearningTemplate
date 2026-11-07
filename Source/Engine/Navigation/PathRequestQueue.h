/**
 * @file PathRequestQueue.h
 * @brief 비동기 경로 요청 줄입니다 — 요청(아무 스레드) → 줄 → 워커(`TaskManager` 스테이지) → 정해진 자리의 수확(`complete`) → 요청자가 받기. 핸들은 칸 번호 + 세대입니다.
 * @details 내비메시 경로(`SceneNavigation::requestPath`)와 격자 A*(GameFramework `GridPathSolver` — RTS 유닛 경로)가 함께 쓰는 한 벌입니다. 바뀌는 것(무엇을 어떻게 구하는가)은
 *          풀이 타입 `TSolver` 하나가 갖습니다:
 *          - `using Job = …;` — 질의와 결과를 함께 담는 값(기본 생성 · 이동 가능). 요청자가 질의 칸을 채워 넘기고, 받을 때 결과 칸을 읽는다.
 *          - `void prepareBatch( vector<Job>& inoutListJob, uint32 laneCount )` — 게임 스레드, 워커에 넘기기 직전. 질의가 가리키는 자원을 풀고(에이전트 종류 → 내비메시) 레인마다의 스크래치를 마련한다.
 *          - `void solve( Job& inoutJob, uint32 laneIndex )` — 워커. 자기 일과 자기 레인의 스크래치만 만진다(레인 하나는 태스크 하나라 레인 스크래치를 둘이 함께 쓰지 않는다).
 *
 *          **언제 무엇이 도는가**(형식이 정한다):
 *          - `request` · `cancel` · `takeResult` 는 아무 스레드에서 부릅니다(병렬 틱). 칸 표 · 대기 줄만 잠금 아래 만집니다.
 *          - `launch` 는 대기 줄을 비행 줄로 옮겨 워커에 넘기고, `complete` 는 그 일을 기다려 결과를 칸에 옮깁니다 — 둘 다 주인 스레드 하나(게임 스레드)에서 부릅니다.
 *            워커는 비행 줄만 쓰고, 비행 줄은 `launch` 와 `complete` 사이에 아무도 크기를 바꾸지 않습니다(요청은 대기 줄로 간다).
 *          - `complete` 가 돌려주는 `Fence` 는 "지금 이 줄의 워커가 아무것도 읽지 않는다" 는 증표입니다. 질의가 읽는 데이터(내비메시 타일 · 격자)를 바꾸는 함수는
 *            이 증표(또는 그것으로 만든 증표)를 인자로 받아, 진행 중 질의와 겹쳐 바꾸는 길을 형식으로 막습니다.
 *
 *          결과는 언제나 다음 수확에서 Ready 가 됩니다 — 워커가 얼마나 빨랐는지와 상관없다(같은 입력 → 같은 프레임의 같은 결과, 결정적).
 *          취소 · 칸 재사용은 세대가 가립니다: 취소한 요청의 결과는 수확에서 버리고, 옛 핸들로는 새 요청의 결과를 받지 못합니다.
 *          언리얼 `UNavigationSystemV1::FindPathAsync`(배치 · `AbortAsyncFindPathRequest`) · 유니티 `NavMeshQuery` 잡(`JobHandle` 완료 뒤 결과)의 자리입니다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    /** @brief 경로 요청 하나의 핸들입니다. 칸 번호와 세대 — 받거나 취소하면 세대가 올라 옛 핸들은 아무것도 가리키지 않습니다. */
    struct PathRequestHandle
    {
        uint32 _slot{ constant::invalid_index::kUint32 };
        uint32 _generation{ 0 };

        bool isValid() const { return _slot != constant::invalid_index::kUint32; }
    };

    /** @brief 요청의 지금 상태입니다. */
    enum class PathRequestState : uint8
    {
        Unknown = 0, ///< 빈 핸들이거나 이미 받았거나 취소했다(줄을 비웠다)
        Pending,     ///< 줄에 섰거나 워커가 구하는 중이다 — 다음 수확(`complete`) 뒤에 `Ready`
        Ready,       ///< 결과가 있다. 받으면(`takeResult`) 핸들이 끝난다
    };
} // namespace sw

namespace sw
{
    /** @class PathRequestQueue @brief 파일 머리말 참고. @tparam TSolver 질의를 푸는 타입(머리말의 세 가지). 줄보다 오래 산다. */
    template <typename TSolver>
    class PathRequestQueue
    {
    public:
        using Job = typename TSolver::Job;

        /**
         * @class Fence
         * @brief "이 줄의 워커가 지금 아무것도 읽지 않는다" 는 증표입니다. 이 줄의 `complete` · `clear` 만 만듭니다(줄의 종류마다 다른 타입 — 격자 줄의 증표로 내비메시를 바꾸지 못한다).
         * @details 질의가 읽는 데이터를 바꾸는 함수가 이 증표를 받으면, 수확(기다림)을 건너뛰고 바꾸는 코드는 컴파일되지 않습니다 — 주석 · 단언이 아니라 형식으로 지키는 순서입니다.
         */
        class Fence
        {
            friend class PathRequestQueue;

        private:
            Fence() {} // NOLINT(modernize-use-equals-default): 사용자 제공 생성자라야 C++17 집합체가 아니다 — 집합체면 아무나 `{}` 로 만든다
        };

        explicit PathRequestQueue( TSolver& solver )
            : _listSlot{}
            , _listFreeSlot{}
            , _listPending{}
            , _listInFlightJob{}
            , _listInFlightHandle{}
            , _listLane{}
            , _stage{}
            , _pSolver{ &solver }
            , _completedCount{ 0 }
            , _lock{}
            , _bLaunchedTasks{ SW_FALSE }
        {
        }

        ~PathRequestQueue()
        {
            // 워커가 비행 줄 · 풀이를 읽고 있다 — 끝날 때까지 놓지 않는다.
            waitLaunchedTasks();
        }

        PathRequestQueue( const PathRequestQueue& )            = delete;
        PathRequestQueue& operator=( const PathRequestQueue& ) = delete;

        /** @brief 질의 칸을 채운 @p job 을 줄에 세우고 핸들을 돌려줍니다(아무 스레드). 다음 `launch` 가 워커에 넘기고 그다음 `complete` 가 결과를 칸에 둡니다. */
        [[nodiscard]] PathRequestHandle request( Job job )
        {
            ScopedLock<SpinLock> lock{ _lock };
            uint32               slotIndex = 0;
            if ( _listFreeSlot.empty() == false )
            {
                slotIndex = _listFreeSlot.back();
                _listFreeSlot.pop_back();
            }
            else
            {
                slotIndex = static_cast<uint32>( _listSlot.size() );
                _listSlot.push_back( Slot{} );
            }
            Slot& slot  = _listSlot[slotIndex];
            slot._job   = std::move( job );
            slot._state = PathRequestState::Pending;
            const PathRequestHandle handle{ slotIndex, slot._generation };
            _listPending.push_back( handle );
            return handle;
        }

        /** @brief 요청을 버립니다(아무 스레드). 워커가 구하는 중이면 그 결과는 수확에서 버린다. 끝난 · 모르는 핸들이면 할 일이 없습니다. */
        void cancel( const PathRequestHandle& handle )
        {
            ScopedLock<SpinLock> lock{ _lock };
            if ( isLiveUnlocked( handle ) )
                freeSlotUnlocked( handle._slot );
        }

        /** @brief 요청의 상태입니다(아무 스레드). */
        PathRequestState getState( const PathRequestHandle& handle ) const
        {
            ScopedLock<SpinLock> lock{ _lock };
            return isLiveUnlocked( handle ) ? _listSlot[handle._slot]._state : PathRequestState::Unknown;
        }

        /**
         * @brief 결과가 있으면 @p outJob 으로 옮기고 핸들을 끝냅니다(`Ready`). 아직이면 `Pending`, 끝난 · 취소한 핸들이면 `Unknown` 이고 @p outJob 은 그대로입니다(아무 스레드).
         */
        [[nodiscard]] PathRequestState takeResult( const PathRequestHandle& handle, Job& outJob )
        {
            ScopedLock<SpinLock> lock{ _lock };
            if ( isLiveUnlocked( handle ) == false )
                return PathRequestState::Unknown;
            Slot& slot = _listSlot[handle._slot];
            if ( slot._state != PathRequestState::Ready )
                return slot._state;
            outJob = std::move( slot._job );
            freeSlotUnlocked( handle._slot );
            return PathRequestState::Ready;
        }

        /**
         * @brief 대기 줄을 워커에 넘깁니다(주인 스레드). 앞서 넘긴 일이 수확되지 않았으면 먼저 수확합니다 — 비행 줄은 한 벌이다.
         * @details 워커가 없으면(테스트 · 도구) 이 자리에서 모두 풉니다. 결과는 그래도 다음 `complete` 에서 칸에 든다(엔진 유무와 같은 순서).
         */
        void launch( string_view taskName )
        {
            if ( _listInFlightJob.empty() == false )
                (void)complete(); // 수확하지 않고 다시 넘기는 자리 — 결과는 칸에 들고 증표는 여기서 쓸 데가 없다
            {
                ScopedLock<SpinLock> lock{ _lock };
                for ( const PathRequestHandle& handle : _listPending )
                {
                    // 줄에 선 뒤 취소했으면 칸의 세대가 올랐다 — 넘기지 않는다.
                    if ( isLiveUnlocked( handle ) == false )
                        continue;
                    _listInFlightJob.push_back( std::move( _listSlot[handle._slot]._job ) );
                    _listInFlightHandle.push_back( handle );
                }
                _listPending.clear();
            }
            const uint32 jobCount = static_cast<uint32>( _listInFlightJob.size() );
            if ( jobCount == 0 )
                return;
            const bool   bWorkers  = engine::areEngineServicesBound();
            const uint32 laneLimit = bWorkers ? engine::getSubsystem<TaskManager>().getWorkerCount() + 1 : 1;
            const uint32 laneCount = MathUtil::max( 1u, MathUtil::min( jobCount, laneLimit ) );
            _pSolver->prepareBatch( _listInFlightJob, laneCount );
            _listLane.resize( laneCount );
            // 레인마다 이어진 구간 — 일이 서로 독립이라 나누는 모양은 결과에 영향이 없다.
            for ( uint32 laneIndex = 0; laneIndex < laneCount; ++laneIndex )
            {
                LaneTask& lane  = _listLane[laneIndex];
                lane._pQueue    = this;
                lane._laneIndex = laneIndex;
                lane._start     = static_cast<uint32>( ( static_cast<uint64>( jobCount ) * laneIndex ) / laneCount );
                lane._end       = static_cast<uint32>( ( static_cast<uint64>( jobCount ) * ( laneIndex + 1 ) ) / laneCount );
            }
            if ( bWorkers == false )
            {
                for ( LaneTask& lane : _listLane )
                {
                    lane.run();
                }
                return;
            }
            TaskManager& taskManager = engine::getSubsystem<TaskManager>();
            for ( LaneTask& lane : _listLane )
            {
                TaskHandle task = taskManager.emplaceTask( taskName, SW_DELEGATE_METHOD( TaskDelegate, &LaneTask::run, &lane ) );
                taskManager.addToStage( _stage, task, taskName );
                taskManager.submitWithoutWake( task );
            }
            taskManager.wakeSleepingWorkers( laneCount );
            _bLaunchedTasks = SW_TRUE;
        }

        /** @brief 넘긴 일을 기다리고(주인 스레드) 결과를 칸에 옮깁니다. 취소한 요청의 결과는 버립니다. 돌려준 증표가 "워커가 읽지 않는다" 의 증거입니다. */
        [[nodiscard]] Fence complete()
        {
            waitLaunchedTasks();
            if ( _listInFlightJob.empty() == false )
            {
                ScopedLock<SpinLock> lock{ _lock };
                for ( size_t jobIndex = 0; jobIndex < _listInFlightJob.size(); ++jobIndex )
                {
                    const PathRequestHandle& handle = _listInFlightHandle[jobIndex];
                    if ( isLiveUnlocked( handle ) == false )
                        continue;
                    Slot& slot  = _listSlot[handle._slot];
                    slot._job   = std::move( _listInFlightJob[jobIndex] );
                    slot._state = PathRequestState::Ready;
                    ++_completedCount;
                }
            }
            _listInFlightJob.clear();
            _listInFlightHandle.clear();
            return Fence{};
        }

        /** @brief 넘긴 일을 기다리고 모든 요청을 버립니다(주인 스레드 — 씬을 비울 때). 옛 핸들은 모두 `Unknown` 이 됩니다. */
        [[nodiscard]] Fence clear()
        {
            (void)complete(); // 아래에서 결과를 모두 버린다 — 증표는 이 함수가 새로 낸다
            ScopedLock<SpinLock> lock{ _lock };
            for ( uint32 slotIndex = 0; slotIndex < static_cast<uint32>( _listSlot.size() ); ++slotIndex )
            {
                if ( _listSlot[slotIndex]._state != PathRequestState::Unknown )
                    freeSlotUnlocked( slotIndex );
            }
            _listPending.clear();
            return Fence{};
        }

        /** @brief 줄에 선 · 구하는 중인 요청 수입니다(진단 · 감시 값). 비행 줄을 읽으므로 주인 스레드에서 부릅니다. */
        uint32 getPendingCount() const
        {
            ScopedLock<SpinLock> lock{ _lock };
            return static_cast<uint32>( _listPending.size() + _listInFlightHandle.size() );
        }
        /** @brief 수확한 결과 수(누적, 진단)입니다. 취소해 버린 것은 세지 않습니다. */
        uint64 getCompletedCount() const
        {
            ScopedLock<SpinLock> lock{ _lock };
            return _completedCount;
        }

    private:
        /** @brief 칸 하나 — 요청의 질의 · 결과와 상태. 세대는 칸을 놓을 때 오른다. */
        struct Slot
        {
            Job              _job{};
            uint32           _generation{ 0 };
            PathRequestState _state{ PathRequestState::Unknown };
        };

        /** @brief 워커 태스크 하나가 맡는 비행 줄의 이어진 구간입니다. 비행 줄 · 풀이는 `launch` ~ `complete` 사이에 바뀌지 않는다. */
        struct LaneTask
        {
            PathRequestQueue* _pQueue{ nullptr };
            uint32            _laneIndex{ 0 };
            uint32            _start{ 0 };
            uint32            _end{ 0 };

            void run()
            {
                for ( uint32 jobIndex = _start; jobIndex < _end; ++jobIndex )
                {
                    _pQueue->_pSolver->solve( _pQueue->_listInFlightJob[jobIndex], _laneIndex );
                }
            }
        };

        bool isLiveUnlocked( const PathRequestHandle& handle ) const SW_REQUIRES_LOCK( _lock )
        {
            return handle.isValid() && handle._slot < _listSlot.size() && _listSlot[handle._slot]._generation == handle._generation &&
                   _listSlot[handle._slot]._state != PathRequestState::Unknown;
        }

        void freeSlotUnlocked( uint32 slotIndex ) SW_REQUIRES_LOCK( _lock )
        {
            Slot& slot  = _listSlot[slotIndex];
            slot._job   = Job{};
            slot._state = PathRequestState::Unknown;
            ++slot._generation;
            _listFreeSlot.push_back( slotIndex );
        }

        /** @brief 워커에 넘긴 태스크가 있으면 모두 끝날 때까지 기다립니다(인라인으로 푼 묶음은 기다릴 것이 없다). */
        void waitLaunchedTasks()
        {
            if ( _bLaunchedTasks == SW_FALSE )
                return;
            engine::getSubsystem<TaskManager>().waitStage( _stage, "PathRequestQueue" );
            _bLaunchedTasks = SW_FALSE;
        }

    private:
        vector<Slot> _listSlot                 SW_GUARDED_BY( _lock );
        vector<uint32> _listFreeSlot           SW_GUARDED_BY( _lock );
        vector<PathRequestHandle> _listPending SW_GUARDED_BY( _lock ); ///< 요청 순서 — 워커에 넘기는 순서다
        vector<Job>                            _listInFlightJob;       ///< 주인 스레드가 `launch` 에서 채우고 `complete` 에서 비운다. 그 사이 워커만 원소를 쓴다
        vector<PathRequestHandle>              _listInFlightHandle;    ///< `_listInFlightJob` 과 같은 순서의 핸들
        vector<LaneTask>                       _listLane;
        TaskStageHandle                        _stage; ///< 이 줄이 낸 태스크 전부(주인 하나가 묶는다 — `NavMeshRuntime::_bakeStage` 와 같은 모양)
        TSolver*                               _pSolver;
        uint64 _completedCount                 SW_GUARDED_BY( _lock );
        mutable SpinLock                       _lock;
        uint8                                  _bLaunchedTasks : 1;
    };
} // namespace sw
