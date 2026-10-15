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
        /**
         * @brief EditorToolDefaults 에 적힌 후보로 ImGui 본문 · 한글 · 아이콘 폰트를 구성합니다.
         * @return 테마 글자 크기를 읽고 본문 · 한글 · 아이콘 글꼴을 모두 찾았으면 true. 하나라도 없으면 무엇이 없는지 오류로 남기고 false 다 —
         *         ImGui 기본 글꼴로 대신하지 않는다(글꼴 후보가 데이터이므로 고칠 곳은 editortooldefaults.json 이다).
         */
        [[nodiscard]] static bool apply();
    };
} // namespace sw::editor
