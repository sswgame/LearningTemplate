/**
 * @file EditorFontSetup.h
 * @brief 에디터 ImGui 폰트(본문·한글·아이콘) 구성
 *
 * @details EditorUtil 에 같이 들어 있었는데, 폰트 구성만 ImGui 를 필요로 해서 **EditorUtil 전체가
 *          ImGui 에 묶여 있었다**. 프로젝트 경로 해석이나 애셋 종류 판별 같은 비-UI 유틸까지
 *          UI 라이브러리를 끌고 다닐 이유가 없다(단위 테스트가 EditorUtil 을 링크하지 못하는
 *          원인이기도 했다). UI 에 속한 것은 Gui/ 로 옮긴다.
 */
#pragma once

namespace sw::editor
{
    /** @brief 에디터 ImGui 폰트 구성 */
    class EditorFontSetup
    {
    public:
        /** @brief EditorData 후보로 ImGui 본문·한글·아이콘 폰트를 구성합니다. */
        static void apply();
    };
} // namespace sw::editor
