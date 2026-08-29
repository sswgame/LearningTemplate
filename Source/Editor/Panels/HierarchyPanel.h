/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 윈도우
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	/** @brief 활성 씬의 오브젝트 아웃라이너 */
	class HierarchyPanel : public IEditorPanel
	{
	public:
		HierarchyPanel();
		~HierarchyPanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Hierarchy"; }
		/** @brief Hierarchy UI를 그립니다. */
		void drawContent() override;

	private:
		uint64 _renamingObjectId;
		utf8   _arrFilterBuffer[constant::kMaxBuffer128];
		utf8   _arrRenameBuffer[constant::kMaxBuffer256];
		bool   _bFocusRenameInput;
	};
} // namespace sw::editor
