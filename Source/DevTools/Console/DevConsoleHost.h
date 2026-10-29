/**
 * @file DevConsoleHost.h
 * @brief 게임 창의 개발 콘솔(`~`)과 표준 입력 명령 채널 — 만들기, 시작 명령 실행, 프레임 갱신, 종료입니다(개발 도구 `DevTools`, 배포 빌드는 짓지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Console/StandardInputCommands.h"

namespace sw
{
    class DevConsoleController;
    class InputMap;

    /**
     * @class DevConsoleHost
     * @brief 개발 도구 진입점(`DevToolsHost`)이 들고 프레임 관찰자 훅에서 부릅니다.
     * @details 콘솔은 에디터가 없을 때만 입력을 받습니다(에디터는 Output Log 입력 줄이 같은 콘솔이다). 키는 셸 InputMap 의 콘솔 액션으로 받습니다(`update`).
     *          `-gv_devConsoleExec` 의 명령은 시작 씬이 열린 뒤 이 콘솔로 돌립니다(에디터가 있어도). `-gv_standardInputCommands=1` 이면 표준 입력의 줄도
     *          이 콘솔로 보냅니다 — 부모 프로세스(에디터의 멀티 프로세스 재생)가 클라이언트를 제어하는 채널입니다.
     */
    class DevConsoleHost
    {
    public:
        DevConsoleHost();
        ~DevConsoleHost();

        /** @brief 콘솔 오버레이를 만들고 시작 명령(`-gv_devConsoleExec`)을 등록해 둡니다. 엔진이 선 뒤 · 루프에 들어가기 전에 부릅니다. */
        void initialize( bool bEnableEditor );
        /**
         * @brief 콘솔과 표준 입력 읽기 스레드를 내립니다.
         * @details 콘솔은 입력 관리자에 글자 콜백을 등록하고 키보드 포커스를 잡으며 UI 화면을 열므로 엔진보다 먼저 부릅니다.
         */
        void shutdown();

        /** @brief 시작 씬이 열렸으면 등록해 둔 시작 명령을 한 번 실행합니다. 프레임마다 부릅니다. */
        void runPendingExec();
        /** @brief 셸 InputMap 의 이번 프레임 액션으로 콘솔을 갱신합니다(에디터가 없을 때). 셸 액션을 갱신한 뒤에 부릅니다. */
        void update( InputMap* pShellMap );
        /**
         * @brief `-gv_standardInputCommands=1` 로 실행했으면 표준 입력에서 온 줄을 처리합니다(`StandardInputCommands` — 종료 낱말은 그쪽이 안다). 나머지 줄은 개발 콘솔 명령입니다.
         * @return 종료를 요청받았으면 true 를 반환합니다.
         * @details 창 닫기를 보낼 수 없는 부모나 콘솔이 없는 부모도 이 경로로 클라이언트를 정상 종료합니다.
         */
        [[nodiscard]] bool pollStandardInputCommands();

    private:
        /** @brief 표준 입력의 종료 아닌 줄을 개발 콘솔에 넘깁니다. */
        void submitStandardInputCommand( string_view command );

    private:
        unique_ptr<DevConsoleController> _devConsoleController;
        StandardInputCommands            _standardInput; ///< `-gv_standardInputCommands=1` 일 때만 시작한다. 부모 프로세스의 명령 채널이다.

        uint8                  _bEnableEditor : 1;
        uint8                  _bExecPending  : 1; ///< `-gv_devConsoleExec` 를 아직 돌리지 않았다.
        [[maybe_unused]] uint8 _reserved      : 6;
    };
} // namespace sw
