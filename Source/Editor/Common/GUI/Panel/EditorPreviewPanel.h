/**
 * @file EditorPreviewPanel.h
 * @brief 미리보기 씬(숨은 월드)을 가진 툴 창의 기반 클래스입니다. 머티리얼, 프리팹, 캐릭터 편집 창이 이것을 상속합니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw
{
    struct ObjectSnapshot;

    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorPreviewPanel
     * @brief 문서 패널(`EditorDocumentPanel`)에 미리보기 씬 뷰포트와 창 범위의 선택, Undo 를 더한 기반 클래스입니다.
     * @details 언리얼의 `FAssetEditorToolkit` 과 `FPreviewScene`, 유니티의 `EditorWindow` 와 `PreviewRenderUtility` 에 해당합니다.
     *
     *          **파생 창이 지킬 계약**
     *          - 내용을 그릴 때 먼저 `beginPreviewFrame()` 을 부른다. 처음 부를 때 씬을 만들고(`getPreviewPolicy`, `getPreviewEnvironment`)
     *            `populatePreviewScene` 을 한 번 부른다. 포커스가 바뀌면 초점 씬과 창의 Undo 스택을 등록하거나 해제한다.
     *          - 뷰포트 위치에서 `drawPreviewViewport( size )` 를 부른다. 이 함수를 부른 프레임만 그리므로, 가려진 탭은 그리지 않는다.
     *          - 미리보기 씬의 오브젝트 편집은 `recordPreviewModify` 와 `getPreviewCommandStack` 으로 **창의 스택**에 남긴다. 창이 포커스일 때
     *            Ctrl+Z 는 이 스택만 되돌리고(`editor::getFocusedCommandStack`), 메인 씬 기록은 그대로 둔다. 창을 닫으면 스택도 사라진다
     *            (되돌리기 어려운 결정 4). 문서 텍스트 편집(`notifyDocumentEdited`)은 지금처럼 에디터 전역 스택에 남는다.
     *          - 선택은 창마다 따로 있다(`getPreviewSelection`, 이 창의 씬과 스택에 묶인 `EditorSelection`). 창이 포커스를 얻거나 선택이 바뀌면
     *            인스펙터가 그 선택을 대상으로 삼는다(`editor::getFocusedSelection`). 인스펙터의 값과 구조 편집, 전역 명령(삭제, 정렬 등)의 기록은
     *            이 창의 스택에 남는다. `EditorTransaction` 이 오브젝트가 있는 씬으로 스택을 고르기 때문이다.
     *          - 창 스택이 늘거나 줄면 기반 클래스가 문서를 dirty 로 표시한다(`syncDocumentDirty`, 닫기 확인보다 먼저 돈다). 그래서 파생 창이
     *            미리보기 씬 편집을 따로 알릴 필요가 없다. 저장에 성공하면 `acknowledgePreviewEdits`, 문서를 다시 만들면 `resetPreviewEdits` 를
     *            부른다. 닫기 확인은 `IEditorPanel::requestClose` 가 한다.
     *          - 창을 종료할 때(`shutdown`) 씬, 렌더 타깃, 초점을 놓는다. 파생 창이 `shutdown` 을 재정의하면 기반 클래스의 것을 부른다.
     */
    class SW_EDITOR_API EditorPreviewPanel : public EditorDocumentPanel
    {
    public:
        ~EditorPreviewPanel() override;

        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 미리보기 씬입니다. 아직 그린 적이 없으면 nullptr 입니다. */
        Scene* getPreviewScene() const { return _viewport.getScene(); }
        /** @brief 카메라와 렌더 타깃을 가진 미리보기 뷰포트입니다. */
        EditorPreviewViewport&       getPreviewViewport() { return _viewport; }
        const EditorPreviewViewport& getPreviewViewport() const { return _viewport; }
        /** @brief 창 범위 Undo 스택입니다. */
        CommandStack& getPreviewCommandStack() { return _commandStack; }
        /** @brief 미리보기 씬과 창의 스택에 묶인 창 범위 선택입니다. 씬을 만들기 전에는 묶이지 않은 빈 선택입니다. */
        EditorSelection&       getPreviewSelection() { return _selection; }
        const EditorSelection& getPreviewSelection() const { return _selection; }

        /**
         * @brief 미리보기 씬 오브젝트 @p obj 의 수정 전후 상태를 **창의 스택**에 남깁니다. 명령은 그 씬의 키를 기억하므로 활성 씬이 바뀌어도 이 씬에서 되돌린다.
         * @return 남겼으면 true 입니다(씬 매니저가 없으면 false).
         */
        bool recordPreviewModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label );

        /** @brief 지금 연 문서가 @p assetPath(리소스 id)이고 읽기에 성공했으면 true 입니다. 읽지 못했으면 편집 사본은 앞 문서의 값이다. */
        bool hasDocument( string_view assetPath ) const { return getLoadedAssetPath() == assetPath && isDocumentLoaded() && isDocumentLoadFailed() == false; }
        /**
         * @brief 감시 값 본문 — 지금 연 문서가 `gv_editorWatchAsset`(리소스 id)이면 @p outValue 를 1, 아니면 0 으로 하고 true 를 돌려줍니다.
         *        전역 변수 관리자나 그 변수가 없으면 값을 만들 수 없어 false 입니다(관리자가 없으면 오류를 남긴다).
         */
        [[nodiscard]] bool readDocumentIsWatchAsset( float64& outValue ) const;

    protected:
        EditorPreviewPanel( const EditorAssetType& kind, bool bLoadOnOpen );

        /** @brief 씬의 틱 정책입니다. 기본은 틱만 돌리고 물리, 오디오, 플레이는 끕니다. */
        virtual ScenePreviewPolicy getPreviewPolicy() const;
        /** @brief 조명 환경 프리셋입니다. 기본은 방향광 + 바닥입니다. */
        virtual EditorPreviewEnvironment getPreviewEnvironment() const { return EditorPreviewEnvironment{}; }
        /** @brief 씬을 만든 직후 한 번 부릅니다. 파생 창이 미리보기 대상(구, 프리팹, 캐릭터)을 스폰합니다. */
        virtual void populatePreviewScene( Scene& /*scene*/ ) {}

        /** @brief 그리기 앞부분에서 부릅니다. 처음이면 씬을 만들고, 창 포커스에 따라 초점 씬과 Undo 스택을 등록하거나 해제합니다. */
        void beginPreviewFrame();
        /** @brief 창 Undo 스택의 지금 길이까지를 "저장했거나 버린 편집"으로 삼습니다. 파생 창이 저장에 성공한 직후 부르며, 그 뒤의 편집만 다시 dirty 로 만듭니다. */
        void acknowledgePreviewEdits() { _seenCommandCount = _commandStack.getCommandCount(); }
        /** @brief 창 Undo 스택을 비우고 편집 기록을 처음으로 돌립니다. 파생 창이 문서를 다시 만들 때 부릅니다. */
        void resetPreviewEdits();
        /**
         * @brief 뷰포트를 @p size 로 그립니다. 창이 포커스이거나 마우스가 올라가 있으면 카메라 입력을 받습니다.
         * @param bOrbitBlocked 기즈모가 마우스를 가졌으면 true 입니다(`EditorPreviewGizmo::hasMouse`). 기즈모를 쓰지 않는 창은 false 입니다.
         */
        void drawPreviewViewport( const float2& size, bool bOrbitBlocked );
        /**
         * @brief 창 Undo 스택이 늘었거나 줄었으면 문서를 dirty 로 표시합니다. 미리보기 씬의 편집은 모두 이 스택을 거치므로 파생 창이 따로 알릴 필요가 없습니다.
         * @details 편집 기록을 스택 밖(편집 모델)에도 두는 파생 창은 재정의해 그 상태를 더하고, 이 함수를 꼭 부른다(스택의 편집을 놓치지 않는다).
         */
        void syncDocumentDirty() override;

    private:
        /** @brief @p bFocused 면 초점 씬과 창 스택을 이 창의 것으로 등록하고, 아니면 이 창이 등록해 둔 것을 해제합니다. */
        void applyWindowFocus( bool bFocused );
        /** @brief 포커스를 가진 채 닫혀도 초점 씬과 창 스택을 해제합니다. 닫힌 창은 그려지지 않아 `beginPreviewFrame` 의 `applyWindowFocus( false )` 가 다시 돌지 않습니다. */
        void onPanelClosed() override { applyWindowFocus( false ); }

        EditorPreviewViewport _viewport;
        CommandStack          _commandStack;
        EditorSelection       _selection;
        bool                  _bFocusApplied;    ///< 이 창이 초점 씬과 스택을 등록해 두었다
        size_t                _seenCommandCount; ///< 지난 그리기의 창 Undo 스택 길이. 달라지면 문서를 고친 것이다
    };
} // namespace sw::editor
