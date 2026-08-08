#pragma once
/**
 * @file GameViewPanel.h
 * @brief 게임 렌더 타깃 + ImGuizmo 를 표시하는 Game View 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief 게임 프레임 버퍼를 ImGui 이미지로 표시하고 선택 오브젝트에 기즈모를 그리는 뷰포트 패널 */
	class GameViewPanel : public IEditorPanel
	{
	public:
		GameViewPanel();

		const char* getWindowTitle() const override { return "Game View"; }
		/** @brief 게임 렌더 텍스처와 선택 오브젝트 기즈모를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		float _view[16]{};
		float _proj[16]{};
		int	  _operation = 0; ///< 0=Translate, 1=Rotate, 2=Scale
	};
} // namespace sw
