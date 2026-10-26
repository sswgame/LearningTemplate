/**
 * @file FramePacer.h
 * @brief 앱 루프의 프레임 페이싱 상태(`FramePacingState`)와 그 상태대로 프레임 끝에서 기다리는 `FramePacer` 입니다.
 * @details 상태는 프레임마다 입력(실행 방식 · 넷 역할 · 창 이벤트가 적은 최소화 · 포커스)으로 다시 정한다. 어느 상태인지가 값으로 드러나야
 *          "배경인데 왜 전속인가" 를 탐침(`Engine.FramePacingState`)으로 바로 본다 — 조건문 속 숨은 분기로 두지 않는다.
 *          언리얼(`t.IdleWhenNotForeground` · 최소화 때 렌더 생략)과 유니티(`Application.runInBackground` · 배경 `targetFrameRate`)와 같은 방향이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class IWindow;

    /** @brief 이번 프레임을 어떤 빠르기로 도는지입니다. 탐침 `Engine.FramePacingState` 가 이 번호를 그대로 낸다(번호를 바꾸면 시나리오도 고친다). */
    enum class FramePacingState : uint8
    {
        Foreground = 0, ///< 포커스를 가진 창 — 따로 묶지 않는다(VSync 설정만).
        Background,     ///< 보이지만 포커스가 없는 창 — `gv_backgroundFrameRate` 로 묶는다.
        Minimized,      ///< 최소화된 창 — 창 메시지가 올 때까지(최대 `FramePacer::kMinimizedWakeMilliseconds`) 잔다.
        Automation,     ///< 시나리오 · 프로파일 실행(실행 인자) — 포커스 · 최소화와 상관없이 전속이다.
        Server,         ///< 리슨 호스트(`-gv_netListenPort`) — 클라이언트가 이 프레임으로 상태를 받으므로 전속이다.
    };

    /** @brief 상태를 정하는 입력입니다. 앱 루프가 프레임마다 채운다. */
    struct FramePacingInputs
    {
        bool _bAutomationRun{ false }; ///< `-scenario`(녹화 포함) · `-gv_profileFrames` · `-gv_profileSeconds` 실행이다.
        bool _bListenHost{ false };    ///< 이 프로세스의 넷 세션이 서버다.
        bool _bMinimized{ false };     ///< `IWindow::isMinimized`
        bool _bFocused{ false };       ///< `IWindow::hasFocus`
    };
} // namespace sw

namespace sw
{
    /**
     * @class FramePacer
     * @brief 프레임 시작에 상태를 정하고(`beginFrame`), 프레임 끝에 그 상태대로 기다립니다(`endFrame`). 주 창 서비스(`MainWindowSubsystem`)가 하나 든다.
     */
    class SW_API FramePacer
    {
    public:
        /** @brief 최소화된 동안 한 번에 자는 최대 시간입니다. 표준 입력 `quit` · 개발 콘솔 명령을 이 주기로는 본다. */
        static constexpr uint32 kMinimizedWakeMilliseconds = 100;

        /** @brief 입력에서 상태를 정합니다. 실행 인자가 고른 상태(자동화 · 서버)가 창 상태보다 앞선다. */
        [[nodiscard]] static FramePacingState resolve( const FramePacingInputs& inputs );
        /**
         * @brief 배경 상한이 허락하는 다음 프레임 시작 시각(나노초)입니다.
         * @return @p frameRateLimit 이 0 이하면(상한 없음) @p frameStartNanoseconds 그대로입니다.
         */
        [[nodiscard]] static int64 computeBackgroundDeadlineNanoseconds( int64 frameStartNanoseconds, int32 frameRateLimit );

        /** @brief 프레임 시작에 부릅니다. 상태를 정하고 시작 시각을 적습니다. */
        void beginFrame( const FramePacingInputs& inputs );
        /** @brief 프레임 끝에 부릅니다. 배경이면 상한 주기까지 자고, 최소화면 @p window 의 메시지를 기다립니다. 다른 상태는 바로 돌아옵니다. */
        void endFrame( IWindow& window ) const;

        /** @brief 이번 프레임의 상태입니다(`beginFrame` 이 정했다). */
        FramePacingState getState() const { return _state; }

    private:
        int64            _frameStartNanoseconds{ 0 };
        FramePacingState _state{ FramePacingState::Foreground };
    };
} // namespace sw
