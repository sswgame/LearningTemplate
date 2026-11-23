/**
 * @file ProjectSettingsHistory.h
 * @brief 프로젝트 설정 창의 되돌리기 스택입니다 — 형식 페이지의 그리드 편집 · 기본값 되돌리기와 표 페이지의 편집이 한 스택에 차례로 오릅니다(감사 E-13, ImGui 없음).
 * @details 형식 페이지는 인스턴스의 전체 글(`ProjectSettingsDocument::captureText`)을 구간마다 한 단계로 쥐고, 표 문서(`ProjectSettingsJSONDocument`)는 자기 트리 스냅숏 기록을
 *          가져 단계가 그 기록을 한 칸 움직인다(노드 배열을 따로 쥐지 않는다). 명령 스택은 작업 공간 형식이라 패널 뷰는 이 형식만 본다(`Config/Rules/Layers/EditorLayers.toml`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class EditorStateTextHistory;
    struct ProjectSettingsPage;

    /** @brief 단계가 문서를 움직인 뒤 부르는 처리기입니다 — 패널이 초안 · 미저장 표시를 맞춘다. */
    SW_DECLARE_DELEGATE( void, ProjectSettingsMovedDelegate, ProjectSettingsPage& page );

    /**
     * @class ProjectSettingsHistory
     * @brief 설정 페이지 편집 단계의 스택입니다. 단계는 페이지를 가리키므로 페이지를 다시 만들거나 파일로 되돌리면 비운다(`clear`).
     */
    class ProjectSettingsHistory
    {
    public:
        ProjectSettingsHistory();
        /** @brief 사라질 스택을 Ctrl+Z 가 겨누지 않게 놓는다. */
        ~ProjectSettingsHistory();
        ProjectSettingsHistory( const ProjectSettingsHistory& )            = delete;
        ProjectSettingsHistory& operator=( const ProjectSettingsHistory& ) = delete;

        /** @brief Ctrl+Z 가 갈 스택입니다(`EditorPanel::findPanelCommandStack`). */
        CommandStack& getStack();
        /** @brief 형식 페이지 @p page 의 편집을 기록합니다(고른 페이지가 바뀔 때 부른다 — 지금 글이 기준이다). 표 페이지 · 열지 못한 페이지는 글 기록이 없다. */
        void selectPage( ProjectSettingsPage& page, const ProjectSettingsMovedDelegate& onMoved );
        /** @brief 고른 형식 페이지를 고쳤습니다. @p bInteracting 이면 놓을 때(`flush`) 한 단계다. */
        void notifyEdited( string_view label, bool bInteracting );
        /** @brief 쥔 편집이 있고 위젯이 풀렸으면 한 단계로 올립니다. */
        void flush( bool bInteracting );
        /** @brief @p page 의 표 문서가 만든 되돌리기 단계 하나를 올립니다. 단계가 문서를 움직이면 @p onMoved 를 부른다. */
        void pushTableStep( ProjectSettingsPage& page, const ProjectSettingsMovedDelegate& onMoved );
        /** @brief 단계를 모두 버립니다. 글 기록의 대상도 놓는다 — 이어 고칠 페이지는 다시 `selectPage` 한다. */
        void clear();

    private:
        unique_ptr<EditorStateTextHistory> _pHistory;
    };
} // namespace sw::editor
