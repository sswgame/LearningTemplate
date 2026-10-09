/**
 * @file EditorWindowTitle.h
 * @brief 에디터 창 제목(`<게임> — <씬>[*] — SW Editor`)을 만듭니다.
 * @details ImGui 없이 글자만 만들어 `Test/EditorTest` 가 시험합니다. 셸(`ImGuiEditor::updateUi`)이 프레임마다 `IWindow::setTitle` 에 겁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw::editor
{
    /**
     * @struct EditorWindowTitleUtil
     * @brief 창 제목 만들기입니다(언리얼 `MyGame - Lvl_Main* - Unreal Editor`, 유니티 `MyGame - Main.unity* - Unity` 자리).
     */
    struct EditorWindowTitleUtil
    {
        /** @brief 제목 끝의 에디터 이름입니다. */
        static constexpr const utf8* kEditorName = "SW Editor";
        /** @brief 씬 파일 접미사입니다. 제목에는 이것을 뗀 이름을 씁니다. */
        static constexpr const utf8* kSceneSuffix = ".scene.xml";

        /**
         * @brief `<게임> — <씬>[*] — SW Editor` 를 씁니다. 씬이 없으면 `<게임> — (no scene) — SW Editor`.
         * @param gameName 게임 프리셋의 창 제목(`GameConfig::_windowTitle`)
         * @param scenePath 활성 씬의 소스 경로(저장한 적 없는 씬이면 씬 이름). 마지막 조각에서 `.scene.xml` 을 뗀 이름을 쓴다
         * @param bDirty 씬 또는 열린 문서 중 미저장이 있으면 true — 씬 이름 뒤에 `*`
         * @param pPlayState Play · Simulate 중이면 "Playing" · "Simulating" 같은 글을 끝에 괄호로 붙인다. 편집 중이면 nullptr
         */
        static string makeTitle( string_view gameName, string_view scenePath, bool bDirty, const utf8* pPlayState );
        /** @brief 제목이 미저장 표시(`*`)를 달고 있으면 true 입니다(탐침 `Editor.WindowTitleDirty`). */
        static bool isDirtyTitle( string_view title );
    };
} // namespace sw::editor
