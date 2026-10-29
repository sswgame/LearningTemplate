/**
 * @file ServerDebugControl.h
 * @brief 전용 서버의 개발 조종 — 개발 콘솔(표준 입력의 개발 명령 · gv_*), 디버그 채널(루프백 · 토큰, 개발 명령 원격 실행 S3), 일시정지 · 한 걸음(`pause` · `resume` · `step [N]`).
 * @details `ServerApp` 이 호스트 서비스 `DevConsole` · `DebugChannel` 로 세우고 내리는 협력 객체다(관리 채널 `ServerAdminChannel` 과 같은 자리 — 언리얼 전용 서버의
 *          `-pause` · 원격 콘솔과 같은 쓰임). 지금 틱 번호는 세울 때 받은 델리게이트로 묻는다 — 서버 루프를 모른다.
 *          Shipping 은 몸통이 비어 있다(명령을 받지 않고 틱을 붙잡지 않는다). `#if` 는 이 파일의 .cpp 에만 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    using ServerTickCountDelegate = Delegate<uint64()>;

    /**
     * @class ServerDebugControl
     * @brief 전용 서버의 개발 조종입니다. 파일 머리말 참고.
     */
    class ServerDebugControl
    {
    public:
        explicit ServerDebugControl( const ServerTickCountDelegate& tickCount );
        ~ServerDebugControl();
        ServerDebugControl( const ServerDebugControl& )            = delete;
        ServerDebugControl& operator=( const ServerDebugControl& ) = delete;

        /** @brief 표준 입력 `help` 에 덧붙일 글입니다(Dev 의 루프 명령 · 개발 명령). Shipping 은 빈 글입니다. */
        static const utf8* getHelpSuffix();

        /** @brief 개발 콘솔을 세웁니다(호스트 서비스 `DevConsole`). `-gv_serverStartPaused` 면 첫 틱 전에 멈춥니다. */
        void startConsole();
        /** @brief 개발 콘솔을 지웁니다. 채널을 먼저 닫는다(채널이 콘솔을 부른다). */
        void stopConsole();
        /** @brief 디버그 채널을 엽니다(`-gv_debugChannelPort` 가 0 이면 아무 일도 하지 않는다). 개발 콘솔 뒤에 연다. 열려고 했는데 못 열면 false 입니다(기동 실패). */
        [[nodiscard]] bool startChannel();
        /** @brief 채널을 닫습니다. 열지 않았어도 됩니다. */
        void stopChannel();
        /** @brief 받은 채널 줄을 이 스레드(게임 스레드)에서 실행합니다. 일시정지 중에도 부른다 — 멈춘 상태의 값을 읽는 것이 쓰임이다. */
        void pump();

        /** @brief 표준 입력 한 줄(일시정지 명령 · 개발 명령)을 처리했으면 true 입니다. 응답과 실패는 로그에 남긴다. */
        [[nodiscard]] bool executeCommand( string_view command );
        /** @brief 게임 틱을 돌지 않고 기다려야 하면 true 입니다(멈췄고 남은 `step` 이 없다). */
        bool isHoldingTick() const { return _bPaused && _pendingStepCount == 0; }
        bool isPaused() const { return _bPaused; }
        /** @brief 틱 하나를 끝냈습니다. 멈춘 채 `step` 으로 돈 틱이면 남은 수를 줄이고, 다 돌았으면 알립니다. */
        void onTickFinished();

    private:
        /** @brief 일시정지 명령(`pause` · `resume` · `step [N]`)이면 처리하고 true 입니다. Shipping 은 늘 false 입니다. */
        [[nodiscard]] bool executePauseCommand( string_view command );

    private:
        /** @brief 개발 콘솔 · 디버그 채널입니다. .cpp 가 정의한다 — Shipping 은 개발 콘솔(DevTools)이 없어 빈 묶음이다. */
        struct DevParts;

        ServerTickCountDelegate _tickCount;
        unique_ptr<DevParts>    _pDev;                  ///< `startConsole` 이 세우고 `stopConsole` 이 지운다
        uint32                  _pendingStepCount{ 0 }; ///< 일시정지 중 더 돌 틱 수(`step N`)
        bool                    _bPaused{ false };      ///< 게임 틱을 멈춘다(콘솔, 종료 신호, 네트워크 스레드는 돈다)
    };
} // namespace sw
