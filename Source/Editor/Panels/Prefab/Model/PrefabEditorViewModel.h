/**
 * @file PrefabEditorViewModel.h
 * @brief 프리팹 창의 ViewModel — 격리 스택(`EditorPrefabIsolation`), 문서 루트 스폰 · 다시 세우기, 중첩 프리팹 들어가기 요청 · 나가기, 지금 단계 저장, 상태 줄, 창이 그릴 값(`gather`). ImGui 를 모릅니다.
 * @details 패널(`PrefabEditorPanel`)은 미리보기 씬 · 뷰포트 · 기즈모 · 창 Undo 스택을 쥐고(`EditorPreviewPanel`) 막대 · 자식 목록을 그리며, 문서 dirty · 창 닫기를 맡는다 —
 *          래그돌 · VFX 미리보기 `Model/` 과 같은 선이다(언리얼 블루프린트 편집기의 툴킷 상태와 뷰포트, 유니티 프리팹 모드의 PrefabStage 와 Scene 뷰가 나뉜 것과 같다).
 *          씬과 창 선택은 패널이 넘긴다(ViewModel 은 씬을 갖지 않는다). 미리보기 씬의 편집은 창 Undo 스택이 맡으므로 명령은 되돌리기를 남기지 않는다.
 *          `docs/agents/EditorUI.md` MVVM 계약 — 헤더는 모델 헤더를 보지 않고 격리 스택은 `unique_ptr` 로 든다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

namespace sw
{
    class Scene;
} // namespace sw

namespace sw::editor
{
    class EditorPrefabIsolation;
    class EditorSelection;

    /** @brief `PrefabEditorViewModel::exitFrame` 의 결과입니다. 패널이 문서 dirty · 창 닫기 · 알림을 이것으로 고른다. */
    enum class PrefabFrameExit : uint8
    {
        Failed,       ///< 단계가 없거나 저장하지 못했다 — 나가지 않았다
        DocumentStep, ///< 문서 단계다 — 스택은 그대로이고 패널이 창을 닫는다(문서 루트는 씬에 남는다)
        PoppedNested, ///< 중첩 단계를 하나 내려갔다
    };

    /** @brief 지금 단계 루트 아래 오브젝트 한 줄입니다(값만). 순서는 깊이 우선이다. */
    struct PrefabEditorChildRow
    {
        string _name;
        uint64 _objectID{ 0 };
        int32  _depth{ 0 };
        uint8  _bSelected{ SW_FALSE };
    };

    /** @brief 프리팹 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct PrefabEditorViewState
    {
        vector<PrefabEditorChildRow> _listChildRow{};
        string                       _prefabPath{}; ///< 지금 단계의 프리팹 경로(격리 중이 아니면 비었다)
        string                       _status{};
        uint8                        _bIsolating{ SW_FALSE }; ///< 문서가 열려 루트가 격리 스택에 있다
        uint8                        _bNested{ SW_FALSE };    ///< 지금 단계가 중첩 프리팹이다(문서 단계 위)
    };

    /**
     * @class PrefabEditorViewModel
     * @brief 프리팹 창의 상태와 명령입니다. 파일 머리말 참고.
     */
    class PrefabEditorViewModel
    {
    public:
        PrefabEditorViewModel();
        ~PrefabEditorViewModel();

        /** @brief 창이 다루는 문서 종류(프리팹)입니다 — View 가 에셋 헤더를 보지 않게 여기를 지난다. */
        [[nodiscard]] static EditorAssetType getDocumentType();

        /**
         * @brief 문서 @p prefabPath 를 워크스페이스 포커스로 넘기고 @p panelID 창 열기를 요청하며 알립니다. 경로가 비었거나 플레이 중이면 false 입니다.
         * @details 활성 씬은 건드리지 않는다 — 씬이 더러워도 열 수 있다. 문서 교체와 미저장 확인은 창의 다음 그리기가 한다.
         */
        [[nodiscard]] static bool requestOpenWindow( string_view prefabPath, string_view panelID );
        /** @brief 격리 단계를 나갔다고 알립니다. */
        static void notifyExited();

        /** @brief @p pScene(미리보기 씬)과 창 선택 @p selection 에서 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). */
        void gather( Scene* pScene, const EditorSelection& selection, PrefabEditorViewState& outState ) const;

        /** @brief 문서가 열려 루트가 격리 스택에 있으면 true 입니다. */
        bool isIsolating() const;
        /** @brief 지금 단계의 프리팹 경로입니다. 단계가 없으면 빈 글입니다. */
        string_view   getPrefabPath() const;
        const string& getStatus() const { return _status; }

        /** @brief 새 미리보기 씬에는 문서 루트가 아직 없다 — 격리 스택과 스폰한 경로를 잊습니다. */
        void resetForNewScene();
        /** @brief 다음 `needsRebuild` 가 문서 루트를 다시 스폰하게 합니다(문서를 다시 읽었다). */
        void requestRebuild() { _spawnedDocumentPath.clear(); }
        /** @brief 문서 @p documentPath 의 루트를 아직 스폰하지 않았으면 true 입니다. */
        bool needsRebuild( string_view documentPath ) const { return string_view{ _spawnedDocumentPath } != documentPath; }
        /**
         * @brief 옛 문서 루트를 지우고 @p documentPath 의 루트를 @p pScene 에 스폰해 격리 스택의 맨 아래 단계로 쌓고 고릅니다.
         * @details 실패해도 같은 경로로 프레임마다 다시 스폰하지 않게 스폰한 경로는 먼저 채운다(실패는 상태 줄에 남는다). 씬이 없으면 아무것도 하지 않는다.
         * @return 스폰했으면 true 입니다.
         */
        bool rebuildRoot( Scene* pScene, EditorSelection& selection, string_view documentPath );

        /** @brief 문서 프리팹 안의 @p prefabPath 중첩 프리팹을 격리하라고 요청합니다. 씬이 없을 수도 있어 `enterPendingNested` 가 쌓습니다. */
        void requestNested( string_view prefabPath ) { _pendingNestedPath = prefabPath; }
        /** @brief 요청한 중첩 프리팹이 있으면 격리합니다. 쌓지 못하면 상태 줄에 남깁니다. 패널이 그리기마다 부릅니다. */
        void enterPendingNested( Scene* pScene, EditorSelection& selection );
        /** @brief 중첩 프리팹 @p prefabPath 를 격리합니다. 지금 단계 아래에서 그 인스턴스를 찾고 없으면 스폰합니다. 쌓았으면 true 입니다. */
        bool enterNested( Scene* pScene, EditorSelection& selection, string_view prefabPath );
        /**
         * @brief 지금 단계를 나갑니다. @p bSaveToPrefab 이면 먼저 지금 단계의 루트를 프리팹 파일에 씁니다. 쓰지 못하면 나가지 않습니다.
         * @details 중첩 단계면 한 단계 내려가고(저장하지 않고 나가면 이 단계가 스폰한 루트를 지운다) 아래 단계 루트를 고른다. 문서 단계면 스택을 두고 `DocumentStep` 을 돌려준다.
         */
        PrefabFrameExit exitFrame( Scene* pScene, EditorSelection& selection, bool bSaveToPrefab );
        /** @brief 지금 단계의 루트를 그 단계의 프리팹 파일에 씁니다. 결과는 상태 줄에 남는다. 썼으면 true 입니다. */
        [[nodiscard]] bool saveFrame( Scene* pScene );
        /** @brief 자식 목록에서 고른 오브젝트 @p objectID 를 창 선택으로 삼습니다. 보기만 바꾸므로 되돌리기에 남지 않는다. 없으면 아무것도 하지 않습니다. */
        void selectObject( Scene* pScene, EditorSelection& selection, uint64 objectID ) const;

    private:
        unique_ptr<EditorPrefabIsolation> _pIsolation; ///< 격리 스택 — 명령 헤더라 앞 선언만 보고 .cpp 가 만든다
        string                            _pendingNestedPath;
        string                            _spawnedDocumentPath; ///< 문서 루트를 마지막으로 스폰한 프리팹 경로
        string                            _status;
    };
} // namespace sw::editor
