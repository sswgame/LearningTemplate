/**
 * @file SourceControlChangesModel.h
 * @brief 변경 창의 모델 — 에디터 컨텍스트의 버전 관리(`EditorSourceControl`)를 찾습니다. ImGui 를 모릅니다.
 * @details 버전 관리 층(`Common/SourceControl`)은 작업 공간보다 아래라 스스로 컨텍스트를 찾을 수 없고, View 는 작업 공간 헤더(`EditorContext`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약).
 *          그래서 찾는 길을 이 패널 Model 에 둔다. 상태 · 목록 · 명령은 `EditorSourceControl` 이 그대로 든다(변경 목록은 문서가 아니라 디스크 상태라 되돌리기에 남지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    class EditorSourceControl;

    /** @brief 변경 창의 모델입니다. */
    struct SourceControlChangesModel
    {
        /** @brief 지금 에디터 컨텍스트의 버전 관리입니다. 컨텍스트가 없으면(에디터 밖 · 종료 뒤) nullptr 입니다. */
        static EditorSourceControl* findSourceControl();
        /** @brief 지금 에디터 컨텍스트의 버전 관리입니다. 에디터가 서 있을 때만 부른다 — 없으면 멈춘다. */
        static EditorSourceControl& getSourceControl();
    };
} // namespace sw::editor
