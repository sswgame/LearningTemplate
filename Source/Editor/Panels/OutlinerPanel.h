#pragma once
/**
 * @file OutlinerPanel.h
 * @brief 활성 씬 GameObject / Component 계층 아웃라이너
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief SceneManager 활성 씬의 오브젝트 트리를 표시·선택합니다. */
	class OutlinerPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Hierarchy"; }
		void		draw( const EditorUIContext& ctx ) override;
	};
} // namespace sw
