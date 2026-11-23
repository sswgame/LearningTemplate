/**
 * @file GameSpeedControl.h
 * @brief 디렉터가 쥐는 배속 · 일시정지 하나입니다. 시뮬레이션(도시 · RTS 판 · 공유 시계)은 이 값을 받아 걸음 수로 돈다(`FixedStepTimer::consumeScaled`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GameSpeedControl
     * @brief 배속(0.25 ~ 8 배, 단계는 2 배)과 일시정지입니다. 게임마다 따로 들던 배속 · 일시정지 코드(Nile · Skirmish 의 같은 코드, 개척 마을의 시계 배율)를 하나로 모은다(감사 K-10).
     * @details 유니티 `Time.timeScale` · 언리얼 `SetGlobalTimeDilation` 과 같은 자리지만 엔진 전역이 아니라 디렉터 하나가 쥔다 — 시뮬레이션만 느려지고 UI · 카메라는 그대로다.
     *          범위 밖 배율은 자르지 않고 거절한다(폴백 금지). 저장은 쓰는 디렉터가 자기 상태 순서대로 `getTimeScale` · `isPaused` 를 싣고 `restore` 로 되돌린다.
     */
    class SW_GF_API GameSpeedControl
    {
    public:
        static constexpr float32 kMinTimeScale = 0.25f; ///< 가장 느린 배속
        static constexpr float32 kMaxTimeScale = 8.0f;  ///< 가장 빠른 배속
        static constexpr float32 kStepFactor   = 2.0f;  ///< 빠르게 · 느리게 한 단계의 배율

        GameSpeedControl();

        /** @brief 지금 배속입니다(일시정지와 따로 둔다 — 풀면 같은 배속으로 돈다). */
        float32 getTimeScale() const { return _timeScale; }
        bool    isPaused() const { return _bPaused != SW_FALSE; }

        /** @brief 입력 맵 액션 셋을 한 번에 처리합니다. 일시정지는 뒤집고, 빠르게 · 느리게는 한 단계씩(범위 끝에서 멈춘다). 바뀌면 정보 로그를 남긴다. */
        void applyInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered );
        /** @brief 배속을 바로 둡니다(누르는 동안 빨리 감기). 범위 밖이면 오류를 남기고 false 이며 지금 값을 그대로 둔다. */
        [[nodiscard]] bool setTimeScale( float32 timeScale );
        /** @brief 저장한 값으로 되돌립니다. 배속이 범위 밖이면 false 이고 아무것도 바꾸지 않는다(신뢰 경계 — 세이브 바이트). */
        [[nodiscard]] bool restore( float32 timeScale, bool bPaused );

    private:
        float32                _timeScale;
        uint8                  _bPaused  : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw
