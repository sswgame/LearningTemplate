/**
 * @file PrefabEditorModel.h
 * @brief 프리팹 창의 모델 — 격리 스택(`EditorPrefabIsolation`), 문서 루트 스폰 · 다시 세우기, 중첩 프리팹 들어가기 요청 · 나가기, 지금 단계 저장, 상태 줄. ImGui 를 모릅니다.
 * @details 패널(`PrefabEditorPanel`)은 미리보기 씬 · 뷰포트 · 기즈모 · 창 Undo 스택을 쥐고(`EditorPreviewPanel`) 막대 · 자식 목록을 그리며, 문서 dirty · 창 닫기 · 알림을 맡는다 —
 *          래그돌 · VFX 미리보기 `Model/` 과 같은 선이다(언리얼 블루프린트 편집기의 툴킷 상태와 뷰포트, 유니티 프리팹 모드의 PrefabStage 와 Scene 뷰가 나뉜 것과 같다).
 *          씬의 매니저와 창 선택은 패널이 넘긴다(모델은 씬을 갖지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    class EditorSelection;

    /** @brief `PrefabEditorModel::exitFrame` 의 결과입니다. 패널이 문서 dirty · 창 닫기 · 알림을 이것으로 고른다. */
    enum class PrefabFrameExit : uint8
    {
        Failed,       ///< 단계가 없거나 저장하지 못했다 — 나가지 않았다
        DocumentStep, ///< 문서 단계다 — 스택은 그대로이고 패널이 창을 닫는다(문서 루트는 씬에 남는다)
        PoppedNested, ///< 중첩 단계를 하나 내려갔다
    };

    /**
     * @class PrefabEditorModel
     * @brief 프리팹 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PrefabEditorModel
    {
    public:
        PrefabEditorModel();

        const EditorPrefabIsolation& getIsolation() const { return _isolation; }
        const string&                getStatus() const { return _status; }

        /** @brief 새 미리보기 씬에는 문서 루트가 아직 없다 — 격리 스택과 스폰한 경로를 잊습니다. */
        void resetForNewScene();
        /** @brief 다음 `needsRebuild` 가 문서 루트를 다시 스폰하게 합니다(문서를 다시 읽었다). */
        void requestRebuild() { _spawnedDocumentPath.clear(); }
        /** @brief 문서 @p documentPath 의 루트를 아직 스폰하지 않았으면 true 입니다. */
        bool needsRebuild( string_view documentPath ) const { return string_view{ _spawnedDocumentPath } != documentPath; }
        /**
         * @brief 옛 문서 루트를 지우고 @p documentPath 의 루트를 @p manager 에 스폰해 격리 스택의 맨 아래 단계로 쌓고 고릅니다.
         * @details 실패해도 같은 경로로 프레임마다 다시 스폰하지 않게 스폰한 경로는 먼저 채운다(실패는 상태 줄에 남는다).
         * @return 스폰했으면 true 입니다.
         */
        bool rebuildRoot( GameObjectManager& manager, EditorSelection& selection, string_view documentPath );

        /** @brief 문서 프리팹 안의 @p prefabPath 중첩 프리팹을 격리하라고 요청합니다. 씬이 없을 수도 있어 `enterPendingNested` 가 쌓습니다. */
        void requestNested( string_view prefabPath ) { _pendingNestedPath = prefabPath; }
        /** @brief 요청한 중첩 프리팹이 있으면 격리합니다. 쌓지 못하면 상태 줄에 남깁니다. 패널이 그리기마다 부릅니다. */
        void enterPendingNested( GameObjectManager* pManager, EditorSelection& selection );
        /** @brief 중첩 프리팹 @p prefabPath 를 격리합니다. 지금 단계 아래에서 그 인스턴스를 찾고 없으면 스폰합니다. 쌓았으면 true 입니다. */
        bool enterNested( GameObjectManager* pManager, EditorSelection& selection, string_view prefabPath );
        /**
         * @brief 지금 단계를 나갑니다. @p bSaveToPrefab 이면 먼저 지금 단계의 루트를 프리팹 파일에 씁니다. 쓰지 못하면 나가지 않습니다.
         * @details 중첩 단계면 한 단계 내려가고(저장하지 않고 나가면 이 단계가 스폰한 루트를 지운다) 아래 단계 루트를 고른다. 문서 단계면 스택을 두고 `DocumentStep` 을 돌려준다.
         */
        PrefabFrameExit exitFrame( GameObjectManager* pManager, EditorSelection& selection, bool bSaveToPrefab );
        /** @brief 지금 단계의 루트를 그 단계의 프리팹 파일에 씁니다. 결과는 상태 줄에 남는다. 썼으면 true 입니다. */
        [[nodiscard]] bool saveFrame( GameObjectManager* pManager );

    private:
        EditorPrefabIsolation _isolation;
        string                _pendingNestedPath;
        string                _spawnedDocumentPath; ///< 문서 루트를 마지막으로 스폰한 프리팹 경로
        string                _status;
    };
} // namespace sw::editor
