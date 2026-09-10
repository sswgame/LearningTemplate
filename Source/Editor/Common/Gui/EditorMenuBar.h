/**
 * @file EditorMenuBar.h
 * @brief 에디터 메인 메뉴바 · File/Panel 요청 처리 (단축키는 EditorCommandGui)
 */
#pragma once

namespace sw::editor
{
    class EditorDockLayout;

    /** @brief 에디터 메인 메뉴바 · File/Panel 요청 처리 (단축키는 EditorCommandGui) */
    class EditorMenuBar
    {
    public:
        /** @brief 테마 설정 대화상자를 메뉴바 및 독스페이스보다 먼저 그려 스타일 변경사항을 프레임 지연 없이 즉시 반영합니다. */
        static void drawThemeDialog();
        /** @brief 다음 프레임부터 테마 설정 대화상자를 엽니다 (editor.themeSettings 커맨드가 부릅니다). */
        static void openThemeDialog();

        /** @brief File / Edit / Assets / Panel 메뉴와 RHI 상태줄을 그립니다. */
        static void draw( EditorDockLayout& dockLayout );

        /** @brief Workspace에 쌓인 패널 열기 요청을 소비합니다. */
        static void processOpenPanelRequests();

        /** @brief File 메뉴가 고른 씬 경로를 메인 스레드에서 로드합니다. */
        static void processPendingSceneLoad();
        /** @brief 씬 세대 동기화와 미저장 확인 모달을 처리합니다. */
        static void processSceneSession();

    private:
        // draw() 가 순서대로 부르는 조각들. 메뉴 하나가 곧 함수 하나다.
        /** @brief 새 씬·열기·저장 등 File 메뉴를 그립니다. */
        static void drawFileMenu();
        /** @brief 실행 취소·다시 실행 등 Edit 메뉴를 그립니다. */
        static void drawEditMenu();
        /** @brief 라이브 코딩 빌드 관련 Build 메뉴를 그립니다. */
        static void drawBuildMenu();
        /** @brief 애셋 종류별 도구 패널을 여는 Assets 메뉴를 그립니다. */
        static void drawAssetsMenu();
        /** @brief 패널 표시 토글과 도킹 레이아웃 초기화를 담은 Panel 메뉴를 그립니다. */
        static void drawPanelMenu( EditorDockLayout& dockLayout );
        /** @brief 메뉴바 오른쪽의 빌드 상태와 RHI·FPS 표시를 그립니다. */
        static void drawStatusArea();
    };
} // namespace sw::editor
