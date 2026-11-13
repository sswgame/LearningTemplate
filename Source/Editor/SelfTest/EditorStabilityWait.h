/**
 * @file EditorStabilityWait.h
 * @brief 에디터가 멈출 때까지(정지 상태) 기다리는 한 번의 대기입니다 — 시나리오 단계(`<EditorWaitSettled/>`)와 에디터 자체 테스트(`EditorSelfTestStep::WaitStable`)가 함께 씁니다.
 * @details 정지는 넷이다: 화면(창 크기 요청 반영 · 도크 배치 적용 끝 · 밀린 흉내 입력 없음 · 기다림을 시작한 뒤에 그린 화면이 두 번 연속 같은 배치), 씬(전환 없음),
 *          에셋(씬이 기다리는 로드 · 업로드 · PSO 없음 — `CaptureReadiness::countPendingJobs`), 뒤에 끝날 일(끝 차단 `SW_AUTOMATION_FINISH_BLOCKER`).
 *          재료를 모으는 곳이 한 곳이라 두 길이 다른 정지를 보지 않는다. 판정은 `EditorStabilityState` 가 한다.
 *          언리얼 Automation 의 `FlushAsyncLoading` + Driver 의 "안정될 때까지", Playwright 의 "networkidle" + actionability("stable")와 같은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/SelfTest/EditorStabilityState.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorStabilityWait
     * @brief 정지 대기 하나의 상태입니다(기다림 번호 · 판정 · 이번 프레임에 막은 까닭).
     * @details 앞 단계(개발 명령 · 입력)의 효과는 다음에 열리는 프레임에 그려지므로 그 프레임에 찍힌 기다림 번호(`EditorSelfTestInput::beginStabilityWait`)로
     *          "기다림을 시작한 뒤의 화면" 을 가린다 — 프레임 수를 세지 않는다. 상태 저장소가 새로 서면(백엔드 교체 · 모듈 리로드가 에디터 컨텍스트를 다시 세운다 — 번호가 0 부터
     *          다시 센다) 거기서 다시 기다린다.
     */
    class EditorStabilityWait
    {
    public:
        EditorStabilityWait();

        /** @brief 이번 프레임의 재료를 모아 판정합니다. 처음 부르면(또는 상태 저장소가 새로 섰으면) 기다림을 시작합니다. @p pActiveManager 는 에셋 일을 셀 활성 씬입니다(없으면 nullptr). */
        [[nodiscard]] EditorStabilityResult advance( GameObjectManager* pActiveManager );
        /** @brief 마지막 `advance` 가 무엇에 막혔는지 한 줄입니다 — 바깥 그물에 걸리거나 진동으로 질 때 붙인다. */
        const string& describeWait() const { return _waitingFor; }
        /** @brief 진동으로 질 때 알릴 한 줄입니다(할 일 없이 움직인 프레임 수와 마지막으로 움직인 창). */
        [[nodiscard]] string describeOscillation() const;

    private:
        uint32               _waitSerial;
        EditorStabilityState _stability;
        string               _waitingFor;
    };
} // namespace sw::editor
