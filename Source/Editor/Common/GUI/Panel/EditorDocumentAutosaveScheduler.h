/**
 * @file EditorDocumentAutosaveScheduler.h
 * @brief 문서 패널 자동 저장의 주기입니다 — 열린 문서 패널의 사본(`IEditorDocumentPanel::takeAutosaveCopy`)을 주기마다 모아 워커로 씁니다(감사 E-1). ImGui 를 그리지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class IEditorPanel;

    /**
     * @struct EditorDocumentAutosaveScheduler
     * @brief 주기(`EditorToolDefaults::_documentAutosaveSeconds` — 설정 필수)가 지나면 dirty 문서의 사본을 모아 쓰기 잡(`EditorDocumentAutosaveWriteJob`)에 넘깁니다.
     * @details 상호작용 구간 중인 문서(`DocumentAutosaveResult::Deferred`)는 이 판에서 빼고 주기를 다시 재지 않는다 — 구간이 끝난 첫 프레임에 그 문서만 다시 나온다(다른 문서는 글이 같아 다시 나오지 않는다).
     *          낸 잡이 끝나지 않았으면 내지 않고 다음 프레임에 다시 본다(앞 묶음을 새 세대가 버리지 않게). 사본 폴더는 씬 자동 저장의 세션 폴더다 — 세션이 없으면 쓰지 않는다.
     *          상태(주기 시계 · 잡)는 에디터 상태 저장소가 든다. 패널 관리자가 패널을 다 그린 뒤 프레임마다 부른다.
     */
    struct SW_EDITOR_API EditorDocumentAutosaveScheduler
    {
        /** @brief 패널 @p listPanel 을 보고 주기가 되었으면 사본을 냅니다. */
        static void tick( const vector<IEditorPanel*>& listPanel );
        /** @brief 이번 세션에 워커가 쓴 문서 사본 수입니다(감시 값 `Editor.AutosaveDocumentCopyCount`). */
        static uint32 getWrittenCopyCount();
    };
} // namespace sw::editor
