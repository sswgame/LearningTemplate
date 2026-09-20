/**
 * @file EngineParallel.h
 * @brief 엔진 코드가 쓰는 포크-조인 병렬 도우미 — 서비스가 묶여 있으면 잡으로, 아니면 현재 스레드에서.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"

namespace sw
{
    namespace engine
    {
        /**
         * @brief [0, count) 를 잡으로 나눠 돌리고 끝날 때까지 기다립니다 (상용 엔진의 ParallelFor).
         * @details 병렬 시스템 하나의 모양은 "상태를 가진 시스템 타입 + 이 호출 + 매니저 단계 한 줄" 이다 —
         *          트랜스폼 플러시·씬 수집·제자리 갱신이 각자 스테이지를 만들고 기다리던 열 줄이 이 한 줄이다.
         *          서비스가 아직 안 묶인 곳(테스트 초기·도구)에서는 현재 스레드가 한 번에 돈다. 문턱 아래도 같다:
         *          디스패치 바닥(~50 us)보다 작은 일은 나누면 느려진다.
         * @param count           일의 수
         * @param serialThreshold 이 수 미만이면 나누지 않는다
         * @param body            (start, end) 구간을 처리하는 본문. 워커는 컨테이너를 만지지 말고 포인터만 받을 것.
         */
        inline void runParallel( uint32 count, uint32 serialThreshold, const ParallelBlockDelegate& body )
        {
            if ( areEngineServicesBound() )
            {
                getTaskManager().runParallel( count, serialThreshold, body );
                return;
            }
            if ( count > 0 && body.isBound() )
                body( 0, count );
        }

        /**
         * @brief 병렬 본문이 스레드마다 하나씩 쓰는 스크래치의 슬롯 수 — 워커마다 하나, 도와주는 메인 스레드 몫 하나.
         * @details 잡마다 스크래치를 만들면 프레임당 청크 수만큼 할당이다. 슬롯 배열을 이 크기로 한 번 잡아 두고
         *          본문이 `getParallelScratchSlot()` 으로 자기 칸을 찾는다.
         */
        inline uint32 getParallelScratchSlotCount()
        {
            return areEngineServicesBound() ? getTaskManager().getWorkerCount() + 1 : 1;
        }

        /** @brief 지금 스레드의 스크래치 슬롯. 워커면 그 번호, 아니면(메인 스레드가 도울 때) 마지막 칸. */
        inline uint32 getParallelScratchSlot()
        {
            if ( areEngineServicesBound() == false )
                return 0;
            const int32 workerIndex = getTaskManager().getCurrentWorkerIndex();
            return workerIndex >= 0 ? static_cast<uint32>( workerIndex ) : getTaskManager().getWorkerCount();
        }
    } // namespace engine
} // namespace sw
