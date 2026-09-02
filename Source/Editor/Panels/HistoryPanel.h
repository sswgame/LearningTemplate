/**
 * @file HistoryPanel.h
 * @brief 실행 취소/다시 실행 작업 내역을 표시하고 롤백/복원하는 히스토리 윈도우
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 작업 히스토리 스택을 검사하고 특정 시점으로 점프하는 에디터 도구 윈도우 */
    class HistoryPanel : public IEditorPanel
    {
    public:
        /** @brief 히스토리 윈도우를 생성합니다. */
        HistoryPanel();
        /** @brief 소멸자. */
        virtual ~HistoryPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 윈도우 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "History"; }
        /** @brief 히스토리 UI를 그립니다. */
        void drawContent() override;
        /** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
    };
} // namespace sw::editor
