/**
 * @file EditorMenuBar.h
 * @brief 에디터 메인 메뉴바 · 단축키 · File/Panel 요청 처리
 */
#pragma once

namespace sw::editor
{
    class EditorDockLayout;

    /** @brief 에디터 메인 메뉴바 · 단축키 · File/Panel 요청 처리 */
    class EditorMenuBar
    {
    public:
        /** @brief 테마 설정 대화상자를 메뉴바 및 독스페이스보다 먼저 그려 스타일 변경사항을 프레임 지연 없이 즉시 반영합니다. */
        static void drawThemeDialog();

        /** @brief File / Edit / Assets / Panel 메뉴와 RHI 상태줄을 그립니다. */
        static void draw( EditorDockLayout& dockLayout );

        /** @brief Ctrl+Z/Y/O, Ctrl+P/Space 등 메뉴와 같은 단축키를 처리합니다. */
        static void processHotkeys();

        /** @brief Workspace에 쌓인 패널 열기 요청을 소비합니다. */
        static void processOpenPanelRequests();

        /** @brief File 메뉴가 고른 씬 경로를 메인 스레드에서 로드합니다. */
        static void processPendingSceneLoad();
        /** @brief 씬 세대 동기화와 미저장 확인 모달을 처리합니다. */
        static void processSceneSession();
    };
} // namespace sw::editor
