#pragma once
/**
 * @file GameViewPanel.h
 * @brief 게임 렌더 타깃을 표시하는 Game View 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief 게임 프레임 버퍼를 ImGui 이미지로 표시하는 뷰포트 패널 */
	class GameViewPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Game View"; }
		/** @brief 게임 렌더 텍스처를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;
	};
} // namespace sw
