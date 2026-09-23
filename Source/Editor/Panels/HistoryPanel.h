/**
 * @file HistoryPanel.h
 * @brief 실행 취소 · 다시 실행 내역을 보여 주고 원하는 시점으로 되돌리는 히스토리 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 작업 히스토리 스택을 살펴보고 특정 시점으로 이동하는 에디터 도구 창입니다. */
    class HistoryPanel : public IEditorPanel
    {
    public:
        /** @brief 히스토리 창을 만듭니다. */
        HistoryPanel();
        /** @brief 소멸자. */
        virtual ~HistoryPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "History"; }
        /** @brief 히스토리 UI를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
    };
} // namespace sw::editor
