/**
 * @file BoneHierarchyPopup.h
 * @brief 본 계층을 보여 주는 플로팅 팝업입니다(IEditorPopup 구현).
 */
#pragma once
#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{
    /**
     * @class BoneHierarchyPopup
     * @brief 선택한 오브젝트의 본 · 계층 구조를 확인하는 플로팅 팝업입니다.
     */
    class BoneHierarchyPopup : public IEditorPopup
    {
    public:
        BoneHierarchyPopup();
        virtual ~BoneHierarchyPopup() override = default;

        // ------------------------------------------------------------------------------
        // IEditorPopup 구현
        // ------------------------------------------------------------------------------
        virtual const utf8* getPopupId() const override { return "BoneHierarchy"; }
        virtual const utf8* getPopupTitle() const override { return "Hierarchy / Skeleton View"; }

        // ------------------------------------------------------------------------------
        // 정적 편의 함수
        // ------------------------------------------------------------------------------
        static void open();
        static void close();
        static void toggle();
        static bool isOpen();

    protected:
        virtual void drawContent() override;
    };
} // namespace sw::editor
