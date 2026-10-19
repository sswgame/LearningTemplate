/**
 * @file PrefabEditorPanel.h
 * @brief 프리팹을 자기 미리보기 씬에서 편집하는 창입니다 — 메인 씬을 더럽히지 않고, 창마다 Undo 스택 · 선택을 가집니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"
#include "Editor/Common/GUI/EditorPreviewPanel.h"
#include "Editor/Viewport/EditorPreviewGizmo.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class PrefabEditorPanel
     * @brief 프리팹 문서 하나를 미리보기 씬에 스폰해 편집합니다(언리얼 블루프린트 편집기 · 유니티 프리팹 모드 자리).
     * @details `PrefabPanel`(메인 씬에 놓인 인스턴스의 오버라이드 도구)과 따로다 — 이 창은 메인 씬을 보지 않는다.
     *          문서 = 열린 프리팹이고 그 루트가 격리 스택(`EditorPrefabIsolation`)의 맨 아래 단계다. 중첩 프리팹을 열면 단계를 쌓아 그 부분만 보이게 하고(문서 루트 밖의
     *          조명 · 바닥은 그대로), 나오면 되돌린다. 저장은 지금 단계의 루트를 그 단계의 프리팹 파일에 쓴다.
     */
    class PrefabEditorPanel : public EditorPreviewPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "prefab_window";

        PrefabEditorPanel();

        /** @brief 프리팹 창입니다(닫혔어도 만들어져 있다). 컨텍스트나 창이 없으면 nullptr 입니다. */
        [[nodiscard]] static PrefabEditorPanel* findWindow();
        /** @brief 창이 열려 문서를 격리 중이면 그 창입니다. 아니면 nullptr 입니다. */
        [[nodiscard]] static PrefabEditorPanel* findIsolatingWindow();
        /**
         * @brief 프리팹 창을 열어 @p prefabPath 를 문서로 보여 줍니다. 다른 문서가 열려 있으면 바꿉니다(미저장 편집은 문서 패널이 확인한다).
         * @details 활성 씬은 건드리지 않는다 — 씬이 더러워도 열 수 있다. 창이 없거나 플레이 중이면 false 입니다.
         */
        [[nodiscard]] static bool openWindow( string_view prefabPath );

        /**
         * @brief X 로 닫으려는 순간 미저장 확인을 그려야 하면 true 입니다.
         * @details ImGui 는 X 를 누르면 그 프레임에 열림 표시만 내리므로, 열림이 내려갔는데 편집이 남았거나 이미 확인 중이면 묻는다.
         *          결정을 한 곳에 모아 두어 닫는 길(X)이 확인 없이 문서를 잃는 일이 없게 한다.
         */
        [[nodiscard]] static constexpr bool shouldAskBeforeClosing( bool bOpen, bool bDirty, bool bConfirming )
        {
            return bConfirming || ( bOpen == false && bDirty );
        }

        /** @brief 창 제목은 종류 테이블(`EditorAssetTypeRegistry`)의 프리팹 패널 제목이다 — 콘텐츠 브라우저가 이 제목으로 연다. 오버라이드 도구(`PrefabPanel`)는 따로 둔 제목을 쓴다. */
        float2 getInitialPanelSize() const override { return float2{ 560.0f, 520.0f }; }

        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        /** @brief 저장하지 않고 닫을 때 쓴다 — 디스크의 프리팹으로 문서 루트를 다시 세워 편집을 버린다. */
        void revertDocument() override;

        /** @brief 격리 스택입니다(읽기). */
        const EditorPrefabIsolation& getIsolation() const { return _isolation; }
        /** @brief 이 창이 프리팹을 격리 중이면(문서가 열려 루트가 있으면) true 입니다. */
        bool isIsolating() const { return _isolation.isActive(); }
        /** @brief 문서 프리팹 안의 @p prefabPath 중첩 프리팹을 격리하라고 요청합니다. 씬이 없을 수도 있어 다음 그리기에서 쌓습니다. */
        void requestNestedIsolation( string_view prefabPath ) { _pendingNestedPath = string{ prefabPath }; }

        /**
         * @brief 지금 단계를 나갑니다. @p bSaveToPrefab 이면 먼저 지금 단계의 루트를 프리팹 파일에 씁니다. 쓰지 못하면 나가지 않고 false 입니다.
         * @details 중첩 단계면 한 단계 내려가고, 문서 단계면 창을 닫습니다(문서 루트는 씬에 남아 다시 열면 그대로다).
         */
        bool exitIsolation( bool bSaveToPrefab );

        /** @brief 문서 루트를 (다시) 스폰합니다 — 미리보기 씬이 있어야 하고 문서가 열려 있어야 합니다. 창 테스트가 직접 부릅니다. */
        bool rebuildDocumentRoot();
        /** @brief 중첩 프리팹 @p prefabPath 를 격리합니다. 지금 단계 아래에서 그 인스턴스를 찾고 없으면 스폰합니다. 쌓았으면 true 입니다. */
        bool enterNestedIsolation( string_view prefabPath );

    private:
        ToolAssetLoadResult loadDocument() override;
        void                populatePreviewScene( Scene& scene ) override;

        /** @brief 미리보기 씬의 매니저입니다. 씬이 아직 없으면 nullptr 입니다. */
        GameObjectManager* findPreviewManager() const;
        /** @brief 문서가 바뀌었으면 문서 루트를 다시 세웁니다. */
        void syncDocumentRoot();
        /** @brief 창의 편집 기록이 늘었거나 줄었으면 미저장 표시를 합니다. */
        void trackEdits();
        void drawIsolationBar();
        /** @brief X 로 닫기 전의 Save / Don't Save / Cancel 확인을 그립니다. 선택이 나면 창을 닫거나(Save · Don't Save) 열어 둡니다(Cancel · 저장 실패). */
        void drawCloseConfirm();
        void drawChildTree( GameObject& object, int32 depth );

        EditorPrefabIsolation _isolation;
        EditorPreviewGizmo    _gizmo;
        string                _pendingNestedPath;
        string                _spawnedDocumentPath; ///< 문서 루트를 마지막으로 스폰한 프리팹 경로
        string                _status;
        bool                  _bConfirmClose;    ///< X 로 닫으려다 미저장 확인을 기다리는 중
        size_t                _seenCommandCount; ///< 지난 그리기의 창 Undo 스택 길이 — 달라지면 문서를 고친 것이다
    };
} // namespace sw::editor
