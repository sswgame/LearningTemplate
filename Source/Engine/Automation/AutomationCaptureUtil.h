/**
 * @file AutomationCaptureUtil.h
 * @brief 주 화면 캡처가 자리 잡은 그림을 찍을 수 있는지 묻는 곳입니다 — 남은 일의 셈(`CaptureReadiness`)에 자동화 층의 화면 덮개(로딩 화면 · 페이드)를 더한다.
 * @details 시나리오 `<Screenshot>` 의 기다림과 시작 인자 캡처(`-gv_screenshotSettledFrames`, 골든 이미지)가 같은 판정을 씁니다.
 *          언리얼 자동화 스크린샷이 레벨 로드 · 스트리밍 · 셰이더 컴파일이 끝나기를 기다린 뒤 찍는 것과 같은 자리입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 화면 캡처의 준비 판정입니다. 게임 스레드에서만 씁니다. */
    struct SW_API AutomationCaptureUtil
    {
        /**
         * @brief 지금 찍으면 자리 잡은 그림인지입니다 — 캡처를 미루는 조건(로딩 화면 · 화면 페이드 · 시계 잡기, `AutomationBlockScope`)이 없고
         *        남은 일(`CaptureReadiness::countPendingJobs` — 씬 쪽 에셋 일과 주 렌더러의 PSO 대기)이 0 입니다.
         * @details 씬이 플레이를 시작했는지는 보지 않는다(편집 모드 시나리오도 찍는다). 첫 씬을 기다려야 하는 시작 인자 캡처는 그것을 따로 본다.
         */
        [[nodiscard]] static bool isReadyForCapture( const GameObjectManager* pManager );
        /** @brief 씬 @p pManager 의 주 화면 캡처까지 남은 일 수입니다(`IEngineRenderDriver::countPendingCaptureJobs`). 렌더러가 없으면(전용 서버) 찍을 그림도 없어 0 입니다. */
        [[nodiscard]] static uint32 countPendingJobs( const GameObjectManager* pManager );
    };
} // namespace sw
