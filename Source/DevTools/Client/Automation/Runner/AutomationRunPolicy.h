/**
 * @file AutomationRunPolicy.h
 * @brief 시나리오 실행의 대기 정책(`Config/Dev/ScenarioRun.json`)입니다 — 정지 시한(`_stallFrames` · `_stallWallSeconds`) · 반복 실패 차단(`_repeatedFailureLimit`) · 정착 조건의 문턱(`_conditions`).
 * @details 정착 조건의 문턱은 조건을 등록한 기능이 제 칸을 읽는다(`AutomationStepRegistry::configureConditions`) — 이 구조체에 칸을 더하지 않는다.
 *          칸이 하나라도 없거나 범위 밖이면 읽기가 오류다(기본값이 없다). 상용 엔진 비교: Gauntlet 의 하트비트 간격 · 테스트 설정 파일 자리.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /** @struct AutomationRunPolicy @brief `Config/Dev/ScenarioRun.json` 의 실행기 칸입니다. */
    struct SW_DEVTOOLS_API AutomationRunPolicy
    {
        /** @brief 정책 파일(프로젝트 루트 기준)입니다. */
        static constexpr const utf8* kPolicyFile = "Config/Dev/ScenarioRun.json";

        /**
         * @brief 진행 중인 정착 조건의 진척이 이만큼의 그린 프레임 동안 그대로면 그 단계는 `WaitStalled` 로 진다(1 이상).
         * @details 그린 프레임으로 센다 — 장치를 다시 만드는 동안처럼 그리지 않는 프레임은 조건이 나아갈 기회가 없다.
         */
        uint32 _stallFrames{ 0 };
        /**
         * @brief 벽시계 감시 값(`SW_AUTOMATION_WALL_WATCH`)을 기다리는 `<WaitUntil timeoutWallSeconds>` 는 그 활동 수가 이만큼의 초(컴파일을 뺀 시계 · `gv_scenarioWallScale` 배율) 동안 그대로면 `WaitStalled` 로 진다(1 이상).
         * @details 바깥 일은 프레임이 실시간보다 빨리 돌아 프레임으로 셀 수 없다. 시한(`timeoutWallSeconds`)은 나아가고 있는 일의 상한으로만 남는다.
         */
        uint32 _stallWallSeconds{ 0 };
        /** @brief 한 체인에서 같은 대기 실패(종류 + 단계 + 감시 값 · 조건 이름)가 이만큼 나오면 체인을 `RepeatedFailure` 로 멈춘다(1 이상). */
        uint32 _repeatedFailureLimit{ 0 };

        /** @brief 정책 JSON 글을 읽습니다(`_conditions` 는 객체인지만 본다). 칸이 없거나 형식 · 범위가 틀리면 그 칸이 대상인 오류입니다. */
        [[nodiscard]] static expected<AutomationRunPolicy> parse( string_view jsonText, string_view sourceName );
        /** @brief 프로젝트의 정책 파일을 읽고, 등록된 정착 조건마다 `_conditions` 의 제 칸을 넘깁니다(`AutomationStepRegistry::configureConditions`). */
        [[nodiscard]] static expected<AutomationRunPolicy> load();
    };
} // namespace sw
