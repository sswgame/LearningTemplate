/**
 * @file SourceControlChangesViewModel.h
 * @brief 변경 목록 창의 ViewModel — 에디터 컨텍스트의 버전 관리(`EditorSourceControl`)를 찾습니다. ImGui 를 모릅니다.
 * @details 패널(`SourceControlChangesPanel`)은 작업 공간 헤더(`EditorContext` · `EditorService`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약).
 *          버전 관리 동작(diff · 이력 · 되돌리기)은 디스크 · 외부 도구 일이라 되돌리기 스택에 남지 않는다(언리얼 Source Control 창과 같다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    class EditorSourceControl;

    /** @brief 변경 목록 창의 ViewModel 입니다. 파일 머리말 참고. */
    class SourceControlChangesViewModel
    {
    public:
        /** @brief 버전 관리입니다. 에디터가 서 있을 때(그리기 · 단추)만 부른다 — 없으면 멈춘다. */
        static EditorSourceControl& getSourceControl();
        /** @brief 버전 관리입니다. 에디터 컨텍스트가 없으면(감시 값 · 개발 명령이 컨텍스트 없이 불릴 때) nullptr 입니다. */
        static EditorSourceControl* findSourceControl();
    };
} // namespace sw::editor
