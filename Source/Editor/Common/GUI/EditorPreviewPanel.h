/**
 * @file EditorPreviewPanel.h
 * @brief 미리보기 씬(숨은 월드)을 가진 툴 창의 기반 클래스입니다 — 머티리얼 · 프리팹 · 캐릭터 편집 창이 이것을 상속합니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/GUI/EditorPreviewViewport.h"
#include "Editor/Common/Workspace/EditorSelection.h"

#include "Engine/Scene/CommandStack.h"

namespace sw
{
    struct ObjectSnapshot;

    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorPreviewPanel
     * @brief 문서 패널(`EditorDocumentPanel`) + 미리보기 씬 뷰포트 + 창 범위 선택 · Undo 입니다.
     * @details 언리얼 `FAssetEditorToolkit` + `FPreviewScene`, 유니티 `EditorWindow` + `PreviewRenderUtility` 자리입니다(`docs/plans/EditorMultiWorld.md` 2.3).
     *
     *          **파생 창이 지킬 계약**
     *          - 내용을 그릴 때 먼저 `beginPreviewFrame()` 을 부른다. 처음 부를 때 씬을 만들고(`getPreviewPolicy` · `getPreviewEnvironment`)
     *            `populatePreviewScene` 을 한 번 부른다. 포커스가 바뀌면 초점 씬 · 창의 Undo 스택을 걸고 푼다.
     *          - 뷰포트 자리에서 `drawPreviewViewport( size )` 를 부른다. 이 함수를 부른 프레임만 그린다(가려진 탭은 그리지 않는다).
     *          - 미리보기 씬의 오브젝트 편집은 `recordPreviewModify` · `getPreviewCommandStack` 으로 **창의 스택**에 남긴다. Ctrl+Z 는 창이 포커스일 때
     *            이 스택만 되돌린다(`editor::getFocusedCommandStack`) — 메인 씬 기록은 그대로다. 창을 닫으면 스택도 사라진다(되돌리기 어려운 결정 4).
     *            문서 텍스트 편집(`notifyDocumentEdited`)은 지금처럼 에디터 전역 스택이다.
     *          - 선택은 창마다다(`getPreviewSelection`, 이 창의 씬 · 스택에 묶인 `EditorSelection`). 창이 포커스를 얻거나 선택이 바뀌면 인스펙터가
     *            그것을 겨눈다(`editor::getFocusedSelection`). 인스펙터의 값 편집은 이 창의 스택에 남는다.
     *          - 창을 내릴 때(`shutdown`) 씬 · 타깃 · 초점을 놓는다. 파생이 `shutdown` 을 재정의하면 기반의 것을 부른다.
     */
    class SW_EDITOR_API EditorPreviewPanel : public EditorDocumentPanel
    {
    public:
        ~EditorPreviewPanel() override;

        void shutdown( IRHIDevice* pRHIDevice ) override;

        /** @brief 미리보기 씬입니다. 아직 그린 적이 없으면 nullptr 입니다. */
        Scene* getPreviewScene() const { return _viewport.getScene(); }
        /** @brief 미리보기 뷰포트입니다(카메라 · 렌더 타깃). */
        EditorPreviewViewport&       getPreviewViewport() { return _viewport; }
        const EditorPreviewViewport& getPreviewViewport() const { return _viewport; }
        /** @brief 창 범위 Undo 스택입니다. */
        CommandStack& getPreviewCommandStack() { return _commandStack; }
        /** @brief 창 범위 선택입니다(미리보기 씬 · 창의 스택에 묶임). 씬을 만들기 전에는 묶이지 않은 빈 선택입니다. */
        EditorSelection&       getPreviewSelection() { return _selection; }
        const EditorSelection& getPreviewSelection() const { return _selection; }

        /**
         * @brief 미리보기 씬 오브젝트 @p obj 의 수정 전후 상태를 **창의 스택**에 남깁니다. 명령은 그 씬의 키를 기억하므로 활성 씬이 바뀌어도 이 씬에서 되돌린다.
         * @return 남겼으면 true 입니다(씬 매니저가 없으면 false).
         */
        bool recordPreviewModify( GameObject& obj, const ObjectSnapshot& before, const ObjectSnapshot& after, string_view label );

    protected:
        EditorPreviewPanel( EditorAssetType kind, bool bLoadOnOpen );

        /** @brief 씬의 틱 정책입니다. 기본은 틱만(물리 · 오디오 · 플레이 없음)입니다. */
        virtual ScenePreviewPolicy getPreviewPolicy() const;
        /** @brief 조명 환경 프리셋입니다. 기본은 방향광 + 바닥입니다. */
        virtual EditorPreviewEnvironment getPreviewEnvironment() const { return EditorPreviewEnvironment{}; }
        /** @brief 씬을 만든 직후 한 번 부릅니다. 파생이 미리보기 대상(구 · 프리팹 · 캐릭터)을 스폰합니다. */
        virtual void populatePreviewScene( Scene& /*scene*/ ) {}

        /** @brief 그리기 앞부분에서 부릅니다. 씬을 (처음이면) 만들고, 창 포커스에 따라 초점 씬 · Undo 스택을 걸거나 풉니다. */
        void beginPreviewFrame();
        /**
         * @brief 뷰포트를 @p size 로 그립니다. 창이 포커스 · 호버면 카메라 입력을 받습니다.
         * @param bOrbitBlocked 기즈모가 마우스를 가졌으면 true 입니다(`EditorPreviewGizmo::hasMouse`). 기즈모를 쓰지 않는 창은 false 입니다.
         */
        void drawPreviewViewport( const float2& size, bool bOrbitBlocked );

    private:
        /** @brief 초점 씬 · 창 스택을 이 창의 것으로 걸거나(@p bFocused) 이 창이 걸어 둔 것이면 풉니다. */
        void applyWindowFocus( bool bFocused );

        EditorPreviewViewport _viewport;
        CommandStack          _commandStack;
        EditorSelection       _selection;
        bool                  _bFocusApplied; ///< 이 창이 초점 씬 · 스택을 걸어 두었다
    };
} // namespace sw::editor
