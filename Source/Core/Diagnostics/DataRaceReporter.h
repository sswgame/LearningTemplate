/**
 * @file DataRaceReporter.h
 * @brief 컨테이너 경합 검출기(`RaceDetectContext`)가 찾은 경합을 로그와 호출 스택으로 남기는 보고기입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) DataRaceReporter — 경합 검출 훅(Concurrency)에 로그 · 호출 스택 보고를 건다
    //    이 파일의 정적 등록이 프로그램 시작 때 걸고, `Core` STATIC 을 링크하는 실행 파일은 진입점에서 직접 부른다
    // ------------------------------------------------------------------------------
    /**
     * @brief 경합 보고 함수를 `RaceDetectContext::setReportFunction` 에 겁니다.
     * @details 보고는 Error 로그 한 줄과 호출 스택 줄들입니다. 디버거에서 멈추는 일은 검출기가 합니다.
     */
    struct SW_API DataRaceReporter
    {
        /** @brief 보고 함수를 겁니다. 여러 번 불러도 됩니다. */
        static void install();
    };
} // namespace sw
