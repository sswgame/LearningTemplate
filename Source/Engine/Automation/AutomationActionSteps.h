/**
 * @file AutomationActionSteps.h
 * @brief 자동화 시나리오의 입력 맵 액션 단계(`Action`, `ActionPress`, `ActionRelease`, `ActionAxis`)를 정의합니다. 단계는 이 .cpp 의 정적 등록자가 등록합니다.
 * @details 액션 이름을 시작 프레임의 통합 입력 맵(`InputManager::getInputMap`)에서 바인딩으로 풀어 그 키 · 버튼 · 스틱을 가상 입력으로 낸다.
 *          그래서 트리거(Hold · DoubleTap · Tap), 레이어, UI 입력 소비가 사람의 입력과 같은 길을 지난다. 키 배치를 바꿔도 시나리오는 그대로다.
 *          언리얼 Enhanced Input 의 `InjectInputForAction` 이 값을 바로 넣는 것과 달리 장치 층에서 내므로 트리거 결함까지 잡는다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    /** @struct AutomationActionSteps @brief 액션 단계 모음입니다. */
    struct AutomationActionSteps
    {
        /**
         * @brief 아무 일도 하지 않습니다. 실행기가 이 함수를 불러 이 .cpp 가 링크에서 빠지지 않게 합니다.
         * @details 배포본은 엔진을 정적 라이브러리로 한 exe 에 링크하므로 아무도 부르지 않는 오브젝트 파일의 정적 등록자는 빠진다.
         */
        static void ensureLinked();
    };
} // namespace sw
