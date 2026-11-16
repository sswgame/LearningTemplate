/**
 * @file DevTools/Client/Capture/StartupCaptureSubsystem.h
 * @brief 시작 인자 사진(`-gv_screenshot` 과 그 짝들)을 찍고, `-gv_screenshotQuit` 이면 요청한 사진(게임 뷰 · 에디터 UI)을 모두 찍은 뒤 앱을 끝내는 개발 서브시스템입니다.
 * @details 예전에는 렌더 스레드 · 엔진 루프 · 렌더 패킷이 이 개발 기능의 칸(대기 프레임 수 · 자리 잡음 판정 · 종료 장부)을 들고 있었다 — 배포본에도 실렸다.
 *          지금은 개발 도구 클라이언트(`DevToolsClient`)가 기동 확장으로 세우고 프레임 끝 훅에서 돌린다. 사진은 런타임의 실행 중 요청 창구(`RenderThread::requestScreenshot`,
 *          개발 명령 screenshot · 에디터 버튼과 같은 길)로 한 장씩 맡긴다. 언리얼 자동화 스크린샷(`-AutomationScreenshot`)이 개발 모듈에 있는 것과 같은 자리다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/EngineFrameHooks.h"

namespace sw
{
    /** @brief 시작 인자가 찍는 사진의 종류입니다(비트). `-gv_screenshotQuit` 은 요청한 종류가 모두 끝나야 앱을 끝냅니다. */
    enum class StartupCaptureKind : uint32
    {
        GameView = 1u << 0, ///< `-gv_screenshot` — 이 서브시스템이 렌더 스레드에 맡긴다.
        EditorUI = 1u << 1, ///< `-gv_screenshotEditor` — 에디터가 찍는다(`EditorUICapture`).
    };

    /**
     * @class StartupCaptureSubsystem
     * @brief 시작 인자 사진의 차례(대기 프레임 · 화면 자리 잡음 · 간격 · 장 수)를 정해 맡기고, 종류별 끝 · 실패를 적어 `-gv_screenshotQuit` 의 종료 코드를 냅니다.
     * @details `expectCapture` · `reportCaptureFinished` 는 어느 스레드에서 불러도 된다(에디터의 쓰기 워커가 부른다). `update` 는 게임 스레드의 프레임 끝에서만 부른다.
     */
    class SW_DEVTOOLS_API StartupCaptureSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8*               kSubsystemName      = "StartupCapture";
        static constexpr MemoryTag                 kSubsystemMemoryTag = MemoryTag::RenderCPU;
        static constexpr SubsystemModuleVisibility kModuleVisibility   = SubsystemModuleVisibility::HostModules; ///< 에디터 UI 사진이 시작 인자 사진 장부에 알린다(`EditorUICapture`)

        StartupCaptureSubsystem();
        ~StartupCaptureSubsystem() override;
        StartupCaptureSubsystem( const StartupCaptureSubsystem& )            = delete;
        StartupCaptureSubsystem& operator=( const StartupCaptureSubsystem& ) = delete;

        SubsystemInitResult     initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                    shutdownSubsystem() override {}
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        SubsystemHostMask       getSubsystemHost() const override;

        /** @brief 이 종류의 사진을 찍겠다고 알립니다. 첫 사진이 끝나기 전에 부릅니다. */
        void expectCapture( StartupCaptureKind kind ) { (void)_expectedMask.fetch_or( static_cast<uint32>( kind ) ); }
        /** @brief 이 종류의 사진을 모두 처리했음을 알립니다(@p bWritten 이 false 면 파일을 쓰지 못했다). 실패를 먼저 적는다 — 끝났다고 보인 순간 실패도 보여야 한다. */
        void reportCaptureFinished( StartupCaptureKind kind, bool bWritten );

        /**
         * @brief 프레임 끝(게임 스레드)입니다. `-gv_screenshot` 의 차례면 사진을 맡기고 맡긴 사진의 결과를 거둔다.
         * @return `-gv_screenshotQuit` 이 켜졌고 요청한 종류가 모두 끝났으면 그 결과의 종료 코드(`Passed` · `CaptureWriteFailed`), 아니면 `kEngineFrameRunning` 입니다.
         */
        [[nodiscard]] EngineFrameExit update();

    private:
        /** @brief `-gv_screenshot` 의 한 프레임입니다 — 맡긴 장의 결과를 거두고, 차례면 다음 장을 맡긴다. */
        void updateGameView();
        /** @brief 화면이 자리 잡았는지입니다(활성 씬이 플레이를 시작했고 로딩 화면 · 페이드 · 에셋 읽기가 끝났다 — `AutomationCaptureUtil::isReadyForCapture`). */
        [[nodiscard]] static bool isScreenStable();
        /** @brief 맡긴 장의 결과가 왔으면 거둔다. 아직이면 false 입니다. */
        [[nodiscard]] bool collectPendingShot();
        /** @brief 다음 장(번호 `_shotCount`)을 렌더 스레드에 맡긴다. 첨부를 찾지 못하면 그 장은 쓰지 못한 것으로 센다. */
        void requestNextShot( uint32 shotTotal );
        /** @brief 게임 뷰 사진을 끝낸다(남은 장이 없거나 기다림이 시한을 넘었다). */
        void finishGameView();

    private:
        atomic<uint32> _expectedMask;          ///< 찍겠다고 알린 종류
        atomic<uint32> _finishedMask;          ///< 끝을 알린 종류
        atomic<uint32> _failedMask;            ///< 한 장이라도 쓰지 못한 종류
        string         _pendingPath;           ///< 맡기고 결과를 기다리는 장의 경로(비면 맡긴 장이 없다)
        uint32         _pendingSerial;         ///< 맡기기 직전의 처리 수(`RenderThread::getCompletedScreenshotSerial`) — 바뀌면 그 장의 결과다
        uint32         _frameCounter;          ///< 첫 장까지 센 프레임(자리 잡음을 기다리면 자리 잡은 프레임부터 센다)
        uint32         _shotCount;             ///< 맡긴 장 수
        int64          _waitStartMicroseconds; ///< 처음 본 단조 시각(0 = 아직) — 자리 잡음 기다림의 시한을 잰다
        uint8          _bGameViewStarted  : 1; ///< 첫 프레임의 알림(`expectCapture`)을 했다
        uint8          _bGameViewFinished : 1; ///< 게임 뷰 사진을 끝냈다
        uint8          _bGameViewFailed   : 1; ///< 게임 뷰 사진 한 장이라도 쓰지 못했다
    };
} // namespace sw
