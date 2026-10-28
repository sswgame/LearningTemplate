/**
 * @file EditorPanelHost.h
 * @brief `IEditorPanelHost` 구현 — 패널 매니저와 도킹 레이아웃으로 패널 열기 · 최대화 · 도구 문서 저장을 합니다.
 */
#pragma once
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/IEditorPanelHost.h"

namespace sw::editor
{
    class EditorContext;
    class EditorPanelManager;

    /**
     * @class EditorPanelHost
     * @brief 아래 층이 보는 패널 진입점의 GUI 층 구현입니다. `EditorContext` 가 패널 매니저 뒤에 만들고 앞에 지웁니다.
     */
    class EditorPanelHost final : public IEditorPanelHost
    {
    public:
        /** @brief Shift+Space 최대화와 플레이 때 최대화가 함께 쓰는 임시 레이아웃 이름입니다(에디터 상태 폴더의 `Temp`). */
        static constexpr const utf8* kMaximizeLayoutName = "maximize-restore";

        EditorPanelHost( EditorContext& context, EditorPanelManager& panelManager );

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorPanelHost";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;

        bool               focusPanel( EditorPanelID panelID ) override;
        [[nodiscard]] bool maximizePanel( EditorPanelID keepPanelID ) override;
        [[nodiscard]] bool restoreMaximizedLayout() override;
        [[nodiscard]] bool saveFocusedDirtyDocument() override;
        [[nodiscard]] bool saveAllDirtyDocuments() override;
        void               discardAllDirtyDocuments() override;
        uint32             countDirtyDocuments() const override;

    private:
        EditorContext&      _context;
        EditorPanelManager& _panelManager;
    };
} // namespace sw::editor
