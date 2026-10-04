/**
 * @file EditorFontSetup.h
 * @brief 에디터 ImGui 폰트(본문 · 한글 · 아이콘)를 구성합니다.
 *
 * @details 폰트 구성만 ImGui 를 필요로 하므로 `EditorUtil` 과 따로 둡니다. 프로젝트 경로 해석이나 애셋 종류 판별 같은 UI 와
 *          무관한 유틸이 UI 라이브러리를 끌고 다니지 않아야 단위 테스트가 `EditorUtil` 을 링크할 수 있습니다.
 */
#pragma once

namespace sw::editor
{
    /** @brief 에디터 ImGui 폰트 구성 */
    class EditorFontSetup
    {
    public:
        /** @brief EditorToolDefaults 에 적힌 후보로 ImGui 본문 · 한글 · 아이콘 폰트를 구성합니다. */
        static void apply();
    };
} // namespace sw::editor
