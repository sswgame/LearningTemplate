/**
 * @file ProjectSettingsTableHistory.h
 * @brief 프로젝트 설정 표 페이지의 되돌리기 스택입니다 — 창에 포커스가 오면 Ctrl+Z 가 이리 간다(감사 E-13, ImGui 없음).
 * @details 표 문서(`ProjectSettingsJSONDocument`)가 자기 트리 스냅숏 기록을 가지고, 이 스택의 단계는 그 기록을 한 칸 움직인다(단계가 노드 배열을 따로 쥐지 않는다).
 *          명령 스택은 작업 공간 형식이라 패널 뷰는 이 형식만 본다(`Config/Rules/Layers/EditorLayers.toml`).
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class UndoStepOwner;
    struct ProjectSettingsPage;

    /** @brief 표 단계가 문서를 움직인 뒤 부르는 처리기입니다 — 패널이 초안 · 미저장 표시를 맞춘다. */
    SW_DECLARE_DELEGATE( void, ProjectSettingsTableMovedDelegate, ProjectSettingsPage& page );

    /**
     * @class ProjectSettingsTableHistory
     * @brief 표 페이지 편집 단계의 스택입니다. 단계는 페이지를 가리키므로 페이지를 다시 만들거나 되돌리면 비운다(`clear`).
     */
    class ProjectSettingsTableHistory
    {
    public:
        ProjectSettingsTableHistory();
        /** @brief 사라질 스택을 Ctrl+Z 가 겨누지 않게 놓는다. */
        ~ProjectSettingsTableHistory();
        ProjectSettingsTableHistory( const ProjectSettingsTableHistory& )            = delete;
        ProjectSettingsTableHistory& operator=( const ProjectSettingsTableHistory& ) = delete;

        /** @brief Ctrl+Z 가 갈 스택입니다(`IEditorPanel::findPanelCommandStack`). */
        CommandStack& getStack() { return *_pStack; }
        /** @brief @p page 의 표 문서가 만든 되돌리기 단계 하나를 올립니다. 단계가 문서를 움직이면 @p onMoved 를 부른다. */
        void push( ProjectSettingsPage& page, const ProjectSettingsTableMovedDelegate& onMoved );
        /** @brief 단계를 모두 버립니다. */
        void clear();

    private:
        unique_ptr<CommandStack>  _pStack;
        unique_ptr<UndoStepOwner> _pOwner; ///< 단계의 주인 — 이 기록이 사라지면 그 단계가 스택에서 빠진다
    };
} // namespace sw::editor
