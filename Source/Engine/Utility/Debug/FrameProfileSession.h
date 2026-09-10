/**
 * @file FrameProfileSession.h
 * @brief `-gv_profileFrames=N` 계측 한 판 — 워밍업 폐기 → N 프레임 측정 → 보고 → 종료 요청
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class FrameProfileSession
     * @brief 프레임 프로파일 한 판의 진행 상태를 들고 판정합니다.
     * @details 예전에는 상태 넷이 `EngineLoop` 의 **public 멤버**로 놓여 있었고 판정 규칙은 `tick`
     *          한가운데 스무 줄로 박혀 있었다. 계측은 루프의 일이 아니라 계측의 일이므로 여기로 모은다 —
     *          `EngineLoop` 은 `begin` / `onFrameEnd` 두 줄만 부르고, 밖으로 나가는 것은 `wantsQuit()`
     *          하나다. 상속이 아니라 합성인 이유: `EngineLoop` 은 가상 함수가 하나도 없고 App 이 **값으로**
     *          들고 있다. 계측 하나 때문에 `SW_API` 클래스에 vtable 을 만들고 소유 모델을 바꿀 일이 아니다.
     * @note `FrameProfiler` 의 프레임 마감(`endFrame`)은 여기서 부르지 않는다 — 프레임 경계는 루프의
     *       일이고 이 타입은 판정만 한다. 호출자가 먼저 마감한 뒤 부른다.
     */
    class SW_API FrameProfileSession
    {
    public:
        /**
         * @brief 통계에서 버리는 초반 프레임 수.
         * @details 셰이더 컴파일·PSO 생성·트랜지언트 할당이 첫 프레임들을 크게 부풀린다. 섞으면
         *          평균이 그 한 번에 끌려가 아무것도 못 읽는다.
         */
        static constexpr uint64 kWarmupFrames = 60;

        /** @brief `-gv_profileFrames` 를 읽어 계측을 시작합니다. 0 이하면 아무것도 하지 않습니다. */
        void begin();

        /** @brief 프레임 마감 뒤 한 번 부릅니다. 목표를 채우면 보고하고 종료를 요청합니다. */
        void onFrameEnd();

        /** @brief 목표 프레임을 채워 종료하려 하면 true. */
        bool wantsQuit() const { return _bWantsQuit == SW_TRUE; }

    private:
        /** @brief 잴 프레임 수. 0 이면 계측하지 않습니다. */
        uint64 _frameTarget{ 0 };
        /** @brief 보고를 이미 냈으면 true — 매 프레임 다시 찍지 않습니다. */
        uint8 _bReported{ SW_FALSE };
        /** @brief 목표 프레임을 채워 종료하려 하면 true. */
        uint8 _bWantsQuit{ SW_FALSE };
        /** @brief 워밍업 구간을 이미 버렸으면 true. */
        uint8 _bWarmedUp{ SW_FALSE };
    };
} // namespace sw
