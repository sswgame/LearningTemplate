/**
 * @file EditorFocus.h
 * @brief 에디터가 지금 겨누는 대상(초점 Undo 스택 · 초점 선택)과 미리보기 씬의 선택 목록입니다 — 에디터 서브시스템이라 컨텍스트와 함께 생기고 사라진다.
 */
#pragma once
#include "Core/Container/vector.h"

#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class CommandStack;
} // namespace sw

namespace sw::editor
{
    class EditorSelection;

    /**
     * @class EditorFocus
     * @brief `editor::getFocusedCommandStack` · `getFocusedSelection` · `registerPreviewSelection` 의 상태입니다(정적 칸에 두지 않는다 — 범위마다 새로 선다).
     * @details 미리보기 패널이 포커스를 얻으면 자기 스택 · 선택을 걸고 잃으면 푼다. 빌린 포인터만 든다. 에디터 UI 스레드에서만 쓴다.
     */
    class EditorFocus final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorFocus";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        CommandStack*            _pFocusedCommandStack{ nullptr }; ///< 포커스된 툴 창의 Undo 스택. nullptr 이면 메인 스택
        EditorSelection*         _pFocusedSelection{ nullptr };    ///< 겨누는 미리보기 선택. nullptr 이면 메인 선택
        vector<EditorSelection*> _listPreviewSelection;            ///< 미리보기 씬에 묶인 선택(빌림)
    };
} // namespace sw::editor
