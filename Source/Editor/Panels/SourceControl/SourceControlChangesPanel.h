/**
 * @file SourceControlChangesPanel.h
 * @brief 버전 관리 변경 목록 창입니다 — 바뀐 파일 · 고른 파일의 diff · 이력 · 되돌리기(git, 외부 계정 없음).
 * @details 상태와 명령은 `EditorSourceControl`(Common/SourceControl, ImGui 없음)이 들고, 이 창은 그리기와 확인만 한다. 언리얼 View Changes(Changelists) 창과
 *          같은 자리이고, 커밋 · 푸시 · 브랜치는 쓰는 사람의 git 도구에 둔다(지원 범위를 툴바 툴팁에 적는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 바뀐 파일 표 · 상세(diff · 이력) · 되돌리기 확인을 그리는 도구 창입니다. */
    class SourceControlChangesPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다(`SW_EDITOR_PANEL`). */
        static constexpr const utf8* kPanelID = "source_control_changes";

        /** @brief 창을 만듭니다(닫힌 채 시작한다). */
        SourceControlChangesPanel();

        /** @brief 창 제목입니다. */
        const utf8* getPanelTitle() const override { return "Changes"; }
        /** @brief 툴바 · 표 · 상세를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */

    private:
        /** @brief 고른 파일의 Diff · History · Revert 단추와 상세 줄을 그립니다. */
        void drawSelectedDetail();
        /** @brief 되돌리기 확인 모달입니다(저장하지 않은 변경을 버린다). */
        void drawRevertConfirm();

    private:
        string                 _selectedPath;    ///< 고른 파일(저장소 기준 경로). 비었으면 없다
        string                 _revertPath;      ///< 확인을 기다리는 되돌리기
        uint8                  _bOpenRevert : 1; ///< 다음 그리기에서 되돌리기 확인 모달을 연다
        uint8                  _bRequested  : 1; ///< 창을 처음 그릴 때 변경 목록을 청했다
        [[maybe_unused]] uint8 _reserved    : 6;
    };
} // namespace sw::editor
