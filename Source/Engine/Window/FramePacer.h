/**
 * @file FramePacer.h
 * @brief 앱 루프의 프레임 페이싱 상태(`FramePacingState`)와 그 상태대로 프레임 끝에서 기다리는 `FramePacer` 입니다.
 * @details 상태는 프레임마다 입력(실행 인자 · 넷 역할 · 창 이벤트가 적은 최소화 · 포커스 · 에디터의 다시 그릴 까닭)으로 다시 정한다. 어느 상태인지가 값으로 드러나야
 *          "배경인데 왜 전속인가" 를 감시 값(`Engine.FramePacingState`)으로 바로 본다 — 조건문 속 숨은 분기로 두지 않는다.
 *          전속 상태(Automation · Server · FullSpeed)는 모두 **실행 인자가 고른 명시 상태**다: `-scenario` · `-gv_profileFrames` · `-gv_netListenPort` ·
 *          `-gv_framePacing=FullSpeed`. 창 상태를 모를 때 전속으로 떨어지는 숨은 폴백은 없다 — 인자가 없으면 창 상태대로다.
 *          언리얼(`t.IdleWhenNotForeground` · 최소화 때 렌더 생략 · 에디터 비실시간 뷰포트)과 유니티(`Application.runInBackground` · 배경 `targetFrameRate` ·
 *          에디터의 입력 때만 다시 그리기)와 같은 방향이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class IWindow;

    /** @brief 이번 프레임을 어떤 빠르기로 도는지입니다. 감시 값 `Engine.FramePacingState` 가 이 번호를 그대로 낸다(번호를 바꾸면 시나리오도 고친다). */
    enum class FramePacingState : uint8
    {
        Foreground = 0, ///< 포커스를 가진 창 — 따로 묶지 않는다(VSync 설정만).
        Background,     ///< 보이지만 포커스가 없는 창 — `gv_backgroundFrameRate` 로 묶는다.
        Minimized,      ///< 최소화된 창 — 창 메시지가 올 때까지(최대 `FramePacer::kMinimizedWakeMilliseconds`) 잔다.
        Automation,     ///< 시나리오 · 프로파일 실행(실행 인자) — 포커스 · 최소화와 상관없이 전속이다.
        Server,         ///< 리슨 호스트(`-gv_netListenPort`) — 클라이언트가 이 프레임으로 상태를 받으므로 전속이다.
        FullSpeed,      ///< `-gv_framePacing=FullSpeed` 가 고정한 전속 — 에디터 멀티플레이가 띄운 클라이언트처럼 포커스 없이 정속이어야 하는 실행.
        EditorIdle,     ///< 에디터가 조용하다(다시 그릴 까닭 없는 프레임이 이어졌다) — 창 메시지가 올 때까지(최대 `kEditorIdleWakeMilliseconds`) 잔다.
    };

    /**
     * @brief 실행 인자로 고르는 페이싱 요청입니다(`-gv_framePacing=<이름>`). 기본은 `Auto` 입니다.
     * @details 리플렉션 enum 이라 명령줄 · 시나리오(`<Variable>`)가 이름으로 고른다.
     */
    ENUM()
    enum class FramePacingRequest : uint8
    {
        Auto,        ///< 실행 방식(시나리오 · 프로파일 · 리슨 호스트)과 창 상태대로
        FullSpeed,   ///< 늘 전속(`FramePacingState::FullSpeed`) — 에디터 멀티플레이의 자식 프로세스가 받는다
        WindowState, ///< 자동화 실행이어도 창 · 에디터 상태대로 — 시나리오가 배경 · 유휴 페이싱 자체를 볼 때
    };

    /** @brief 상태를 정하는 프레임 입력입니다. 앱 루프가 프레임마다 채운다. */
    struct FramePacingInputs
    {
        bool _bAutomationRun{ false }; ///< `-scenario`(녹화 포함) · `-gv_profileFrames` · `-gv_profileSeconds` 실행이다.
        bool _bListenHost{ false };    ///< 이 프로세스의 넷 세션이 서버다.
        bool _bMinimized{ false };     ///< `IWindow::isMinimized`
        bool _bFocused{ false };       ///< `IWindow::hasFocus`
        bool _bEditorQuiet{ false };   ///< 에디터가 이번 프레임에 다시 그릴 까닭이 없다(`ModuleFrameState::_bEditorQuiet`). 에디터가 없으면 false.
    };
} // namespace sw

namespace sw
{
    /**
     * @class FramePacer
     * @brief 프레임 시작 시각을 적고(`markFrameStart`), 프레임 입력이 모이면 상태를 정하고(`beginFrame`), 프레임 끝에 그 상태대로 기다립니다(`endFrame`).
     *        주 창 서비스(`MainWindowSubsystem`)가 하나 든다.
     */
    class SW_API FramePacer
    {
    public:
        /** @brief 최소화된 동안 한 번에 자는 최대 시간입니다. 표준 입력 `quit` · 개발 콘솔 명령을 이 주기로는 본다. */
        static constexpr uint32 kMinimizedWakeMilliseconds = 100;
        /** @brief 에디터가 조용한 동안 한 번에 자는 최대 시간입니다. 창 메시지가 아닌 변화(출력 로그 줄 · 툴팁 지연 · 표준 입력)도 이 주기로는 그려진다. */
        static constexpr uint32 kEditorIdleWakeMilliseconds = 100;
        /**
         * @brief 조용한 프레임이 이만큼 이어져야 잡니다.
         * @details 마지막 변화 뒤의 프레임이 화면에 닿게 하는 몫이다 — ImGui 는 포커스 · 창 배치를 다음 프레임에 반영하고, 렌더 스레드는 게임 스레드보다 한두 프레임 늦다.
         */
        static constexpr uint32 kEditorIdleSettleFrames = 3;

        /** @brief 입력에서 상태를 정합니다. 실행 인자가 고른 상태(FullSpeed · 자동화 · 서버)가 창 · 에디터 상태보다 앞선다. */
        [[nodiscard]] static FramePacingState resolve( const FramePacingInputs& inputs, FramePacingRequest request, uint32 editorQuietFrames );
        /**
         * @brief 배경 상한이 허락하는 다음 프레임 시작 시각(나노초)입니다.
         * @return @p frameRateLimit 이 0 이하면(상한 없음) @p frameStartNanoseconds 그대로입니다.
         */
        [[nodiscard]] static int64 computeBackgroundDeadlineNanoseconds( int64 frameStartNanoseconds, int32 frameRateLimit );

        /** @brief 프레임 맨 앞에서 부릅니다. 배경 상한이 재는 시작 시각입니다. */
        void markFrameStart();
        /** @brief 프레임 입력(에디터 UI 갱신 뒤)이 모이면 부릅니다. 조용한 프레임 수를 세고 `gv_framePacing` 과 함께 상태를 정합니다. */
        void beginFrame( const FramePacingInputs& inputs );
        /** @brief 프레임 끝에 부릅니다. 배경이면 상한 주기까지 자고, 최소화 · 에디터 유휴면 @p window 의 메시지를 기다립니다. 다른 상태는 바로 돌아옵니다. */
        void endFrame( IWindow& window ) const;

        /** @brief 이번 프레임의 상태입니다(`beginFrame` 이 정했다). */
        FramePacingState getState() const { return _state; }

    private:
        int64            _frameStartNanoseconds{ 0 };
        uint32           _editorQuietFrames{ 0 }; ///< 에디터가 조용했던 프레임이 몇 번 이어졌는지(`kEditorIdleSettleFrames` 에서 멈춘다)
        FramePacingState _state{ FramePacingState::Foreground };
    };
} // namespace sw
