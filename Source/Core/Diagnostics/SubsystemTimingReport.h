/**
 * @file Core/Diagnostics/SubsystemTimingReport.h
 * @brief 서브시스템 목록의 기동 · 종료 시간을 재게 하고(개발 구성만) 요약 줄로 잇습니다.
 * @details 시간은 `SubsystemCollection` 이 재지만 시계는 위 층이 건넨다(Subsystem 층은 Time · 문자열 빌더를 include 할 수 없다). 시계를 거는 자리를 여기 하나로 모아
 *          배포본은 재지 않는다 — 요약 줄이 Info 라 배포본에서는 컴파일되지 않으니 잰 값을 볼 곳이 없다. 언리얼의 `LogInit` 시간 · stat 처럼 개발 구성의 진단이다.
 *          Core 진단에 둔다 — Core 형식(서브시스템 목록 · 단조 시계 · 문자열 빌더)만 쓰고 엔진 루프 · 에디터 기동이 같은 줄을 남긴다. 언리얼 `FScopedBootTiming` 도 Core 다
 *          (예전 자리 Engine/Profiling 은 엔진 루프가 개발 보고를 이름으로 부르는 모양으로 보였다 — 이것은 보고 도구가 아니라 시계 걸기와 한 줄 요약이다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/SubsystemCollection.h"

namespace sw
{
    /** @brief 서브시스템 시간 진단의 입구입니다(엔진 서비스 · 기동 단계 · 에디터 기동 단계 · 에디터 범위). */
    struct SW_API SubsystemTimingReport
    {
        /** @brief @p collection 이 기동 · 종료 시간을 재게 합니다. 배포본은 아무것도 하지 않습니다(`getInitTimings` · `getShutdownTimings` 가 빈다). */
        static void enableTiming( SubsystemCollection& collection );
        /** @brief 시간 목록을 오래 걸린 순으로 `이름=ms` 로 잇습니다(1 ms 미만은 뺀다 — 시동 · 종료 요약 줄). */
        [[nodiscard]] static string describe( vector<SubsystemCollection::SubsystemTiming> listTiming );
    };
} // namespace sw
