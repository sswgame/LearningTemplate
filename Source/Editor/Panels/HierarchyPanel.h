/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 윈도우
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	/** @brief 활성 씬의 오브젝트 아웃라이너 */
	class HierarchyPanel : public IEditorPanel
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Hierarchy"; }
		/** @brief Hierarchy UI를 그립니다. */
		void drawContent() override;

	private:
		/** @brief Save Scene 파일 대화상자 결과. */
		static void onSaveScenePicked( const vector<string>& paths );

	private:
		utf8 _arrFilterBuffer[constant::kMaxBuffer128]{};
	};
} // namespace sw::editor
