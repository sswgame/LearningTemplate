/**
 * @file ConsoleViewModel.h
 * @brief 콘솔 창의 ViewModel — 창 밖에 닿는 명령(로그 줄을 IDE 에서 열기 · 로그 파일 폴더 열기)과 플레이 세션 반응(플레이 시작에 지우기 · 오류에서 멈추기)의 판정 값입니다. ImGui 를 모릅니다.
 * @details 로그 줄 · 거르기 · 묶기는 창이 든다(로그 싱크가 UI 스레드로 넘긴 스냅숏 — `ConsoleLogRows`). 여기 명령은 문서를 바꾸지 않아 되돌리기에 남지 않는다. `docs/agents/EditorUI.md` MVVM 계약.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct LogEntry;
} // namespace sw

namespace sw::editor
{
    /** @brief 플레이 세션의 지금 상태입니다(값만). */
    struct ConsolePlayState
    {
        bool _bStopped{ true };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
        bool _bRunning{ false }; ///< 플레이 중이고 멈추지 않았다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다)
    };

    /** @brief 콘솔 창의 ViewModel 입니다(상태 없음). */
    struct ConsoleViewModel
    {
        /** @brief 지금 플레이 세션 상태를 읽습니다(`EditorPlaySession`). */
        static ConsolePlayState gatherPlayState();
        /** @brief 플레이를 멈춥니다(오류에서 멈추기 — 유니티 Error Pause). */
        static void pausePlay();
        /** @brief 로그 줄 @p entry 의 소스 위치(메시지의 `파일(줄)` 또는 줄의 파일 · 줄)를 IDE 에서 엽니다. 위치가 없으면 아무것도 하지 않고, 실패는 경고로 남는다. */
        static void openEntryInIde( const LogEntry& entry );
        /** @brief @p path(로그 파일 또는 폴더)를 파일 탐색기로 엽니다. 못 띄우면 경고가 남는다. */
        static void showInFileExplorer( const string& path );
    };
} // namespace sw::editor
