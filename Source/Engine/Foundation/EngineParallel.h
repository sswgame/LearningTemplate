/**
 * @file EngineParallel.h
 * @brief 엔진 코드가 쓰는 포크-조인 병렬 도우미입니다. 서비스가 연결돼 있으면 잡으로, 아니면 현재 스레드에서 돌립니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    namespace engine
    {
        /**
         * @brief [0, count) 를 잡으로 나눠 돌리고 끝날 때까지 기다립니다 (상용 엔진의 ParallelFor).
         * @details 병렬 시스템 하나의 모양은 "상태를 가진 시스템 타입 + 이 호출 + 매니저 단계 한 줄" 입니다(트랜스폼 플러시 ·
         *          씬 수집 · 제자리 갱신). 스테이지를 만들고 기다리는 코드를 시스템마다 다시 쓰지 마십시오. 서비스가 아직
         *          연결되지 않은 곳(테스트 초기 · 도구)에서는 현재 스레드가 한 번에 돕니다. 임계값보다 작은 일도 마찬가지입니다.
         *          디스패치 최소 비용(잠든 풀 ~34 us, 워커가 깨어 있으면 ~7 us)보다 작은 일은 나누면
         *          오히려 느려집니다.
         * @param count           일의 수
         * @param serialThreshold 이 수 미만이면 나누지 않습니다
         * @param body            (start, end) 구간을 처리하는 본문. 워커는 컨테이너를 만지지 말고 포인터만 받으십시오.
         */
        inline void runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
        {
            if ( areEngineSubsystemsBound() )
            {
                getSubsystem<TaskManager>().runParallel( count, serialThreshold, body );
                return;
            }
            if ( count > 0 && body.isBound() )
                body( 0, count );
        }

        /**
         * @brief `runParallel` 과 같되 나눌지를 **측정한 비용**으로 정합니다 — 항목당 비용을 모를 때(대부분의 시스템) 이쪽을 쓰십시오.
         * @details 앞 항목을 호출 스레드가 직접 돌며 비용을 재고, 남은 일의 예상 직렬 시간이 디스패치 손익분기(16 us — `kSerialBudgetNanos`)보다 짧으면 이어서 직렬로 돕니다.
         *          무거운 항목은 첫 항목에서 바로 나뉩니다. 자세한 규칙은 `TaskManager::runParallelByCost`. 서비스가 연결되지 않은 곳에서는 현재 스레드가 한 번에 돕니다.
         */
        inline void runParallelByCost( uint32 count, const ParallelBlockDelegate& body )
        {
            if ( areEngineSubsystemsBound() )
            {
                getSubsystem<TaskManager>().runParallelByCost( count, body );
                return;
            }
            if ( count > 0 && body.isBound() )
                body( 0, count );
        }

        /**
         * @brief 항목 하나가 출력 칸 하나를 쓰는 병렬 for 입니다 — 본문은 `body( uint32 index, T& slot )` 이고 @p output 의 제 칸만 받습니다(`TaskManager::runParallelInto`).
         * @details 출력이 항목마다 한 칸이면 `runParallel` 에 `.data()` 포인터를 손으로 넘기지 말고 이쪽을 쓰십시오. 본문이 출력 컨테이너를 잡지 못하므로 경합 감지기에
         *          걸리는 "벡터를 람다로 잡기" 가 생기지 않습니다. 원소 타입을 적어 부르면(`runParallelInto<float32>( list, … )`) 컨테이너가 span 으로 바뀝니다.
         *          서비스가 연결되지 않은 곳에서는 현재 스레드가 한 번에 돕니다.
         */
        template <typename T, typename Body>
        void runParallelInto( span<T> output, uint32 serialThreshold, const Body& body )
        {
            using Job = TaskManager::ParallelIntoJob<T, Body>;
            SW_ASSERT( output.size() == static_cast<uint32>( output.size() ) );
            const Job job{ output.data(), &body };
            runParallel( static_cast<uint32>( output.size() ), serialThreshold, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Job::runRange, &job ) );
        }

        /** @brief `runParallelInto` 와 같되 나눌지를 측정한 비용으로 정합니다(`runParallelByCost`). 항목당 비용을 모르면(대부분의 시스템) 이쪽입니다. */
        template <typename T, typename Body>
        void runParallelIntoByCost( span<T> output, const Body& body )
        {
            using Job = TaskManager::ParallelIntoJob<T, Body>;
            SW_ASSERT( output.size() == static_cast<uint32>( output.size() ) );
            const Job job{ output.data(), &body };
            runParallelByCost( static_cast<uint32>( output.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &Job::runRange, &job ) );
        }

        /**
         * @brief 출력 칸이 여럿인 병렬 for 입니다 — 본문은 `body( start, end, span<T0>, span<T1>, … )` 이고 각 출력의 [start, end) 조각만 받습니다(`TaskManager::runParallelRangeInto`).
         * @details 항목 하나가 칸 여럿을 쓰거나 구간째 일괄 처리하는 곳에 씁니다. 출력은 길이가 @p count 이거나 비어야 합니다(빈 것은 없는 스트림). 서비스가 연결되지 않은 곳에서는 현재 스레드가 한 번에 돕니다.
         */
        template <typename Body, typename... Ts>
        void runParallelRangeInto( uint32 count, uint32 serialThreshold, const Body& body, span<Ts>... outputs )
        {
            using Job = TaskManager::ParallelRangeIntoJob<Body, Ts...>;
            SW_ASSERT( Job::hasItemCount( count, outputs... ) );
            const Job job{ std::tuple<span<Ts>...>{ outputs... }, &body };
            runParallel( count, serialThreshold, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Job::runRange, &job ) );
        }

        /** @brief `runParallelRangeInto` 와 같되 나눌지를 측정한 비용으로 정합니다(`runParallelByCost`). */
        template <typename Body, typename... Ts>
        void runParallelRangeIntoByCost( uint32 count, const Body& body, span<Ts>... outputs )
        {
            using Job = TaskManager::ParallelRangeIntoJob<Body, Ts...>;
            SW_ASSERT( Job::hasItemCount( count, outputs... ) );
            const Job job{ std::tuple<span<Ts>...>{ outputs... }, &body };
            runParallelByCost( count, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Job::runRange, &job ) );
        }

        /**
         * @brief 결정적 병렬 리듀스입니다 —블록마다 `body( index, T& partial )` 로 번호 순서로 접고, 부분값을 블록 순서로 `combine` 합니다(`TaskManager::runParallelReduce`).
         * @details 워커 수 · 스케줄 · 서비스 유무와 무관하게 같은 비트입니다. 원자 더하기나 스크래치 슬롯별 합처럼 도착 순서에 달린 합 대신 이쪽을 쓰십시오.
         */
        template <typename T, typename Body, typename Combine>
        [[nodiscard]] T runParallelReduce( uint32 count, uint32 blockSize, const T& identity, const Body& body, const Combine& combine )
        {
            using Job = TaskManager::ParallelReduceJob<T, Body>;
            SW_ASSERT( blockSize > 0 );
            if ( count == 0 )
                return identity;
            vector<T> listPartial( Job::computeBlockCount( count, blockSize ), identity );
            runParallelIntoByCost<T>( listPartial, Job{ count, blockSize, &body } );
            return Job::combineInOrder( listPartial, combine );
        }

        /**
         * @brief 병렬 본문이 스레드마다 하나씩 쓰는 스크래치 슬롯의 수입니다. 워커마다 하나, 기다리면서 돕는 워커 아닌 스레드
         *        몫 몇 개입니다.
         * @details 잡마다 스크래치를 만들면 프레임당 청크 수만큼 할당합니다. 슬롯 배열을 이 크기로 한 번 잡아 두고, 본문이
         *          `getParallelScratchSlot()` 으로 자기 칸을 찾습니다. 돕는 스레드가 메인 하나만이 아니라는 것(렌더 · 로더도
         *          기다리며 돕습니다)은 `TaskManager::getScratchSlotCount` 주석에 있습니다.
         */
        inline uint32 getParallelScratchSlotCount()
        {
            return areEngineSubsystemsBound() ? getSubsystem<TaskManager>().getScratchSlotCount() : 1;
        }

        /**
         * @brief 지금 스레드가 작업 풀의 워커이면 true 입니다(서비스가 없으면 false).
         * @details "메인 스레드 전용" 정적 칸(창 · 입력 · 에디터 UI 초기화)이 `SW_ASSERT( engine::isOnPoolWorker() == false )` 로 근거를 코드에 둔다.
         *          창 메시지는 OS 가 창을 만든 스레드에서만 주고, 엔진이 다른 스레드를 만드는 길은 작업 풀뿐이라 이 한 검사가 "동시 첫 호출 없음" 을 지킨다.
         */
        inline bool isOnPoolWorker()
        {
            return areEngineSubsystemsBound() && getSubsystem<TaskManager>().isWorkerThread();
        }

        /** @brief 지금 스레드의 스크래치 슬롯입니다. 워커면 그 번호, 아니면 그 스레드가 처음 물을 때 받은 고유한 도우미 칸입니다. */
        inline uint32 getParallelScratchSlot()
        {
            if ( areEngineSubsystemsBound() == false )
                return 0;
            return getSubsystem<TaskManager>().getCurrentThreadScratchSlot();
        }
    } // namespace engine
} // namespace sw
