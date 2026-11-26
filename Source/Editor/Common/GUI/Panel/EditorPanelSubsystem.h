/**
 * @file EditorPanelSubsystem.h
 * @brief `IEditorPanelSubsystem` 구현 — 패널 매니저와 도킹 레이아웃으로 패널 열기 · 최대화 · 도구 문서 저장을 합니다.
 */
#pragma once
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/IEditorPanelSubsystem.h"

namespace sw::editor
{
    class EditorContext;
    class EditorPanelManager;

    /**
     * @class EditorPanelSubsystem
     * @brief 아래 층이 보는 패널 진입점의 GUI 층 구현입니다. `EditorContext` 가 패널 매니저 뒤에 만들고 앞에 지웁니다.
     */
    class EditorPanelSubsystem final : public IEditorPanelSubsystem
    {
    public:
        /** @brief Shift+Space 최대화와 플레이 때 최대화가 함께 쓰는 임시 레이아웃 이름입니다(에디터 상태 폴더의 `Temp`). */
        static constexpr const utf8* kMaximizeLayoutName = "maximize-restore";

        EditorPanelSubsystem( EditorContext& context, EditorPanelManager& panelManager );

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "EditorPanelSubsystem";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        void                         resetToFreshEditor() override {} // 패널 매니저로 가는 창구라 자기 상태가 없다
        span<const utf8* const>      getSubsystemDependencies() const override;

        bool               focusPanel( EditorPanelID panelID ) override;
        [[nodiscard]] bool maximizePanel( EditorPanelID keepPanelID ) override;
        [[nodiscard]] bool restoreMaximizedLayout() override;
        [[nodiscard]] bool saveFocusedDirtyDocument() override;
        [[nodiscard]] bool saveAllDirtyDocuments() override;
        void               discardAllDirtyDocuments() override;
        uint32             countDirtyDocuments() const override;
        void               collectOpenDocuments( vector<EditorOpenDocument>& outList ) const override;
        void               collectPanelCommandStacks( uint32& outFocusedWindowID, vector<EditorPanelCommandStack>& outList ) const override;

    private:
        EditorContext&      _context;
        EditorPanelManager& _panelManager;
    };
} // namespace sw::editor
