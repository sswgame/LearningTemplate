/**
 * @file ProfilerLaunchSubsystem.h
 * @brief 프로파일러 출력을 실행 인자로 켜는 개발 도구입니다 — Tracy(`-gv_tracy` · `-gv_tracyMemory`), 종료 트레이스(`-gv_traceSaveOnExit`), 느린 프레임 자동 캡처(`-gv_traceSlowTickMs`),
 *        종료 때 에셋 로드 표(`-gv_assetLoadReport`).
 * @details 예전에는 엔진(`ProfilerBackend::initialize` · `Engine::initialize` · `Engine::shutdown`)과 서버 틱 루프(`ServerApp::run`)가 테스트용 변수를 읽었다.
 *          출력 자체(`ProfilerBackend` · `ProfilerTraceExport`)는 엔진에 두고, 실행 인자로 언제 켜고 어디에 쓸지는 개발 도구가 정한다 — 배포 호스트는 DevTools 를 링크하지 않는다
 *          (언리얼 `-tracehost` · `-trace=` 실행 인자가 Developer 쪽 트레이스 모듈에서 읽히는 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /**
     * @class ProfilerLaunchSubsystem
     * @brief 기동 단계 서브시스템입니다. 초기화 때 Tracy · 종료 트레이스를 켜고, 종료 때 종료 트레이스 · 에셋 로드 표를 쓰며, 프레임마다 느린 프레임을 잡습니다.
     * @details 서비스 범위(`FrameProfiler`)가 먼저 서므로 초기화 때 프로파일러가 있고, 종료 때도 아직 있다(기동 단계가 서비스보다 먼저 내려간다).
     *          Tracy 는 기동 단계부터 기록한다 — 서비스 범위의 초기화 구간은 타임라인에 없다.
     */
    class ProfilerLaunchSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ProfilerLaunchSubsystem";

        ProfilerLaunchSubsystem();

        SubsystemInitResult initializeSubsystem() override;
        void                shutdownSubsystem() override;
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 프레임 앞 — 느린 프레임 판정의 시작 시각을 적습니다(개발 도구의 프레임 앞 훅). */
        void onFrameBegin();
        /**
         * @brief 프레임 끝 — 프레임 본문(프레임 앞 훅 ~ 프레임 끝 훅)이 `-gv_traceSlowTickMs` 를 넘었으면 그 앞뒤 링 버퍼를 트레이스로 저장합니다(`ProfilerTraceExport::updateSlowFrameCapture`).
         * @details 전용 서버는 한 틱이 한 프레임이다. 꺼져 있으면 정수 비교 하나다.
         */
        void onFrameEnd();

    private:
        /** @brief 트레이스 머리말의 프로세스 이름입니다(전용 서버 `Server`, 그 밖 `App`). */
        static const utf8* getProcessName();

    private:
        uint64 _frameBeginNanos; ///< 이번 프레임 앞 훅의 단조 시각(나노초). 첫 프레임 앞 훅 전에는 0 이다
    };
} // namespace sw
