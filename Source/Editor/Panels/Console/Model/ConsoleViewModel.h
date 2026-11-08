/**
 * @file ConsoleViewModel.h
 * @brief Output Log 창의 ViewModel — 플레이 세션 상태 읽기 · 멈추기(Clear on Play · Error Pause)와 로그 파일 보이기를 받습니다. ImGui 를 모릅니다.
 * @details 패널(`ConsolePanel`)은 작업 공간 · 커맨드 헤더(`EditorPlaySession` · `EditorAssetFileCommands`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약).
 *          플레이 멈춤 · 탐색기 열기는 문서를 바꾸지 않아 되돌리기에 남지 않는다(유니티 Console 의 Error Pause 와 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief Output Log 창의 ViewModel 입니다. 파일 머리말 참고. 상태가 없다. */
    struct ConsoleViewModel
    {
        /** @brief 플레이 세션이 멈춰(정지) 있으면 true 입니다. */
        static bool isPlayStopped();
        /** @brief 플레이 중이고 일시 정지가 아니면 true 입니다. */
        static bool isPlayRunning();
        /** @brief 플레이를 일시 정지합니다(Error Pause). */
        static void pausePlay();
        /** @brief @p absolutePath(로그 파일 또는 폴더)를 탐색기로 보입니다. 못 띄우면 경고가 남고 false 입니다. */
        static bool showInFileExplorer( string_view absolutePath );
    };
} // namespace sw::editor
