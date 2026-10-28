/**
 * @file FrameProfileSession.h
 * @brief `-gv_profileFrames=N` 계측 한 회분입니다(워밍업 버리기 → N 프레임 측정 → 보고 → 종료 요청).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @class FrameProfileSession
     * @brief 프레임 프로파일 한 회분의 진행 상태를 들고 판정합니다.
     * @details 계측은 루프의 일이 아니라 계측의 일이므로 상태와 판정 규칙을 여기로 모읍니다.
     *          `Engine` 는 `begin` / `onFrameEnd` 두 줄만 부르고, 밖으로 나가는 것은 `isQuitRequested()`
     *          하나입니다. 상속이 아니라 합성인 이유: `Engine` 는 가상 함수가 하나도 없고 App 이 **값으로**
     *          들고 있습니다. 계측 하나 때문에 `SW_API` 클래스에 vtable 을 만들고 소유 모델을 바꿀 일이 아닙니다.
     * @note `FrameProfiler` 의 프레임 마감(`endFrame`)은 여기서 부르지 않습니다. 프레임 경계는 루프의
     *       일이고 이 타입은 판정만 합니다. 부르는 쪽이 먼저 마감한 뒤 부릅니다.
     */
    class SW_API FrameProfileSession
    {
    public:
        /**
         * @brief 통계에서 버리는 초반 프레임 수입니다.
         * @details 셰이더 컴파일 · PSO 생성 · 트랜지언트 할당이 첫 프레임들을 크게 부풀립니다. 섞으면
         *          평균이 그 한 번에 끌려가 아무것도 읽을 수 없습니다.
         */
        static constexpr uint64 kWarmupFrames = 60;

        /** @brief `-gv_profileFrames` · `-gv_profileSeconds` 를 읽어 계측을 시작합니다. 둘 다 0 이하면 아무것도 하지 않습니다. */
        void begin();

        /** @brief 프레임 마감 뒤 한 번 부릅니다. 목표를 채우면 보고하고 종료를 요청합니다. */
        void onFrameEnd();

        /** @brief 목표 프레임을 채워 종료하려 하면 true 입니다. */
        bool isQuitRequested() const { return _bQuitRequested == SW_TRUE; }

        /** @brief 계측을 시작했으면(기동 인자든 뒤늦은 시작이든) true 입니다. 앱 루프는 이 동안 포커스와 상관없이 전속으로 돈다(`FramePacingState::Automation`). */
        bool isActive() const { return _bActive == SW_TRUE; }

        /**
         * @brief 보고를 이미 냈으면 true 입니다. 이 타입이 볼 수 없는 표(GPU 메모리 — Graphics 는 Utility 위 층이다)는 이 값이 바뀐 프레임에
         *        부르는 쪽(`Engine`)이 이어서 남깁니다.
         */
        bool hasReported() const { return _bReported == SW_TRUE; }

        /**
         * @brief 측정 창을 닫을 때인가입니다. 프레임 목표와 시간 목표 가운데 먼저 닿는 쪽이 닫습니다(0 인 목표는 보지 않습니다).
         * @details 프레임 상한이 없는 실행은 초당 프레임 수가 장면마다 수십 배 달라 "N 프레임" 이 몇 초인지 모릅니다 — 오래 도는 테스트(soak)은
         *          시간으로 끊습니다(`-gv_profileSeconds`).
         */
        static bool isMeasureWindowDone( uint64 frames, uint64 frameTarget, int64 elapsedMicro, int64 secondsTarget );

        /** @brief 히치 꼬리에 남기는 최악 프레임 수입니다. */
        static constexpr uint32 kWorstFrameCount = 5;
        /** @brief 히치로 세는 프레임 간격 임계값(마이크로초) — 60 Hz 한 주기 · 두 주기 · 100 ms 입니다. */
        static constexpr int64 kArrHitchThresholdMicro[] = { 16667, 33333, 100000 };

        /** @brief 프레임 간격 히스토그램의 칸 너비(마이크로초)입니다. 165 Hz 한 주기(6.06 ms)를 60 칸으로 나눌 만큼 잘다. */
        static constexpr int64 kPacingBinMicro = 100;
        /** @brief 프레임 간격 히스토그램의 칸 수입니다 — 0 ~ 100 ms 이고 넘는 간격은 마지막 칸에 넣는다(놓침 수는 마지막 칸도 센다). */
        static constexpr uint32 kPacingBinCount = 1000;

        /**
         * @struct FramePacing
         * @brief 측정 창의 프레임 페이싱 — 중앙 간격과 그 1.5 배 · 2.5 배를 넘은 프레임 수입니다.
         * @details VSync 를 켜면 중앙 간격이 곧 화면 주기다. 1.5 배를 넘은 프레임은 주기를 하나 이상, 2.5 배는 둘 이상 놓쳤다(Candidates-graphics
         *          VulkanVsyncFrameMiss). 고정 문턱(`kArrHitchThresholdMicro`, 60 Hz 기준)은 165 Hz 에서 주기 둘을 놓쳐도 세지 않는다 — 주사율을
         *          묻지 않고 측정에서 주기를 얻는다. VSync 를 끄면 중앙 간격은 프레임 비용이고 이 수는 그 대비 튐이다.
         */
        struct FramePacing
        {
            int64  _medianMicro{ 0 };          ///< 중앙 간격(그 칸의 가운데 값). 프레임이 없으면 0
            uint32 _overOneAndHalfMedian{ 0 }; ///< 칸 아래 끝이 중앙 간격의 1.5 배 이상인 프레임 수
            uint32 _overTwoAndHalfMedian{ 0 }; ///< 칸 아래 끝이 중앙 간격의 2.5 배 이상인 프레임 수
        };
        /** @brief 간격 히스토그램(@p binCount 칸, 칸 너비 `kPacingBinMicro`)에서 페이싱을 구합니다. 순수 함수입니다. */
        static FramePacing computePacing( const uint32* pArrBin, uint32 binCount );

    private:
        /** @brief 측정 창의 프레임당 할당 수와(켰다면) 콜스택별 상위 자리를 로그로 남깁니다. */
        void reportAllocations( uint64 frames );
        /** @brief 측정 창 안 게임 스레드 프레임 간격(벽시계) 하나를 히치 꼬리에 더합니다. */
        void recordFrameInterval( int64 intervalMicro );
        /**
         * @brief 히치 꼬리 줄(`[Profile] hitch`)과 페이싱 줄(`[Profile] pacing`)을 남깁니다 — 최악 프레임 다섯, 임계값을 넘은 프레임 수, 중앙 간격 대비 놓친 주기.
         *        평균 · 백분위 표가 가리는 한 번의 긴 프레임을 본다.
         */
        void reportHitches() const;

        /** @brief 측정할 프레임 수입니다. 0 이면 프레임 수로 끊지 않습니다(시간 목표만 있을 때). */
        uint64 _frameTarget{ 0 };
        /** @brief 측정할 시간(초)입니다. 0 이면 시간으로 끊지 않습니다. */
        int64 _secondsTarget{ 0 };
        /** @brief 계측을 시작했으면 true 입니다(프레임 · 시간 목표 가운데 하나라도 있었다). */
        uint8 _bActive{ SW_FALSE };
        /** @brief 보고를 이미 냈으면 true 입니다. 프레임마다 다시 찍지 않습니다. */
        uint8 _bReported{ SW_FALSE };
        /** @brief 목표 프레임을 채워 종료하려 하면 true 입니다. */
        uint8 _bQuitRequested{ SW_FALSE };
        /** @brief 워밍업 구간을 이미 버렸으면 true 입니다. */
        uint8 _bWarmedUp{ SW_FALSE };
        /** @brief 측정 구간에서 할당 추적을 켰으면 true 입니다(보고 뒤 세부 추적을 끕니다). */
        uint8 _bAllocationTrackingStarted{ SW_FALSE };
        /** @brief 측정 구간이 시작될 때의 할당 횟수 누계입니다. 프레임당 할당 수는 보고 시점 값과의 차이로 구합니다. */
        uint64 _allocationCountAtStart{ 0 };
        /**
         * @brief 측정 구간이 시작된 벽시계 시각(마이크로초, steady_clock)입니다.
         * @details 구간 표는 스레드마다 **일한 시간**이라 "프레임이 실제로 몇 us 마다 나오나" 를 답하지 못합니다. 게임 스레드가
         *          빨라져 렌더 스레드의 구간이 늘어 보일 때(경합인지 회귀인지) 가르려면 측정 구간 전체의 벽시계가 필요합니다.
         */
        int64 _measureStartMicro{ 0 };
        /** @brief 앞 프레임이 끝난 벽시계 시각(마이크로초)입니다. 측정 창 안에서만 씁니다. */
        int64 _lastFrameEndMicro{ 0 };
        /** @brief 측정 창의 가장 긴 프레임 간격들(마이크로초, 큰 순)입니다. */
        int64 _arrWorstFrameMicro[kWorstFrameCount]{};
        /** @brief `kArrHitchThresholdMicro` 의 임계값마다 넘은 프레임 수입니다. */
        uint32 _arrHitchCount[3]{};
        /** @brief 측정 창의 프레임 간격 히스토그램입니다(칸 너비 `kPacingBinMicro`). 할당 없이 중앙값과 놓친 주기를 구한다. */
        uint32 _arrPacingBin[kPacingBinCount]{};
    };
} // namespace sw
