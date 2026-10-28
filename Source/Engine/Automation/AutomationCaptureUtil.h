/**
 * @file AutomationCaptureUtil.h
 * @brief 화면 캡처가 자리 잡은 그림을 찍을 수 있는지(로딩 화면 · 페이드 · 에셋 읽기가 끝났는지)를 묻는 한 곳입니다.
 * @details 시나리오 `<Screenshot>` 의 기다림과 시작 인자 캡처(`-gv_screenshotSettledFrames`, 골든 이미지)가 같은 판정을 씁니다.
 *          언리얼 자동화 스크린샷이 레벨 로드 · 스트리밍 · 셰이더 컴파일이 끝나기를 기다린 뒤 찍는 것과 같은 자리입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class GameObjectManager;

    /** @brief 화면 캡처의 준비 판정입니다. 게임 스레드에서만 씁니다. */
    struct SW_API AutomationCaptureUtil
    {
        /**
         * @brief 씬이 기다리는 에셋 일의 수입니다 — 읽는 중이거나 애니메이션을 기다리거나 디더로 나타나는 중인 메시(씬 프레임 도착 단계의 대기), 읽거나 올리는 중인 텍스처.
         * @details 감시 값 `Assets.Pending` 이 같은 셈을 쓴다. @p pManager 가 nullptr 이면 텍스처만 센다.
         */
        [[nodiscard]] static uint32 countPendingAssetWork( const GameObjectManager* pManager );
        /**
         * @brief 지금 찍으면 자리 잡은 그림인지입니다 — 캡처를 미루는 조건(로딩 화면 · 화면 페이드 · 시계 잡기, `AutomationBlockScope`)이 없고 에셋 일이 0 입니다.
         * @details 씬이 플레이를 시작했는지는 보지 않는다(편집 모드 시나리오도 찍는다). 첫 씬을 기다려야 하는 시작 인자 캡처는 그것을 따로 본다.
         */
        [[nodiscard]] static bool isReadyForCapture( const GameObjectManager* pManager );
    };
} // namespace sw
