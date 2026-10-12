/**
 * @file DevConsoleHost.h
 * @brief 게임 창의 개발 콘솔(`~`)과 표준 입력 명령 채널의 App 쪽 — 만들기, 시작 명령 실행, 프레임 갱신, 종료입니다. **Shipping 에는 콘솔이 없습니다.**
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class DevConsoleController;
    class InputMap;
    class IWindow;
    class StandardInputReader;

    /**
     * @class DevConsoleHost
     * @brief App 이 값 멤버로 들고 프레임 순서에 맞춰 직접 부릅니다. Shipping 에서는 모든 함수가 아무것도 하지 않습니다.
     * @details 콘솔은 에디터가 없을 때만 입력을 받습니다(에디터는 Output Log 입력 줄이 같은 콘솔이다). 키는 셸 InputMap 의 콘솔 액션으로 받습니다(`update`).
     *          `-gv_devConsoleExec` 의 명령은 시작 씬이 열린 뒤 이 콘솔로 돌립니다(에디터가 있어도). `-gv_standardInputCommands=1` 이면 표준 입력의 줄도
     *          이 콘솔로 보냅니다 — 부모 프로세스(에디터의 멀티 프로세스 재생)가 클라이언트를 제어하는 채널입니다.
     */
    class DevConsoleHost
    {
    public:
        DevConsoleHost();
        ~DevConsoleHost();

        /** @brief 콘솔 오버레이를 만들고 시작 명령(`-gv_devConsoleExec`)을 등록해 둡니다. 루프에 들어가기 직전에 부릅니다. */
        void initialize( IWindow* pWindow, bool bEnableEditor );
        /**
         * @brief 콘솔과 표준 입력 읽기 스레드를 내립니다.
         * @details 콘솔은 입력 관리자에 글자 콜백을 등록하고 키보드 포커스를 잡으므로 엔진(입력 관리자)보다 먼저, 콘솔 창은 게임 창이 소유한 팝업이라
         *          게임 창보다도 먼저 부릅니다.
         */
        void shutdown();

        /** @brief 시작 씬이 열렸으면 등록해 둔 시작 명령을 한 번 실행합니다. 프레임마다 부릅니다. */
        void runPendingExec();
        /** @brief 셸 InputMap 의 이번 프레임 액션으로 콘솔을 갱신합니다(에디터가 없을 때). 셸 액션을 갱신한 뒤에 부릅니다. */
        void update( InputMap* pShellMap );
        /**
         * @brief `-gv_standardInputCommands=1` 로 실행했으면 표준 입력에서 온 줄을 처리합니다. `quit` 과 `stop` 은 종료이고, 나머지는 개발 콘솔 명령입니다.
         * @return 종료를 요청받았으면 true 를 반환합니다. Shipping 에서는 항상 false 입니다.
         * @details 창 닫기를 보낼 수 없는 부모나 콘솔이 없는 부모도 이 경로로 클라이언트를 정상 종료합니다.
         */
        [[nodiscard]] bool pollStandardInputCommands();

    private:
#if !defined( SW_SHIPPING )
        unique_ptr<DevConsoleController> _devConsoleController;
        unique_ptr<StandardInputReader>  _standardInput;            ///< `-gv_standardInputCommands=1` 일 때만 있다. 부모 프로세스의 명령 채널이다.
        vector<string>                   _listStandardInputCommand; ///< 표준 입력에서 꺼낸 줄(프레임마다 재사용)

        uint8                  _bEnableEditor : 1;
        uint8                  _bExecPending  : 1; ///< `-gv_devConsoleExec` 를 아직 돌리지 않았다.
        [[maybe_unused]] uint8 _reserved      : 6;
#endif
    };
} // namespace sw
