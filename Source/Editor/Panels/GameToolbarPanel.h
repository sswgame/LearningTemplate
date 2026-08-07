#pragma once
/**
 * @file GameToolbarPanel.h
 * @brief 플레이어 속도 등 게임 런타임 조절 툴바 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief 게임 플레이 관련 간단 컨트롤 툴바 */
	class GameToolbarPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Game Toolbar"; }
		/** @brief 플레이어 속도 등 런타임 슬라이더/버튼을 그립니다. */
		void		draw( const EditorUIContext& ctx ) override;
	};
}
