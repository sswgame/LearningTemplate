/**
 * @file AutomationEnvironmentSteps.h
 * @brief 자동화 시나리오의 환경 단계(`DevCommand`, `ResizeWindow`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    /** @struct AutomationEnvironmentSteps @brief 환경 단계 모음입니다. */
    struct AutomationEnvironmentSteps
    {
        /** @brief 개발 명령 단계의 엘리먼트 이름입니다. 이 단계가 든 시나리오는 개발 명령이 없는 구성(Shipping)에서 건너뜁니다(종료 코드 13). */
        static constexpr const utf8* kDevCommandKind = "DevCommand";

        /**
         * @brief 아무 일도 하지 않습니다. 실행기가 이 함수를 불러 이 .cpp 가 링크에서 빠지지 않게 합니다.
         * @details 배포본은 엔진을 정적 라이브러리로 한 exe 에 링크하므로 아무도 부르지 않는 오브젝트 파일의 정적 등록자는 빠진다.
         */
        static void ensureLinked();
    };
} // namespace sw
