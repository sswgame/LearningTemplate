/**
 * @file EngineParallel.h
 * @brief 엔진 코드가 쓰는 포크-조인 병렬 도우미입니다. 서비스가 연결돼 있으면 잡으로, 아니면 현재 스레드에서 돌립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"

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
            if ( areEngineServicesBound() )
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
            if ( areEngineServicesBound() )
            {
                getSubsystem<TaskManager>().runParallelByCost( count, body );
                return;
            }
            if ( count > 0 && body.isBound() )
                body( 0, count );
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
            return areEngineServicesBound() ? getSubsystem<TaskManager>().getScratchSlotCount() : 1;
        }

        /**
         * @brief 지금 스레드가 작업 풀의 워커이면 true 입니다(서비스가 없으면 false).
         * @details "메인 스레드 전용" 정적 칸(창 · 입력 · 에디터 UI 초기화)이 `SW_ASSERT( engine::isOnPoolWorker() == false )` 로 근거를 코드에 둔다.
         *          창 메시지는 OS 가 창을 만든 스레드에서만 주고, 엔진이 다른 스레드를 만드는 길은 작업 풀뿐이라 이 한 검사가 "동시 첫 호출 없음" 을 지킨다.
         */
        inline bool isOnPoolWorker()
        {
            return areEngineServicesBound() && getSubsystem<TaskManager>().isWorkerThread();
        }

        /** @brief 지금 스레드의 스크래치 슬롯입니다. 워커면 그 번호, 아니면 그 스레드가 처음 물을 때 받은 고유한 도우미 칸입니다. */
        inline uint32 getParallelScratchSlot()
        {
            if ( areEngineServicesBound() == false )
                return 0;
            return getSubsystem<TaskManager>().getCurrentThreadScratchSlot();
        }
    } // namespace engine
} // namespace sw
