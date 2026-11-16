/**
 * @file FrameProfileSession.h
 * @brief `-gv_profileFrames=N` · `-gv_profileSeconds=S` 계측 한 회분입니다(워밍업 버리기 → 측정 → 보고 → 종료 요청). 개발 도구(`DevTools`)에 있다 — 배포 호스트는 DevTools 를 링크하지 않는다.
 * @details 예전에는 엔진(`Engine`)이 값으로 들고 프레임 끝마다 불러 배포 바이너리에 계측 실행 · 테스트용 변수가 실렸다. 계측의 재료(`FrameProfiler` · 보고 형식
 *          `FrameProfileReport` · GPU 메모리 표)는 엔진에 두고, 언제 재고 언제 끝낼지는 개발 도구가 프레임 끝 훅(`DevToolsHost::onFrameEnd`)에서 정한다 —
 *          언리얼 `stat startfile` · `-benchmark` 처럼 계측 실행은 개발 쪽의 일이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/EngineFrameHooks.h"
#include "Engine/Profiling/FrameProfileReport.h"

namespace sw
{
    class FrameProfiler;
    class IEngineRenderDriver;

    /**
     * @class FrameProfileSession
     * @brief 프레임 프로파일 한 회분의 진행 상태를 들고 판정합니다. 개발 도구가 기동 단계 범위에 하나 둔다(`DevToolsHost::addStartupSubsystems`).
     * @details 목표는 프레임 끝마다 읽는다 — 실행 인자로 준 목표는 첫 프레임 끝에, 부하 시나리오(`<Load durationSeconds>`)가 씬 플레이를 시작한 프레임에
     *          넣은 목표(`gv_profileSeconds`)는 그 프레임 끝에 측정을 연다. 목표를 채우면 보고하고 종료 코드(통과)를 낸다.
     * @note `FrameProfiler` 의 프레임 마감(`endFrame`)은 여기서 부르지 않습니다. 프레임 경계는 엔진 루프의 일이고 이 타입은 판정만 합니다. 루프가 먼저 마감한 뒤 훅이 부릅니다.
     */
    class FrameProfileSession final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "FrameProfileSession";

        /**
         * @brief 통계에서 버리는 초반 프레임 수입니다.
         * @details 셰이더 컴파일 · PSO 생성 · 트랜지언트 할당이 첫 프레임들을 크게 부풀립니다. 섞으면
         *          평균이 그 한 번에 끌려가 아무것도 읽을 수 없습니다.
         */
        static constexpr uint64 kWarmupFrames = 60;

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

        /** @brief @p pRenderDriver 는 GPU 메모리 표 · RHI 이름을 묻는 창구다(렌더러가 없는 호스트 — 전용 서버 — 는 nullptr). */
        explicit FrameProfileSession( IEngineRenderDriver* pRenderDriver );
        FrameProfileSession( const FrameProfileSession& )            = delete;
        FrameProfileSession& operator=( const FrameProfileSession& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 프레임 마감 뒤 한 번 부릅니다(개발 도구의 프레임 끝 훅). 목표가 새로 생겼으면 측정을 열고, 채우면 보고(로그 · GPU 메모리 표 · 캐시 표 ·
         *        `-gv_profileReportPath` JSON)를 낸 뒤 종료 코드(통과)를 돌려줍니다.
         * @param activeSceneID 보고에 적을 활성 씬 식별자(`EngineFrameContext::_activeSceneID`)
         */
        EngineFrameExit onFrameEnd( string_view activeSceneID );

        /** @brief 측정 창의 기계 판독 보고입니다(보고 뒤에 유효). */
        const FrameProfileReport& getReport() const { return _report; }

        /**
         * @brief 보고를 낸 프레임에 종료 코드를 낼지 정합니다(기본 true — 실행 인자 `-gv_profileSeconds` 계측은 측정이 곧 실행 전체다).
         * @details 부하 시나리오는 끄고 끝을 실행기가 정한다 — 측정 창(벽시계)과 판정 단계(시뮬레이션 시계)가 따로 흘러, 느린 기계에서 창이 먼저 닫히면
         *          판정이 돌기 전에 프로세스가 끝났다. 언리얼 CSV 프로파일러의 `-csvExitOnCompletion` 과 같은 자리다(끝은 테스트 컨트롤러가 쥔다).
         */
        void setExitOnCompletion( bool bExit ) { _bExitOnCompletion = bExit ? SW_TRUE : SW_FALSE; }
        /** @brief 측정 창을 닫고 보고를 냈는가입니다. */
        bool isCompleted() const { return _bReported == SW_TRUE; }

        /**
         * @brief 측정 창을 닫을 때인가입니다. 프레임 목표와 시간 목표 가운데 먼저 닿는 쪽이 닫습니다(0 인 목표는 보지 않습니다).
         * @details 프레임 상한이 없는 실행은 초당 프레임 수가 장면마다 수십 배 달라 "N 프레임" 이 몇 초인지 모릅니다 — 오래 도는 테스트(soak)은
         *          시간으로 끊습니다(`-gv_profileSeconds`).
         */
        static bool isMeasureWindowDone( uint64 frames, uint64 frameTarget, int64 elapsedMicro, int64 secondsTarget );
        /** @brief 간격 히스토그램(@p binCount 칸, 칸 너비 `kPacingBinMicro`)에서 페이싱을 구합니다. 순수 함수입니다. */
        static FramePacing computePacing( const uint32* pArrBin, uint32 binCount );

    private:
        /**
         * @brief `-gv_profileFrames` · `-gv_profileSeconds` 를 읽어 계측을 시작합니다. 앱 루프가 포커스와 상관없이 전속으로 돌도록 페이싱 요청이 `Auto` 면 `FullSpeed` 로 둔다
         *        (`gv_framePacing` — 배경 창의 상한이 측정을 흐리지 않게).
         */
        void begin();
        /** @brief 측정 창 하나를 진행합니다. 보고를 냈으면 true 입니다. */
        bool advance();
        /** @brief 보고를 낸 프레임에 엔진 쪽 표(GPU 메모리 · 캐시 적중)를 남기고 `-gv_profileReportPath` 에 JSON 을 씁니다(경로가 비면 쓰지 않는다). */
        void reportEngineTables( string_view activeSceneID );
        /** @brief 측정 창의 프레임당 할당 수와(켰다면) 콜스택별 상위 자리를 로그로 남깁니다. */
        void reportAllocations( uint64 frames );
        /** @brief 측정 창 안 게임 스레드 프레임 간격(벽시계) 하나를 히치 꼬리에 더합니다. */
        void recordFrameInterval( int64 intervalMicro );
        /**
         * @brief 히치 꼬리 줄(`[Profile] hitch`)과 페이싱 줄(`[Profile] pacing`)을 남깁니다 — 최악 프레임 다섯, 임계값을 넘은 프레임 수, 중앙 간격 대비 놓친 주기.
         *        평균 · 백분위 표가 가리는 한 번의 긴 프레임을 본다.
         */
        void reportHitches() const;
        /** @brief 측정 창의 값(벽시계 · 히치 · 페이싱 · 할당 · 구간 표)을 `_report` 에 담습니다. 로그 표와 같은 값이다. */
        void fillReport( const FrameProfiler& profiler, uint64 frames, int64 elapsedMicro );

    private:
        /** @brief GPU 메모리 표 · RHI 이름을 묻는 창구입니다. 렌더러가 없는 호스트는 nullptr 입니다. */
        IEngineRenderDriver* _pRenderDriver;
        /** @brief 측정할 프레임 수입니다. 0 이면 프레임 수로 끊지 않습니다(시간 목표만 있을 때). */
        uint64 _frameTarget;
        /** @brief 측정할 시간(초)입니다. 0 이면 시간으로 끊지 않습니다. */
        int64 _secondsTarget;
        /** @brief 측정 구간이 시작될 때의 할당 횟수 누계입니다. 프레임당 할당 수는 보고 시점 값과의 차이로 구합니다. */
        uint64 _allocationCountAtStart;
        /**
         * @brief 측정 구간이 시작된 벽시계 시각(마이크로초, steady_clock)입니다.
         * @details 구간 표는 스레드마다 **일한 시간**이라 "프레임이 실제로 몇 us 마다 나오나" 를 답하지 못합니다. 게임 스레드가
         *          빨라져 렌더 스레드의 구간이 늘어 보일 때(경합인지 회귀인지) 가르려면 측정 구간 전체의 벽시계가 필요합니다.
         */
        int64 _measureStartMicro;
        /** @brief 앞 프레임이 끝난 벽시계 시각(마이크로초)입니다. 측정 창 안에서만 씁니다. */
        int64 _lastFrameEndMicro;
        /** @brief 측정 창의 가장 긴 프레임 간격들(마이크로초, 큰 순)입니다. */
        int64 _arrWorstFrameMicro[kWorstFrameCount];
        /** @brief `kArrHitchThresholdMicro` 의 임계값마다 넘은 프레임 수입니다. */
        uint32 _arrHitchCount[3];
        /** @brief 측정 창의 프레임 간격 히스토그램입니다(칸 너비 `kPacingBinMicro`). 할당 없이 중앙값과 놓친 주기를 구한다. */
        uint32 _arrPacingBin[kPacingBinCount];
        /** @brief 측정 창의 기계 판독 보고입니다(`fillReport` 가 채우고 `reportEngineTables` 가 쓴다). */
        FrameProfileReport _report;
        uint8              _bActive                    : 1; ///< 계측을 시작했다(프레임 · 시간 목표 가운데 하나라도 있었다)
        uint8              _bReported                  : 1; ///< 보고를 이미 냈다 — 프레임마다 다시 찍지 않는다
        uint8              _bWarmedUp                  : 1; ///< 워밍업 구간을 이미 버렸다
        uint8              _bAllocationTrackingStarted : 1; ///< 측정 구간에서 할당 추적을 켰다(보고 뒤 세부 추적을 끈다)
        uint8              _bExitOnCompletion          : 1; ///< 보고를 낸 프레임에 종료 코드(통과)를 낸다(`setExitOnCompletion`)
    };
} // namespace sw
