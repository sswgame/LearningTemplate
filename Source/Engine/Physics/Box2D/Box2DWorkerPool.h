/**
 * @file Box2DWorkerPool.h
 * @brief Box2D 월드 하나에 붙는 전용 워커 스레드 묶음입니다(`b2WorldDef::enqueueTask` · `finishTask`). Box2D 헤더를 include 하므로 `Physics/Box2D` 밖에서는 보지 않습니다.
 * @details 바디가 수천인 2D 장면에서 Box2D 의 스텝(충돌, 솔버 스테이지, 바디 마무리)을 여러 스레드로 나눈다. 유니티의 Physics2D 다중 스레드 옵션
 *          (`Physics2D.simulationMode` · job options), Box2D 예제의 enkiTS 연결과 같은 자리다. 워커 수는 `PhysicsSettings::_workerCount2D` 다.
 *
 *          **왜 엔진 공유 작업 풀이 아닌가.** Box2D 의 솔버는 워커 수만큼 태스크를 한꺼번에 넣고, 0 번이 아닌 태스크는 0 번이 스테이지를 열 때까지
 *          바쁘게 기다린다. 공유 풀의 스레드가 다른 일(게임 틱 잡)로 막히거나, 기다리는 태스크들이 스레드를 다 차지하면 0 번이 돌 자리가 없어 교착한다.
 *          그래서 Box2D 일만 하는 스레드를 따로 둔다.
 *
 *          **스레드 소유 규칙.**
 *          - 월드는 스텝을 부르는 스레드(게임 스레드) 하나가 가진다. 워커는 `b2World_Step` 안에서 Box2D 가 넣은 태스크 조각만 실행하고, 엔진 코드나
 *            씬 함수를 부르지 않는다. 스텝 밖에서는 잠들어 있다.
 *          - 스텝을 부른 스레드는 일꾼 번호 0 이고, `finishTask` 에서 기다리는 동안 대기열의 조각을 직접 실행한다. 전용 스레드는 1 .. 워커 수 - 1 이다.
 *            한 번에 도는 콜백끼리 일꾼 번호가 겹치지 않는다(Box2D 가 번호로 스레드별 버퍼를 고른다).
 *          - 대기열은 먼저 넣은 순서(FIFO)다. 솔버의 0 번 태스크는 나머지보다 먼저 들어가므로 먼저 꺼내지고, 0 번은 다른 태스크 없이도 모든 블록을
 *            끝낼 수 있어(블록은 원자적으로 가져간다) 기다리는 태스크가 스레드를 차지해도 진행한다.
 *
 *          **잠금 순서.** `_mutex` 는 잎 잠금이다. 대기열을 고치는 동안만 쥐고, 쥔 채로 Box2D 콜백 · 할당 · 다른 잠금을 부르지 않는다. 씬과 엔진의 어떤
 *          잠금도 이 잠금 안에서 잡히지 않는다.
 *
 *          **결정성.** Box2D v3 는 워커 수와 관계없이 같은 결과를 낸다(일을 나누는 단위가 스레드가 아니라 고정 블록이고, 결과를 쓰는 자리가 블록마다
 *          정해져 있다). `PhysicsScene2DTest.StepIsDeterministicAcrossWorkerCounts` 가 1 과 4 의 결과를 비트까지 비교한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include <box2d/types.h>

namespace sw
{
    /** @class Box2DWorkerPool @brief Box2D 태스크만 실행하는 전용 워커들입니다. 파일 머리말 참고. */
    class Box2DWorkerPool
    {
    public:
        /** @brief 일꾼 수 @p workerCount(스텝 스레드 포함, 2 이상)로 만듭니다. 전용 스레드는 하나 적게 띄웁니다. */
        explicit Box2DWorkerPool( uint32 workerCount );
        ~Box2DWorkerPool();

        Box2DWorkerPool( const Box2DWorkerPool& )            = delete;
        Box2DWorkerPool& operator=( const Box2DWorkerPool& ) = delete;

        /** @brief 일꾼 수(스텝 스레드 포함)입니다. `b2WorldDef::workerCount` 에 넣습니다. */
        uint32 getWorkerCount() const { return _workerCount; }

        /** @brief `b2WorldDef::enqueueTask` 입니다. @p pUserContext 는 이 풀입니다. */
        static void* enqueueTask( b2TaskCallback* pTask, int32 itemCount, int32 minRange, void* pTaskContext, void* pUserContext );
        /** @brief `b2WorldDef::finishTask` 입니다. 그 태스크의 조각이 모두 끝날 때까지 대기열을 함께 비웁니다. */
        static void finishTask( void* pUserTask, void* pUserContext );

    private:
        /** @brief Box2D 태스크 하나입니다. 스텝 스레드만 만들고 되돌리므로 목록은 잠그지 않습니다(`_stepThread` 역할 — 선언의 `SW_GUARDED_BY`). */
        struct Task
        {
            b2TaskCallback* _pCallback{ nullptr };
            void*           _pContext{ nullptr };
            atomic<uint32>  _remainingRangeCount{ 0 };
        };

        /** @brief 태스크의 한 조각([시작, 끝))입니다. */
        struct Range
        {
            Task* _pTask{ nullptr };
            int32 _startIndex{ 0 };
            int32 _endIndex{ 0 };
        };

        Task* enqueue( b2TaskCallback* pTask, int32 itemCount, int32 minRange, void* pTaskContext ) SW_REQUIRES_LOCK( _stepThread );
        void  finish( Task* pTask ) SW_REQUIRES_LOCK( _stepThread );
        /** @brief 대기열의 조각 하나를 꺼내 @p workerIndex 로 실행합니다. 꺼낼 것이 없으면 false 입니다. */
        [[nodiscard]] bool tryRunOne( uint32 workerIndex );
        void               runWorker( uint32 workerIndex, ThreadStopToken stopToken );

        vector<sw::thread>                 _listThread;
        ThreadRole                         _stepThread;                  ///< 스텝 스레드(월드를 가진 쪽) 역할 — 잠금이 아니라 분석용 능력이다
        vector<unique_ptr<Task>> _listTask SW_GUARDED_BY( _stepThread ); ///< 만든 태스크 전부(소유)
        vector<Task*> _listFreeTask        SW_GUARDED_BY( _stepThread ); ///< 다시 쓸 태스크
        vector<Range> _listRange           SW_GUARDED_BY( _mutex );      ///< 대기열.
        mutex                              _mutex;
        atomic<uint32>                     _wakeWord;               ///< 넣을 때마다 올리는 세대 — 워커가 이 워드에서 잠든다(`Futex`).
        uint32 _nextRangeIndex             SW_GUARDED_BY( _mutex ); ///< `_listRange` 에서 다음에 꺼낼 칸.
        uint32                             _workerCount;
    };
} // namespace sw
