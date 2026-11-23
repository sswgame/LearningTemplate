/**
 * @file EditorStateDocumentPanel.h
 * @brief 문서가 에셋 파일 하나가 아닌 문서 패널의 기반입니다 — 실시간 상태(전역 변수 · 메시 모디파이어 세션)나 설정 파일 묶음(프로젝트 설정)을 저장합니다.
 * @details 문서 패널의 기반은 둘이고 `EditorDocumentPanelBase` 을 직접 상속할 수 없다(생성자가 두 기반에만 열려 있다). 에셋 파일 하나를 고치는 패널은
 *          `EditorDocumentPanel`(워크스페이스 포커스 → `canAcceptDocumentPath` → `loadDocument`, 저장 왕복 시나리오가 사본을 연다)이고, 이 기반은 연 문서 경로가 없는 패널이다 —
 *          언리얼 Project Settings · Console Variables 창이 에셋 편집기(`FAssetEditorToolkit`)가 아닌 것과 같다. 예전에는 에셋 파일을 고치는 패널도 직접 상속해
 *          경로 칸 · 목록마다 자체 열기 길과 문서 경로를 따로 들었고, 시나리오가 사본을 가리킬 수 없었다.
 *          에셋 파일 하나를 고치는 새 패널이 이 기반을 고르면 안 된다 — 열기 길과 저장 왕복을 잃는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"

namespace sw::editor
{
    /**
     * @class EditorStateDocumentPanel
     * @brief 연 문서 경로가 없는 문서 패널입니다. 파일 머리말 참고. 필요할 때 여는 도구라 닫힌 채 시작합니다.
     * @details 자동 저장 사본은 지금 저장하면 쓸 파일(`collectFilesToSave`)마다 하나다 — 사본 글은 파생이 만들고(`tryMakeFileText`), 복원은 파생이 자기 파일인지 보고(`isEditingFile`) 적용한다(`applyFileText`).
     */
    class SW_EDITOR_API EditorStateDocumentPanel : public EditorDocumentPanelBase
    {
    public:
        /** @brief 연 문서 경로가 없다 — 옮기기 · 삭제 계획이 볼 에셋 파일이 아니다. */
        [[nodiscard]] string_view getDocumentAssetPath() const final { return {}; }
        /** @brief 저장할 파일마다 사본 하나를 냅니다. 지난 사본과 같은 글은 내지 않고, 더는 고친 것이 아닌 파일의 사본은 지운다. */
        [[nodiscard]] DocumentAutosaveResult takeAutosaveCopies( vector<EditorDocumentAutosaveEntry>& inoutListEntry ) final;
        /** @brief 상태 문서는 되돌리기 기록을 자기 모델(`EditSnapshotHistory` · 세션)에 두어 문서 글 왕복이 없다 — 전수 시나리오가 이 패널을 고르면 실패로 알린다. */
        [[nodiscard]] expected<void> validateUndoRoundTrip() final
        {
            return unexpected( Error{ ErrorKind::Unsupported, "a state document keeps its undo in its own model - it has no document text round trip", getPanelTitle() } );
        }

    protected:
        EditorStateDocumentPanel()
            : EditorDocumentPanelBase{ false }
            , _mapCopyTextHash{}
        {
        }

        /** @brief 지금 저장하면 파일 @p absolutePath(`collectFilesToSave` 가 낸 것)에 쓸 글입니다. */
        [[nodiscard]] virtual expected<string> tryMakeFileText( string_view absolutePath ) const = 0;
        /** @brief 지금 이 패널이 파일 @p absolutePath 를 고치고 있으면 true 입니다(복원할 사본의 주인인지). */
        [[nodiscard]] virtual bool isEditingFile( string_view absolutePath ) = 0;
        /** @brief `isEditingFile` 이 참인 파일의 사본 글 @p text 를 저장하지 않은 편집으로 적용합니다. 글을 읽지 못하면 false 이고 상태는 그대로입니다. */
        [[nodiscard]] virtual bool applyFileText( string_view absolutePath, const string& text ) = 0;

        /** @brief 복원을 청한 사본이 이 패널의 파일이면 적용하고, 저장 · 되돌리기로 깨끗해졌으면 사본을 지웁니다. */
        void flushDocumentInteraction() final;

    private:
        /** @brief 낸 사본 가운데 @p listPath 에 없는 파일의 사본을 지웁니다. */
        void removeStaleCopies( const vector<string>& listPath );

    private:
        unordered_map<string, uint64> _mapCopyTextHash; ///< 사본을 낸 파일 → 그 글의 해시(같은 글을 다시 내지 않고, 깨끗해지면 지울 사본을 안다)
    };
} // namespace sw::editor
