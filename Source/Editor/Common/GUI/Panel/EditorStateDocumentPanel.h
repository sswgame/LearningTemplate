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
#include "Editor/Common/GUI/Panel/EditorDocumentPanelBase.h"

namespace sw::editor
{
    /**
     * @class EditorStateDocumentPanel
     * @brief 연 문서 경로가 없는 문서 패널입니다. 파일 머리말 참고. 필요할 때 여는 도구라 닫힌 채 시작합니다.
     * @details 헤더만의 형식이라 내보내지 않는다(내보낸 기반 `EditorDocumentPanelBase` 을 상속할 뿐이고 확장 DLL 의 패널도 이것을 그대로 짓는다).
     */
    class EditorStateDocumentPanel : public EditorDocumentPanelBase
    {
    public:
        /** @brief 연 문서 경로가 없다 — 옮기기 · 삭제 계획이 볼 에셋 파일이 아니다. */
        [[nodiscard]] string_view getDocumentAssetPath() const final { return {}; }
        /** @brief 상태 문서는 되돌리기 기록을 자기 모델(`EditSnapshotHistory` · 세션)에 두어 문서 글 왕복이 없다 — 전수 시나리오가 이 패널을 고르면 실패로 알린다. */
        /** @brief 상태 문서는 연 에셋 파일이 없어 사본을 두지 않는다(shortcut: 프로젝트 설정 · 전역 변수 · 메시 모디파이어는 자동 저장이 없다 — 사고가 잦으면 상태 글을 같은 폴더에 둔다). */
        [[nodiscard]] DocumentAutosaveResult takeAutosaveCopy( string& /*outDocumentPath*/, string& /*outText*/ ) final { return DocumentAutosaveResult::Nothing; }
        [[nodiscard]] expected<void>         validateUndoRoundTrip() final
        {
            return unexpected( Error{ ErrorKind::Unsupported, "a state document keeps its undo in its own model - it has no document text round trip", getPanelTitle() } );
        }

    protected:
        EditorStateDocumentPanel()
            : EditorDocumentPanelBase{ false }
        {
        }
    };
} // namespace sw::editor
