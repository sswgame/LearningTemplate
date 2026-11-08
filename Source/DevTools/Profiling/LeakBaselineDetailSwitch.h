/**
 * @file LeakBaselineDetailSwitch.h
 * @brief 기동 누수 기준선 창을 스택 전수(`FullStacks`)로 여는 개발 스위치입니다(개발 도구 `DevTools` — 배포 빌드는 짓지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @struct LeakBaselineDetailSwitch
     * @brief `-gv_leakWindowFullStacks` 를 메모리 프로파일러의 기준선 질의(`MemoryProfiler::setLeakBaselineDetailQuery`)로 겁니다. 개발 도구 진입점(`DevToolsHost`)이 만들 때 걸고 소멸할 때 뗀다.
     * @details 켜면 종료 누수 보고가 줄마다 첫 프레임(누가 할당했나)을 적는다. 창 안의 모든 할당이 스택을 잡아 Debug 실행이 느려지므로 누수 출처를 찾을 때만 켠다.
     *          값은 기준선을 잡는 순간에 읽는다(명령줄이 풀린 뒤다).
     */
    struct SW_DEVTOOLS_API LeakBaselineDetailSwitch
    {
        /** @brief 기준선 질의를 겁니다. */
        static void install();
        /** @brief 기준선 질의를 뗍니다(기본 `TagTotals` 로 돌아간다). */
        static void uninstall();
    };
} // namespace sw
