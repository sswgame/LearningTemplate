/**
 * @file BoneHierarchyPopup.h
 * @brief 본 계층을 보여 주는 플로팅 팝업입니다(IEditorPopup 구현). 줄은 `BoneHierarchyViewModel::gather` 가 모으고 이 팝업은 그리기만 합니다(MVVM 계약).
 * @details 열기 · 닫기는 팝업 관리자(`EditorPopupManager::openPopup( kPopupID )`)가 한다 — 팝업마다 같은 일을 하는 정적 창구를 두지 않는다.
 */
#pragma once
#include "Editor/Common/GUI/IEditorPopup.h"
#include "Editor/Panels/Animation/Model/BoneHierarchyViewModel.h"

namespace sw::editor
{
    /**
     * @class BoneHierarchyPopup
     * @brief 선택한 오브젝트의 본 · 계층 구조를 확인하는 플로팅 팝업입니다.
     */
    class BoneHierarchyPopup : public IEditorPopup
    {
    public:
        /** @brief 팝업 매니저에서 이 팝업을 찾는 id 입니다. */
        static constexpr const utf8* kPopupID = "BoneHierarchy";

        BoneHierarchyPopup();
        virtual ~BoneHierarchyPopup() override = default;

        // ------------------------------------------------------------------------------
        // IEditorPopup 구현
        // ------------------------------------------------------------------------------
        virtual const utf8* getPopupID() const override { return kPopupID; }
        virtual const utf8* getPopupTitle() const override { return "Hierarchy / Skeleton View"; }

    protected:
        virtual void drawContent() override;

    private:
        /** @brief 펴 둔 줄로 트리를 그립니다. 접힌 오브젝트 아래 줄은 건너뜁니다. */
        void drawTree();

    private:
        BoneHierarchyViewModel _viewModel;
        BoneHierarchyViewState _viewState; ///< 프레임마다 `gather` 가 채운다
    };
} // namespace sw::editor
