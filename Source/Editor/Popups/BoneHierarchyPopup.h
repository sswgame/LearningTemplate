/**
 * @file BoneHierarchyPopup.h
 * @brief 플로팅 본 계층 유틸리티 (IEditorPopup 구현체)
 */
#pragma once
#include "Editor/Common/Gui/IEditorPopup.h"

namespace sw::editor
{
	/**
	 * @class BoneHierarchyPopup
	 * @brief 선택된 오브젝트의 본/계층 구조를 확인하는 플로팅 팝업
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
		// 정적(Static) 편의 API
		// ------------------------------------------------------------------------------
		static void open();
		static void close();
		static void toggle();
		static bool isOpen();

	protected:
		virtual void drawContent() override;
	};
} // namespace sw::editor
